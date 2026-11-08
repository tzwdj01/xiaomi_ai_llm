/* Experimental S12 1.74.18 ABI adaptation. NOT an installer or live hook.
 * The S12 wakeword-buffer completion callback has not been verified, so this
 * build has no production code to register one. The inherited worker can
 * claim readiness only in isolated tests that supply a mock. No native service
 * should load it. */
#ifndef S12_ISOLATED_UNIT_TEST
#error "Research code: compile only through the isolated S12 safety test"
#endif
_Static_assert(sizeof(void *)==8,"S12 research requires a 64-bit userland");
#define xaudio_register_callback unused_s12a_xaudio_register_callback
#define register_data_upload_callback unused_s12a_register_data_upload_callback
#define speech_message__pack unused_s12a_speech_message__pack
#include "../native_asr/native_asr.c"
#undef xaudio_register_callback
#undef register_data_upload_callback
#undef speech_message__pack
#include "s12_prepare.h"

#define S12_MDSPEECH_PARAMS 0x410u
#define S12_MDSPEECH_WAKE_OFFSET 0x3e8u
#define S12_MDSPEECH_COOKIE_OFFSET 0x3f0u
#ifndef S12_EXPECTED_WAKE_CALLBACK
#define S12_EXPECTED_WAKE_CALLBACK ((uintptr_t)0x4187f0)
#endif
typedef int (*s12_wake_fn)(void *,int,float);
static s12_wake_fn s12_original_wake;
static union {uint64_t align;unsigned char bytes[S12_MDSPEECH_PARAMS];} s12_params_copy;
static __thread int s12_wake_result;

/* Isolated mock only: the reviewed firmware's recorder copies these callback
 * slots. The offline emulator confirms the three-argument empty-buffer branch,
 * but runtime thread ownership and object lifetime remain unverified. There is
 * deliberately no production code that discovers or binds this pointer. */
typedef void (*s12_ivw_fn)(unsigned,void *,uint64_t);
static s12_ivw_fn s12_mock_ivw;
static void s12_mock_ivw_forward(unsigned result,void *buffer,unsigned length,
                                 unsigned unused,int flags) {
    if(s12_mock_ivw && result==1 && buffer && !length && !unused && !flags)
        s12_mock_ivw(result,buffer,(uint64_t)length);
}
static int s12_mock_bind_ivw(const unsigned char *object,uintptr_t expected_ivw,
                             uintptr_t expected_asr) {
    if(!object || !expected_ivw || !expected_asr || s12_mock_ivw)return 0;
    uintptr_t ivw=0,asr=0,context=0;
    memcpy(&ivw,object+0x30,sizeof(ivw));
    memcpy(&asr,object+0x38,sizeof(asr));
    memcpy(&context,object+0x58,sizeof(context));
    if(ivw!=expected_ivw || asr!=expected_asr || !context)return 0;
    pthread_mutex_lock(&wake_lock);
    s12_mock_ivw=(s12_ivw_fn)ivw;
    original_ivw=s12_mock_ivw_forward;
    pthread_mutex_unlock(&wake_lock);
    return 1;
}

static void s12_wake_forward(void *ctx,unsigned code,float angle) {
    s12_wake_result=s12_original_wake?s12_original_wake(ctx,(int)code,angle):-1;
}
static int s12_physical_wake(void *ctx,int code,float angle) {
    s12_wake_result=-1;
    wake_observed(ctx,(unsigned)code,angle);
    return s12_wake_result;
}

void *mdspeech_create(void *params,void *second) {
    void *(*fn)(void *,void *)=dlsym(RTLD_NEXT,"mdspeech_create");
    if (!fn) return NULL;
    if (role!=1 || !params) return fn(params,second);
    s12_wake_fn callback=NULL;void *cookie=NULL;
    memcpy(&callback,(unsigned char *)params+S12_MDSPEECH_WAKE_OFFSET,sizeof(callback));
    memcpy(&cookie,(unsigned char *)params+S12_MDSPEECH_COOKIE_OFFSET,sizeof(cookie));
    if ((uintptr_t)callback!=S12_EXPECTED_WAKE_CALLBACK || !cookie || s12_original_wake)
        return fn(params,second);
    memcpy(s12_params_copy.bytes,params,sizeof(s12_params_copy.bytes));
    pthread_mutex_lock(&wake_lock);
    s12_original_wake=callback;original_wake=s12_wake_forward;wake_context=cookie;
    /* Intentionally leave original_ivw NULL until its ABI/semantics are proven. */
    pthread_mutex_unlock(&wake_lock);
    s12_wake_fn replacement=s12_physical_wake;
    memcpy(s12_params_copy.bytes+S12_MDSPEECH_WAKE_OFFSET,&replacement,sizeof(replacement));
    return fn(s12_params_copy.bytes,second);
}

size_t speech_message__pack(const void *message,void *out) {
    s12_pack_fn fn=dlsym(RTLD_NEXT,"speech_message__pack");
    if (!fn) return 0;
    if (role!=1 || !s12_is_prepare(message)) return fn(message,out);
    struct control s;int fd=state_open(&s);
    if (fd<0) return fn(message,out);
    if (!active(&s) || s.phase!=TRIGGERED || !alive(s.aivs_pid)) {
        state_close(fd,NULL);return fn(message,out);
    }
    s12_size_fn size_fn=dlsym(RTLD_NEXT,"speech_message__get_packed_size");
    int rewritten=0;size_t n=s12_pack_nonwakeup(message,out,fn,size_fn,&rewritten);
    if (!n || !rewritten) {
        s.phase=FAILED;state_close(fd,&s);
        note("S12 prepare rejected seq=%u",s.sequence);
        return 0;
    }
    s.packet_hash=packet_hash(out,n);s.packet_size=(uint32_t)n;s.phase=PREPARED;
    state_close(fd,&s);note("S12 prepare seq=%u NONWAKEUP bytes=%zu",s.sequence,n);
    return n;
}
