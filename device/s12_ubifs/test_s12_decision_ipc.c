/* Two-process simulated producer/observer. No device audio or native daemon. */
#define _DEFAULT_SOURCE
#include "s12_silero.h"
#include "../endpoint_probe/neural_decision.h"
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static uint32_t monotonic_ms(void) {
    struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return 0;
    return (uint32_t)((uint64_t)t.tv_sec*1000+(uint64_t)t.tv_nsec/1000000);
}
static int observer(struct neural_stream *s,struct neural_decision *d,const char *runtime,
                    const char *model,int ready,int go,pid_t parent) {
    char ch;if(read(go,&ch,1)!=1)return 1;
    struct s12_silero *vad=s12_silero_open(runtime,model);if(!vad)return 1;
    if(write(ready,"R",1)!=1){s12_silero_close(vad);return 1;}
    uint32_t used=0,candidate=0,deadline=monotonic_ms()+25000;int seen=0,ok=1;
    while((int32_t)(deadline-monotonic_ms())>0 && getppid()==parent){
        uint32_t available=__atomic_load_n(&s->used,__ATOMIC_ACQUIRE);
        uint32_t state=__atomic_load_n(&s->state,__ATOMIC_ACQUIRE);
        if(state==NS_INVALID){ok=0;break;}
        if(available<used || available>NS_BYTES || available%320 || available-used>50*320){ok=0;break;}
        if(available==used){
            if(state==NS_DONE)break;
            if(state!=NS_LIVE){ok=0;break;}
            usleep(1000);continue;
        }
        int16_t pcm[160];float samples[160];memcpy(pcm,s->pcm+used,320);
        for(unsigned i=0;i<160;i++)samples[i]=pcm[i]/32768.0f;
        if(!s12_silero_accept(vad,samples,160)){ok=0;break;}
        used+=320;int speech=s12_silero_detected(vad);if(speech<0){ok=0;break;}
        if(speech){seen=1;candidate=0;}
        int segment;while((segment=s12_silero_pop_segment(vad))==1){seen=1;candidate=used/320;}
        if(segment<0){ok=0;break;}
        uint32_t proposal=!seen && used/320>=ND_START_FRAMES?ND_NO_SPEECH:candidate;
        nd_publish(d,(struct neural_proposal){s->producer,used/320,proposal,monotonic_ms(),0});
    }
    s12_silero_close(vad);return ok?0:1;
}

int main(int argc,char **argv) {
    if(argc!=4)return 2;
    FILE *file=fopen(argv[3],"rb");if(!file)return 2;
    struct neural_stream *s=mmap(NULL,sizeof(*s),PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANONYMOUS,-1,0);
    struct neural_decision *d=mmap(NULL,sizeof(*d),PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANONYMOUS,-1,0);
    int start[2],ready[2];
    if(s==MAP_FAILED || d==MAP_FAILED || pipe(start) || pipe(ready))return 1;
    pid_t parent=getpid(),child=fork();if(child<0)return 1;
    if(!child){
        close(start[1]);close(ready[0]);
        int rc=observer(s,d,argv[1],argv[2],ready[1],start[0],parent);
        close(start[0]);close(ready[1]);_exit(rc);
    }
    close(start[0]);close(ready[1]);
    ns_init(s,1,(uint32_t)parent,(uint32_t)child);nd_init(d,1,(uint32_t)parent,(uint32_t)child);
    int ok=ns_claim(s,1,(uint32_t)parent,(uint32_t)parent),found=0;char ch;
    if(!ok || write(start[1],"G",1)!=1 || read(ready[0],&ch,1)!=1 || ch!='R')ok=0;
    close(start[1]);close(ready[0]);
    uint32_t frame=0;int16_t pcm[160];
    while(ok && frame<1200 && fread(pcm,sizeof(*pcm),160,file)==160){
        if(!ns_append(s,pcm,320)){ok=0;break;}
        usleep(10000);
        if(nd_due(s,d,1,(uint32_t)parent,(uint32_t)parent,monotonic_ms())){found=1;break;}
        frame++;
    }
    fclose(file);
    if(!found || frame<765 || frame>795)ok=0;
    ns_close(s,ok);if(!ok)kill(child,SIGTERM);
    int status=0;if(waitpid(child,&status,0)!=child || !WIFEXITED(status) || WEXITSTATUS(status))ok=0;
    if(ok){
        int start2[2],ready2[2];
        if(pipe(start2) || pipe(ready2))ok=0;
        else {
            pid_t child2=fork();
            if(child2==0){
                close(start2[1]);close(ready2[0]);
                int rc=observer(s,d,argv[1],argv[2],ready2[1],start2[0],parent);
                close(start2[0]);close(ready2[1]);_exit(rc);
            }
            if(child2<0)ok=0;
            else {
                close(start2[0]);close(ready2[1]);
                ns_init(s,2,(uint32_t)parent,(uint32_t)child2);nd_init(d,2,(uint32_t)parent,(uint32_t)child2);
                if(!ns_claim(s,2,(uint32_t)parent,(uint32_t)parent) ||
                   write(start2[1],"G",1)!=1 || read(ready2[0],&ch,1)!=1 || ch!='R')ok=0;
                close(start2[1]);close(ready2[0]);
                int16_t silence[160]={0};
                if(ok && !ns_append(s,silence,320))ok=0;
                usleep(50000);ns_close(s,0);
                if(nd_due(s,d,2,(uint32_t)parent,(uint32_t)parent,monotonic_ms()))ok=0;
                if(!ok)kill(child2,SIGTERM);
                status=0;
                if(waitpid(child2,&status,0)!=child2 || !WIFEXITED(status) || WEXITSTATUS(status)!=1)ok=0;
            }
        }
    }
    munmap(s,sizeof(*s));munmap(d,sizeof(*d));
    if(!ok)return 1;
    printf("S12_DECISION_IPC_TEST_OK speech_end_frame=%u separate_processes=2 cancellation=rejected\n",frame);return 0;
}
