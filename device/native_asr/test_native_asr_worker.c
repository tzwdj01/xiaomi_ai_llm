/* Isolated worker regression: no native daemon, microphone, or cloud access. */
#define _GNU_SOURCE
#include <stdatomic.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#define CONTROL_DIR "/tmp/native_followup_worker_unit"
#define NATIVE_BUSY_FILE CONTROL_DIR "/busy"
static int worker_test_access(const char *,int);
#define access worker_test_access
#include "native_asr.c"
#undef access
#undef NDEBUG
#include <assert.h>

static _Atomic unsigned wake_calls,ivw_calls;
static int worker_test_access(const char *path,int mode) {
    if(!strcmp(path,"/tmp/mipns/mute")){errno=ENOENT;return -1;}
    return access(path,mode);
}
static void fake_wake(void *context,unsigned result,float angle) {
    assert(context==&wake_calls && result==1 && angle==90.0f);wake_calls++;
}
static void fake_ivw(unsigned result,void *data,unsigned length,unsigned reserved,int flags) {
    assert(result==1 && data && !length && !reserved && !flags);ivw_calls++;
}
static void set_phase(unsigned phase) {
    struct control s;int fd=state_open(&s);assert(fd>=0);
    s.phase=phase;s.deadline=now_ms()+20000;state_close(fd,&s);
}
static int wait_phase(unsigned phase) {
    for(unsigned i=0;i<100;i++) {
        struct control s;int fd=state_open(&s);assert(fd>=0);
        int matched=s.phase==phase;state_close(fd,NULL);
        if(matched)return 1;
        usleep(10000);
    }
    return 0;
}
int main(void) {
    assert(mkdir(CONTROL_DIR,0700)==0);
    struct control s={.magic=CONTROL_MAGIC,.version=1,.sequence=1,.owner=(uint32_t)getpid(),
        .aivs_pid=(uint32_t)getpid(),.deadline=now_ms()+20000,.phase=IDLE};
    int fd=open(CONTROL_FILE,O_CREAT|O_EXCL|O_WRONLY|O_NOFOLLOW,0600);assert(fd>=0);
    assert(write(fd,&s,sizeof(s))==sizeof(s));close(fd);
    original_wake=fake_wake;original_ivw=fake_ivw;wake_context=&wake_calls;last_angle=90.0f;
    pthread_t thread;assert(!pthread_create(&thread,NULL,worker,NULL));
    int ready=0;
    for(unsigned i=0;i<100 && !ready;i++) {
        fd=state_open(&s);assert(fd>=0);ready=s.mipns_pid==(uint32_t)getpid();state_close(fd,NULL);
        if(!ready)usleep(10000);
    }
    assert(ready);
    fd=open(NATIVE_BUSY_FILE,O_CREAT|O_EXCL|O_WRONLY,0600);assert(fd>=0);close(fd);
    set_phase(REQUEST);assert(wait_phase(FAILED));assert(!wake_calls && !ivw_calls);
    assert(unlink(NATIVE_BUSY_FILE)==0);
    set_phase(REQUEST);
    for(unsigned i=0;i<100 && !ivw_calls;i++)usleep(10000);
    assert(wake_calls==1 && ivw_calls==1);
    usleep(100000);set_phase(REQUEST);
    for(unsigned i=0;i<100 && wake_calls<2;i++)usleep(10000);
    assert(wake_calls==2);set_phase(FAILED);usleep(400000);assert(ivw_calls==1);
    assert(!pthread_cancel(thread) && !pthread_join(thread,NULL));
    assert(unlink(CONTROL_FILE)==0);unlink(CONTROL_DIR "/events.log");
    assert(rmdir(CONTROL_DIR)==0);
    puts("PASS: busy denies wake, idle triggers, cancellation drops stale completion");
    return 0;
}
