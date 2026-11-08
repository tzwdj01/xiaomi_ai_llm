/* Independent process; no native service, microphone, or cloud access. */
#define CONTROL_DIR "/tmp/s12_followup_safety_unit"
#define NATIVE_BUSY_FILE CONTROL_DIR "/busy"
#define FAILURE_ARMED_FILE CONTROL_DIR "/armed"
#define S12_ISOLATED_UNIT_TEST 1
#include <stdatomic.h>
#include <unistd.h>
#define main s12_descriptor_probe_main
#include "abi_probe.c"
#undef main
static int original_callback(void *,int,float);
static void *test_dlsym(void *,const char *);
static int test_access(const char *,int);
static _Atomic int mute_result=-99,busy_result=-99;
#define S12_EXPECTED_WAKE_CALLBACK ((uintptr_t)original_callback)
#define dlsym test_dlsym
#define access test_access
#include "native_asr_s12_experiment.c"
#undef dlsym
#undef access
#undef NDEBUG
#include <assert.h>

static _Atomic unsigned calls;
static _Atomic unsigned ivw_calls;
static int original_callback(void *ctx,int code,float angle) {
    assert(ctx==&calls && code==1 && angle==90.0f);calls++;return 7;
}
static void mock_ivw(unsigned result,void *buffer,uint64_t length) {
    assert(result==1 && buffer && length==0);ivw_calls++;
}
static void mock_asr(void) {}
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
static int test_access(const char *path,int mode) {
    int result=access(path,mode);
    if(!strcmp(path,"/tmp/mipns/mute"))mute_result=result;
    if(!strcmp(path,NATIVE_BUSY_FILE))busy_result=result;
    return result;
}
static void make_state(unsigned phase) {
    struct control s={.magic=CONTROL_MAGIC,.version=1,.sequence=3,
        .owner=(uint32_t)getpid(),.aivs_pid=(uint32_t)getpid(),
        .deadline=now_ms()+20000,.phase=phase};
    strcpy(s.dialog,"owned-test");strcpy(s.text,"stale test text");
    int fd=open(CONTROL_FILE,O_CREAT|O_TRUNC|O_RDWR|O_NOFOLLOW,0600);
    assert(fd>=0 && write(fd,&s,sizeof(s))==sizeof(s));close(fd);
}
static void update_state(unsigned phase) {
    struct control s;int fd=state_open(&s);assert(fd>=0);
    s.phase=phase;s.owner=(uint32_t)getpid();s.aivs_pid=(uint32_t)getpid();
    s.deadline=now_ms()+20000;state_close(fd,&s);
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

    /* Only the isolated mock can bind a callback. Reject wrong companion
     * pointer and missing context before exercising the upstream worker. */
    unsigned char object[0x60]={0};
    uintptr_t ivw=(uintptr_t)mock_ivw,asr=(uintptr_t)mock_asr,context=(uintptr_t)&calls;
    memcpy(object+0x30,&ivw,sizeof(ivw));memcpy(object+0x38,&asr,sizeof(asr));
    assert(!s12_mock_bind_ivw(object,ivw,asr) && !original_ivw);
    memcpy(object+0x58,&context,sizeof(context));
    assert(!s12_mock_bind_ivw(object,ivw,asr+1) && !original_ivw);
    assert(s12_mock_bind_ivw(object,ivw,asr) && original_ivw);
    assert(!s12_mock_bind_ivw(object,ivw,asr));
    unsigned char empty=0;
    s12_mock_ivw_forward(2,&empty,0,0,0);
    s12_mock_ivw_forward(1,&empty,1,0,0);
    s12_mock_ivw_forward(1,&empty,0,0,1);
    assert(!ivw_calls);
    /* The real controller waits for the worker to publish its PID after it
     * has observed both callbacks; otherwise a stale callback snapshot can
     * correctly fail a request that races initialization. */
    int ready=0;
    for(unsigned i=0;i<100 && !ready;i++) {
        fd=state_open(&s);assert(fd>=0);ready=s.mipns_pid==(uint32_t)getpid();state_close(fd,NULL);
        if(!ready)usleep(10000);
    }
    assert(ready);
    /* A busy marker denies a wake. The test marker is inside CONTROL_DIR and
     * cannot affect the real assistant's busy state. */
    fd=open(NATIVE_BUSY_FILE,O_CREAT|O_EXCL|O_WRONLY,0600);assert(fd>=0);close(fd);
    update_state(REQUEST);
    int denied=0;
    for(unsigned i=0;i<100 && !denied;i++) {
        fd=state_open(&s);assert(fd>=0);denied=s.phase==FAILED;state_close(fd,NULL);
        if(!denied)usleep(10000);
    }
    assert(denied && calls==1 && !ivw_calls);
    assert(unlink(NATIVE_BUSY_FILE)==0);
    update_state(REQUEST);
    for(unsigned i=0;i<100 && !ivw_calls;i++)usleep(10000);
    if(ivw_calls!=1 || calls!=2) {
        fd=state_open(&s);assert(fd>=0);
        fprintf(stderr,"mock worker: phase=%u active=%d owner_alive=%d aivs_alive=%d wake=%d ivw=%d ctx=%d mute=%d busy=%d checked_mute=%d checked_busy=%d calls=%u ivw_calls=%u\n",
                s.phase,active(&s),alive(s.owner),alive(s.aivs_pid),original_wake!=NULL,
                original_ivw!=NULL,wake_context!=NULL,access("/tmp/mipns/mute",F_OK)==0,
                access(NATIVE_BUSY_FILE,F_OK)==0,(int)mute_result,(int)busy_result,
                (unsigned)calls,(unsigned)ivw_calls);
        state_close(fd,NULL);
    }
    assert(ivw_calls==1 && calls==2);
    usleep(100000);
    fd=state_open(&s);assert(fd>=0 && s.phase==TRIGGERED);state_close(fd,NULL);

    /* Cancellation between wake and buffer completion must suppress the
     * stale callback; this is a simulated controller and mock audio path. */
    update_state(REQUEST);
    for(unsigned i=0;i<100 && calls<3;i++)usleep(10000);
    assert(calls==3);fd=state_open(&s);assert(fd>=0);s.phase=FAILED;state_close(fd,&s);
    usleep(400000);assert(ivw_calls==1);
    assert(!pthread_cancel(thread) && !pthread_join(thread,NULL));
    unlink(CONTROL_FILE);unlink(CONTROL_DIR "/events.log");unlink(CONTROL_DIR "/physical-wake");
    assert(rmdir(CONTROL_DIR)==0);dlclose(lib);
    puts("S12_FOLLOWUP_SAFETY_TEST_OK: pack, mock IVW bridge, cancellation, physical handoff");
    return 0;
}
