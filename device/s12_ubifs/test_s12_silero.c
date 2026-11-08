/* Standalone detector comparison using a supplied 16 kHz mono S16LE fixture. */
#include "s12_silero.h"
#include <stdint.h>
#include <stdio.h>

struct result {unsigned frames, events;uint32_t hash;};
static int run(FILE *in,struct s12_silero *vad,int emit,struct result *result) {
    unsigned frame=0;int last=0,ok=1;int16_t pcm[160];float data[160];size_t n;
    result->hash=2166136261u;result->events=0;
    while((n=fread(pcm,sizeof(*pcm),160,in))==160){
        for(unsigned j=0;j<160;j++)data[j]=pcm[j]/32768.0f;
        if(!s12_silero_accept(vad,data,160)){ok=0;break;}
        int state=s12_silero_detected(vad);if(state<0){ok=0;break;}
        if(state!=last){
            if(emit)printf("%u %s\n",frame,state?"speech":"quiet");
            result->hash=(result->hash^frame)*16777619u;
            result->hash=(result->hash^(unsigned)(state+1))*16777619u;
            result->events++;last=state;
        }
        int segment;
        while((segment=s12_silero_pop_segment(vad))==1){
            if(emit)printf("%u segment\n",frame);
            result->hash=(result->hash^frame)*16777619u;
            result->hash=(result->hash^3u)*16777619u;result->events++;
        }
        if(segment<0){ok=0;break;}
        frame++;
    }
    if(n || ferror(in))ok=0;
    result->frames=frame;return ok;
}
int main(int argc,char **argv) {
    if (argc!=4) {fprintf(stderr,"usage: %s RUNTIME MODEL PCM\n",argv[0]);return 2;}
    FILE *in=fopen(argv[3],"rb");if(!in)return 2;
    struct s12_silero *vad=s12_silero_open(argv[1],argv[2]);
    if(!vad){fclose(in);fprintf(stderr,"detector unavailable\n");return 1;}
    struct result first={0},second={0};
    int ok=run(in,vad,1,&first);
    if(!s12_silero_reset(vad) || fseek(in,0,SEEK_SET) || !run(in,vad,0,&second))ok=0;
    if(first.frames!=second.frames || first.events!=second.events || first.hash!=second.hash)ok=0;
    if(s12_silero_accept(vad,NULL,1) || s12_silero_detected(vad)!=-1 || s12_silero_reset(vad))ok=0;
    s12_silero_close(vad);
    vad=s12_silero_open(argv[1],argv[2]);float sample=0;
    if(!vad || s12_silero_accept(vad,&sample,4097) || s12_silero_detected(vad)!=-1)ok=0;
    fclose(in);s12_silero_close(vad);
    if(!ok)return 1;
    printf("S12_SILERO_TEST_OK frames=%u events=%u reset_replay=matched invalid_input=rejected\n",
           first.frames,first.events);return 0;
}
