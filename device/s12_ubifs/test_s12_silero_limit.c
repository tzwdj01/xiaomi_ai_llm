/* Limit and fail-closed test, using only 20 seconds of zero-valued PCM. */
#include "s12_silero.h"
#include <stdio.h>
int main(int argc,char **argv) {
    if(argc!=3)return 2;
    struct s12_silero *v=s12_silero_open(argv[1],argv[2]);
    if(!v)return 1;
    float silence[4000]={0};int ok=1;
    for(unsigned i=0;i<80;i++) {
        if(!s12_silero_accept(v,silence,4000) || s12_silero_detected(v)!=0 ||
           s12_silero_pop_segment(v)!=0){ok=0;break;}
    }
    if(s12_silero_accept(v,silence,1) || s12_silero_detected(v)!=-1 ||
       s12_silero_pop_segment(v)!=-1 || s12_silero_reset(v))ok=0;
    s12_silero_close(v);
    if(!ok)return 1;
    puts("S12_SILERO_LIMIT_TEST_OK silent_20s limit=sticky_failure");return 0;
}
