/* Pure mock test, no native services, audio, or network. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static void original(void *,const void *);
static void wake(void),voip(void),event(void);
static void *mock_dlsym(void *,const char *);
#define S12_PCM_PROBE_UNIT_TEST 1
#define S12_EXPECTED_ASR_CB ((uintptr_t)original)
#define S12_EXPECTED_WAKE_CB ((uintptr_t)wake)
#define S12_EXPECTED_VOIP_CB ((uintptr_t)voip)
#define S12_EXPECTED_EVENT_CB ((uintptr_t)event)
#define dlsym mock_dlsym
#include "vpm_pcm_probe_experiment.c"
#undef dlsym
static int calls;static unsigned char packet[48];static int context;
static int expect_passthrough;
static void original(void *ctx,const void *p){assert(ctx==&context && p==packet);calls++;}
static void other(void *ctx,const void *p){(void)ctx;(void)p;}
static void wake(void){} static void voip(void){} static void event(void){}
static void mock_register(s12_asr_cb cb,void *w,void *v,void *e,void *ctx) {
    assert(w==(void *)wake && v==(void *)voip && e==(void *)event && ctx==&context);
    if(expect_passthrough)assert(cb==other);
    else {assert(cb!=original);cb(ctx,packet);}
}
static void *mock_dlsym(void *h,const char *name) {
    (void)h;assert(!strcmp(name,"register_vpm_data_upload_callback"));return (void *)mock_register;
}
int main(void) {
    struct s12_pcm_counts s={0};s12_counts=&s;
    uint32_t type=2,bytes=1920;void *pointer=&context;
    memcpy(packet,&type,4);memcpy(packet+8,&bytes,4);memcpy(packet+24,&pointer,sizeof(pointer));
    register_vpm_data_upload_callback(original,(void *)wake,(void *)voip,(void *)event,&context);
    assert(calls==1 && s.registered && s.packets==1 && s.audio_packets==1);
    assert(s.audio_bytes==1920 && s.valid_frames==6 && !s.invalid_frames);
    type=1;bytes=0;memcpy(packet,&type,4);memcpy(packet+8,&bytes,4);
    s12_observe_asr(&context,packet);assert(calls==2 && s.packets==2 && s.audio_packets==1);
    type=2;bytes=123;memcpy(packet,&type,4);memcpy(packet+8,&bytes,4);
    s12_observe_asr(&context,packet);assert(calls==3 && s.invalid_frames==1 && s.valid_frames==6);
    expect_passthrough=1;register_vpm_data_upload_callback(other,(void *)wake,(void *)voip,(void *)event,&context);
    assert(calls==3 && s.registered && s.invalid_frames==1);
    puts("S12_PCM_PROBE_MOCK_OK passthrough=3 audio_frames=6 invalid=1");return 0;
}
