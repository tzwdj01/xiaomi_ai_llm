/* Independent process; no native service, microphone, or cloud access. */
#define CONTROL_DIR "/tmp/s12_followup_safety_unit"
#define NATIVE_BUSY_FILE CONTROL_DIR "/busy"
#define FAILURE_ARMED_FILE CONTROL_DIR "/armed"
#define S12_ISOLATED_UNIT_TEST 1
#define main s12_descriptor_probe_main
#include "abi_probe.c"
#undef main
static int original_callback(void *,int,float);
static void *test_dlsym(void *,const char *);
#define S12_EXPECTED_WAKE_CALLBACK ((uintptr_t)original_callback)
#define dlsym test_dlsym
#include "native_asr_s12_experiment.c"
#undef dlsym
#undef NDEBUG
#include <assert.h>

static unsigned calls;
static int original_callback(void *ctx,int code,float angle) {
    assert(ctx==&calls && code==1 && angle==90.0f);calls++;return 7;
}
static int unused_callback(void *ctx,int code,float angle) {(void)ctx;(void)code;(void)angle;return -1;}
static int expect_wrapped;
static void *fake_create(void *params,void *second) {
    assert(second==(void *)(uintptr_t)55);
    s12_wake_fn cb=NULL;void *cookie=NULL;
    memcpy(&cb,(unsigned char *)params+S12_MDSPEECH_WAKE_OFFSET,sizeof(cb));
    memcpy(&cookie,(unsigned char *)params+S12_MDSPEECH_COOKIE_OFFSET,sizeof(cookie));
    assert(cookie==&calls);
    if(expect_wrapped){assert(cb==s12_physical_wake && cb(cookie,1,90)==7);}
    else assert(cb==unused_callback);
    return (void *)(uintptr_t)42;
}
static void *test_dlsym(void *handle,const char *name) {
    if(!strcmp(name,"mdspeech_create"))return (void *)fake_create;
    return dlsym(handle,name);
}
static void make_state(unsigned phase) {
    struct control s={.magic=CONTROL_MAGIC,.version=1,.sequence=3,
        .owner=(uint32_t)getpid(),.aivs_pid=(uint32_t)getpid(),
        .deadline=now_ms()+20000,.phase=phase};
    strcpy(s.dialog,"owned-test");strcpy(s.text,"stale test text");
    int fd=open(CONTROL_FILE,O_CREAT|O_TRUNC|O_RDWR|O_NOFOLLOW,0600);
    assert(fd>=0 && write(fd,&s,sizeof(s))==sizeof(s));close(fd);
}

int main(void) {
    assert(mkdir(CONTROL_DIR,0700)==0);
    void *lib=dlopen("/usr/lib/libaivs-message-util.so",RTLD_NOW|RTLD_GLOBAL);assert(lib);
    const message_desc *top=dlsym(lib,"speech_message__descriptor");
    const message_desc *up=dlsym(lib,"speech_message__upward_message__descriptor");
    const message_desc *prep=dlsym(lib,"speech_message__upward_message__stream_prepare_request_message__descriptor");
    void *(*unpack)(void *,size_t,const void *)=dlsym(lib,"speech_message__unpack");
    void (*free_msg)(void *,void *)=dlsym(lib,"speech_message__free_unpacked");
    assert(top && up && prep && unpack && free_msg);
    assert(top->size==48 && up->size==72 && prep->size==40);
    unsigned char *t=calloc(1,48),*u=calloc(1,72),*p=calloc(1,40);assert(t&&u&&p);
    top->init(t);up->init(u);prep->init(p);
    uint32_t zero=0,one=1;memcpy(t+24,&zero,4);memcpy(t+32,&u,sizeof(u));
    memcpy(u+24,&one,4);memcpy(u+40,&p,sizeof(p));memcpy(p+24,&zero,4);
    role=1;make_state(TRIGGERED);
    unsigned char encoded[4096];size_t n=speech_message__pack(t,encoded);assert(n);
    struct control s;int fd=state_open(&s);assert(fd>=0);
    assert(s.phase==PREPARED && s.packet_size==n && s.packet_hash==packet_hash(encoded,n));state_close(fd,NULL);
    unsigned char *decoded=unpack(NULL,n,encoded),*decoded_up=NULL,*decoded_p=NULL;
    assert(decoded);memcpy(&decoded_up,decoded+32,sizeof(decoded_up));assert(decoded_up);
    memcpy(&decoded_p,decoded_up+40,sizeof(decoded_p));assert(decoded_p);
    uint32_t mode=0;memcpy(&mode,decoded_p+24,4);assert(mode==1 && !memcmp(p+24,&zero,4));
    free_msg(decoded,NULL);
    make_state(IDLE);n=speech_message__pack(t,encoded);assert(n);
    decoded=unpack(NULL,n,encoded);assert(decoded);memcpy(&decoded_up,decoded+32,sizeof(decoded_up));
    memcpy(&decoded_p,decoded_up+40,sizeof(decoded_p));memcpy(&mode,decoded_p+24,4);assert(mode==0);
    free_msg(decoded,NULL);free(t);free(u);free(p);

    unsigned char params[S12_MDSPEECH_PARAMS]={0};void *cookie=&calls;
    s12_wake_fn callback=unused_callback;
    memcpy(params+S12_MDSPEECH_WAKE_OFFSET,&callback,sizeof(callback));
    memcpy(params+S12_MDSPEECH_COOKIE_OFFSET,&cookie,sizeof(cookie));
    assert(mdspeech_create(params,(void *)(uintptr_t)55)==(void *)(uintptr_t)42);
    assert(!s12_original_wake && !original_wake && !wake_context && !calls);
    callback=original_callback;memcpy(params+S12_MDSPEECH_WAKE_OFFSET,&callback,sizeof(callback));
    make_state(BOUND);fd=open(NATIVE_BUSY_FILE,O_CREAT|O_WRONLY,0600);assert(fd>=0);close(fd);
    expect_wrapped=1;
    assert(mdspeech_create(params,(void *)(uintptr_t)55)==(void *)(uintptr_t)42);
    s12_wake_fn original_params_callback=NULL;
    memcpy(&original_params_callback,params+S12_MDSPEECH_WAKE_OFFSET,sizeof(original_params_callback));
    assert(original_params_callback==original_callback && calls==1 && wake_context==&calls);
    fd=state_open(&s);assert(fd>=0);
    assert(s.phase==NATIVE_HANDOFF && !s.text[0] && !strcmp(s.dialog,"owned-test"));state_close(fd,NULL);
    assert(access(NATIVE_BUSY_FILE,F_OK)<0);
    assert(!original_ivw); /* No unverified S12 completion callback is available. */
    make_state(REQUEST);pthread_t thread;assert(!pthread_create(&thread,NULL,worker,NULL));
    usleep(100000);fd=state_open(&s);assert(fd>=0);
    assert(s.phase==FAILED && !s.mipns_pid && calls==1);state_close(fd,NULL);
    assert(!pthread_cancel(thread) && !pthread_join(thread,NULL));
    unlink(CONTROL_FILE);unlink(CONTROL_DIR "/events.log");unlink(CONTROL_DIR "/physical-wake");
    assert(rmdir(CONTROL_DIR)==0);dlclose(lib);
    puts("S12_FOLLOWUP_SAFETY_TEST_OK: pack, callback ABI, blocked unverified wake, physical handoff");
    return 0;
}
