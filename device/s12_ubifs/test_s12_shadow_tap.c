/* Synthetic memory only; never opens a microphone or native service. */
#define S12_SHADOW_ISOLATED_TEST 1
#include "s12_shadow_tap.h"
#include <stddef.h>
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

struct packet {uint32_t type,pad0,bytes,pad1;uint64_t stamp;const void *data;};
_Static_assert(offsetof(struct packet,bytes)==8,"packet size offset");
_Static_assert(offsetof(struct packet,data)==24,"packet data offset");
static unsigned native_calls;
static void original(void *context,const void *packet) {
    assert(context==&native_calls && packet);native_calls++;
}
static void initialize(struct neural_stream *s,struct s12_shadow_tap *t,uint32_t seq) {
    uint32_t id=(uint32_t)getpid();
    ns_init(s,seq,id,id);assert(ns_claim(s,seq,id,id));
    *t=(struct s12_shadow_tap){.stream=s,.sequence=seq,.owner=id,.producer=id};
}
int main(void) {
    struct neural_stream *s=malloc(sizeof(*s));assert(s);
    struct s12_shadow_tap tap;
    unsigned char pcm[1920];memset(pcm,0x5a,sizeof(pcm));
    struct packet p={.type=2,.bytes=sizeof(pcm),.data=pcm};
    initialize(s,&tap,1);s12_shadow_forward(&tap,original,&native_calls,&p);
    assert(native_calls==1 && s->used==1920 && !memcmp(s->pcm,pcm,1920));
    p.type=1;s12_shadow_forward(&tap,original,&native_calls,&p);
    assert(native_calls==2 && s->used==1920 && s->state==NS_LIVE);
    p.type=2;p.bytes=319;s12_shadow_forward(&tap,original,&native_calls,&p);
    assert(native_calls==3 && s->state==NS_INVALID);
    initialize(s,&tap,2);tap.sequence=1;p.bytes=1920;
    s12_shadow_forward(&tap,original,&native_calls,&p);
    assert(native_calls==4 && s->used==0 && s->state==NS_LIVE);
    tap.sequence=2;tap.lock=1;
    s12_shadow_forward(&tap,original,&native_calls,&p);
    assert(native_calls==5 && s->state==NS_LIVE && !s12_shadow_ok(&tap));
    initialize(s,&tap,3);s->used=NS_BYTES-320;
    s12_shadow_forward(&tap,original,&native_calls,&p);
    assert(native_calls==6 && s->state==NS_INVALID);
    initialize(s,&tap,4);p.data=NULL;
    s12_shadow_forward(&tap,original,&native_calls,&p);
    assert(native_calls==7 && s->state==NS_INVALID);
    free(s);
    puts("S12_SHADOW_TAP_TEST_OK pass_through stale concurrent overflow invalid");
    return 0;
}
