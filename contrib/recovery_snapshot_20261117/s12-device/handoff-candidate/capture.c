#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <math.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
static volatile sig_atomic_t interrupted=0;
static void stop_capture(int sig){(void)sig;interrupted=1;}
#define D "/tmp/s12-pcm-probe/"
static uint64_t nowms(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000+t.tv_nsec/1000000;}
static void nap(void){struct timespec t={0,30000000};nanosleep(&t,0);}
static void p16(FILE*f,unsigned v){fputc(v&255,f);fputc((v>>8)&255,f);}
static void p32(FILE*f,unsigned v){p16(f,v);p16(f,v>>16);}
int main(int argc,char**argv){
 if(argc!=3)return 2; const char*out=argv[1];if(strcmp(out,"/tmp/voice.wav"))return 2; char *end=NULL; long timeout=strtol(argv[2],&end,10);if(!end||*end||timeout<5||timeout>10)return 2;
 if(access(D "active",F_OK))return 1; signal(SIGTERM,stop_capture);signal(SIGINT,stop_capture);
 unlink(D "capture.raw");unlink(D "format.txt");unlink(D "record");
 int mark=open(D "record",O_CREAT|O_EXCL|O_WRONLY,0600);if(mark<0)return 1;close(mark);
 uint64_t start=nowms(),last_voice=0;int fd=-1,started=0,burst=0,handoff=0;off_t offset=0;double dc=0,noise=0;int calibrated=0,dc_initialized=0;
 size_t cap=16000*23,count=0;unsigned decim=0;int16_t *samples=calloc(cap,sizeof(*samples));if(!samples){unlink(D "record");return 1;}
 int32_t block[480*7];
 while(!interrupted && nowms()-start<22000){
  FILE *wake=fopen("/tmp/native_first_events.log.wuw","r");
  if(wake){unsigned long long stamp=0;int ok=fscanf(wake,"%llu",&stamp);fclose(wake);if(ok==1 && stamp>=start && stamp<=nowms()+1000){handoff=1;break;}}
  if(!started && nowms()-start >= (uint64_t)timeout*1000)break;
  if(fd<0){fd=open(D "capture.raw",O_RDONLY);if(fd<0){nap();continue;}}
  ssize_t bytes=pread(fd,block,sizeof(block),offset);if(bytes<28){nap();continue;}
  int frames=(int)(bytes/28);offset+=(off_t)frames*28;double energy=0;
  for(int i=0;i<frames;i++) {double v=block[i*7]/65536.0;if(!dc_initialized){dc=v;dc_initialized=1;}dc+=0.001*(v-dc);energy+=fabs(v-dc);if((decim++%3)==0&&count<cap){double x=(v-dc)*40;if(x>32767)x=32767;if(x< -32768)x=-32768;samples[count++]=(int16_t)x;}}
  energy/=frames;uint64_t elapsed=nowms()-start;
  if(elapsed<350){noise+=energy;calibrated++;continue;}
  double baseline=calibrated?noise/calibrated:0;double threshold=baseline*3.0+6;if(threshold<12)threshold=12;if(threshold>1200)threshold=1200;
  if(energy>threshold){burst++;if(burst>=3){started=1;last_voice=nowms();}}else burst=0;
  if(!started&&elapsed>=(uint64_t)timeout*1000)break;
  if(started&&nowms()-last_voice>=1400)break;
 }
 unlink(D "record");if(fd>=0)close(fd); nap(); unlink(D "capture.raw");
 if(interrupted){free(samples);return 130;}
 if(handoff){free(samples);return 125;}
 if(!started||count<4000){free(samples);return 124;}
 FILE *f=fopen(out,"wb");if(!f){free(samples);return 1;}fwrite("RIFF",1,4,f);p32(f,36+(unsigned)count*2);fwrite("WAVEfmt ",1,8,f);p32(f,16);p16(f,1);p16(f,1);p32(f,16000);p32(f,32000);p16(f,2);p16(f,16);fwrite("data",1,4,f);p32(f,(unsigned)count*2);for(size_t i=0;i<count;i++)p16(f,(uint16_t)samples[i]);int err=fclose(f);free(samples);return err?1:0;
}
