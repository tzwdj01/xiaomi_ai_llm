/* Standalone model-to-decision exercise. No native services or microphone. */
#include "s12_silero.h"
#include "../endpoint_probe/neural_decision.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

struct observed {int seen;uint32_t candidate;};
static int observe(struct s12_silero *vad,struct neural_stream *s,struct neural_decision *d,
                   const int16_t pcm[160],uint32_t now,struct observed *o) {
    float samples[160];for(unsigned i=0;i<160;i++)samples[i]=pcm[i]/32768.0f;
    if(!ns_append(s,pcm,320) || !s12_silero_accept(vad,samples,160))return 0;
    int detected=s12_silero_detected(vad);if(detected<0)return 0;
    if(detected){o->seen=1;o->candidate=0;}
    int segment;
    while((segment=s12_silero_pop_segment(vad))==1){o->seen=1;o->candidate=s->used/320;}
    if(segment<0)return 0;
    uint32_t proposal=!o->seen && s->used/320>=ND_START_FRAMES?ND_NO_SPEECH:o->candidate;
    nd_publish(d,(struct neural_proposal){s->producer,s->used/320,proposal,now,0});
    return 1;
}

int main(int argc,char **argv) {
    if(argc!=4)return 2;
    FILE *file=fopen(argv[3],"rb");if(!file)return 2;
    struct s12_silero *vad=s12_silero_open(argv[1],argv[2]);
    struct neural_stream *s=calloc(1,sizeof(*s));
    if(!vad || !s){fclose(file);s12_silero_close(vad);free(s);return 1;}
    struct neural_decision d;ns_init(s,1,1001,1002);nd_init(&d,1,1001,1002);
    if(!ns_claim(s,1,1001,1003))return 1;
    int16_t pcm[160];struct observed observed={0};int found=0;uint32_t frame=0;
    while(fread(pcm,sizeof(*pcm),160,file)==160){
        uint32_t now=1000+frame*10;
        if(!observe(vad,s,&d,pcm,now,&observed))return 1;
        if(nd_due(s,&d,1,1001,1003,now)){found=1;break;}
        frame++;
    }
    fclose(file);
    if(!found || frame!=765 || !observed.seen)return 1;
    uint32_t now=1000+frame*10;
    if(nd_due(s,&d,1,1001,1003,now+151))return 1; /* stale proposal */
    int16_t zero[160]={0};
    if(!ns_append(s,zero,320) || nd_due(s,&d,1,1001,1003,now))return 1; /* unread PCM */
    ns_close(s,0);if(nd_due(s,&d,1,1001,1003,now))return 1; /* cancellation */

    if(!s12_silero_reset(vad))return 1;
    ns_init(s,2,1001,1002);nd_init(&d,2,1001,1002);
    if(!ns_claim(s,2,1001,1003))return 1;
    observed=(struct observed){0};
    for(frame=0;frame<600;frame++){
        now=20000+frame*10;
        if(!observe(vad,s,&d,zero,now,&observed))return 1;
        int due=nd_no_speech_due(s,&d,2,1001,1003,now);
        if(due!=(frame==599) || observed.seen)return 1;
    }
    if(!ns_append(s,zero,320) || nd_no_speech_due(s,&d,2,1001,1003,now))return 1;
    ns_close(s,0);s12_silero_close(vad);free(s);
    puts("S12_DECISION_TEST_OK speech_end=765 silence=600 stale/unread/cancel=rejected");
    return 0;
}
