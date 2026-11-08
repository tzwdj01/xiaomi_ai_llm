/* Isolated compatibility test: no injection, microphone, cloud, or real text. */
#define CONTROL_DIR "/tmp/s12_json_unit"
#define NATIVE_BUSY_FILE CONTROL_DIR "/busy"
#define FAILURE_ARMED_FILE CONTROL_DIR "/armed"
#include "../native_asr/native_asr.c"
#undef NDEBUG
#include <assert.h>

static void header(json_value *v, const char *ns, const char *name, const char *id, int event) {
    j_ctor(v,7);
    void *h=j_member(v,"header");
    put_string(h,"namespace",ns); put_string(h,"name",name);
    put_string(h,event?"id":"dialog_id",id);
}
static void state_for_test(unsigned phase, const char *dialog) {
    struct control s={.magic=CONTROL_MAGIC,.version=1,.sequence=5,
        .owner=(uint32_t)getpid(),.deadline=now_ms()+20000,.phase=phase};
    snprintf(s.dialog,sizeof(s.dialog),"%s",dialog);
    int fd=open(CONTROL_FILE,O_CREAT|O_TRUNC|O_RDWR|O_NOFOLLOW,0600);
    assert(fd>=0); assert(write(fd,&s,sizeof(s))==sizeof(s)); close(fd);
}
static void test_real_json_reader(void) {
    json_value builder, value;
    void (*ctor)(void *)=dlsym(RTLD_NEXT,"_ZN4Json17CharReaderBuilderC1Ev");
    void (*dtor)(void *)=dlsym(RTLD_NEXT,"_ZN4Json17CharReaderBuilderD1Ev");
    void *(*new_reader)(const void *)=dlsym(RTLD_NEXT,"_ZNK4Json17CharReaderBuilder13newCharReaderEv");
    assert(ctor && dtor && new_reader);
    ctor(&builder); void *reader=new_reader(&builder); assert(reader);
    void **vtable=*(void ***)reader;
    int (*parse)(void *,const char *,const char *,void *,void *)=(void *)vtable[2];
    void (*destroy)(void *)=(void *)vtable[1];
    const char *inputs[]={
        "{\"header\":{\"dialog_id\":\"ordinary-test\",\"namespace\":\"Speaker\",\"name\":\"SetVolume\"}}",
        "{\"header\":{\"dialog_id\":\"owned-test\",\"namespace\":\"Speaker\",\"name\":\"SetVolume\"}}"
    };
    for (unsigned i=0;i<2;i++) {
        j_ctor(&value,0);
        assert(parse(reader,inputs[i],inputs[i]+strlen(inputs[i]),&value,NULL)==(i==0));
        j_dtor(&value);
    }
    destroy(reader); dtor(&builder);
}

int main(void) {
    assert(mkdir(CONTROL_DIR,0700)==0);
    void *sdk=dlopen("/usr/lib/libaivs_sdk.so",RTLD_NOW|RTLD_GLOBAL);
    if (!sdk) { fprintf(stderr,"SDK unavailable: %s\n",dlerror()); return 2; }
    assert(resolve_json()); role=2; json_ready=1;
    json_value event, array;
    header(&event,"SpeechRecognizer","Recognize","test-event",1);
    j_ctor(&array,6); j_swap(j_member(&event,"context"),&array); j_dtor(&array);
    assert(asr_only(&event)); assert(asr_only(&event));
    const void *contexts=member(&event,"context");
    assert(j_size(contexts)==1);
    const void *control=j_index(contexts,0);
    assert(!strcmp(string_member(member(control,"header"),"namespace"),"Execution"));
    assert(!strcmp(string_member(member(control,"header"),"name"),"RequestControl"));
    const void *disabled=member(member(control,"payload"),"disabled");
    assert(j_size(disabled)==2);
    assert(!strcmp(str_value(j_index(disabled,0)),"NLP"));
    assert(!strcmp(str_value(j_index(disabled,1)),"TTS"));
    j_dtor(&event);

    remember("owned-test"); state_for_test(BOUND,"owned-test");
    json_value instruction;
    header(&instruction,"Speaker","SetVolume","owned-test",0);
    assert(receive_json(1,&instruction)==0); j_dtor(&instruction);
    header(&instruction,"SpeechSynthesizer","Speak","owned-test",0);
    assert(receive_json(1,&instruction)==0); j_dtor(&instruction);
    header(&instruction,"Speaker","SetVolume","ordinary-test",0);
    assert(receive_json(1,&instruction)==1); j_dtor(&instruction);
    header(&instruction,"SpeechRecognizer","RecognizeResult","owned-test",0);
    void *payload=j_member(&instruction,"payload");
    void (*bool_ctor)(void *,int)=dlsym(RTLD_NEXT,"_ZN4Json5ValueC1Eb"); assert(bool_ctor);
    json_value yes, result;
    bool_ctor(&yes,1); j_swap(j_member(payload,"is_final"),&yes); j_dtor(&yes);
    j_ctor(&array,6); j_ctor(&result,7); put_string(&result,"text","test text");
    j_append(&array,&result); j_dtor(&result); j_swap(j_member(payload,"results"),&array); j_dtor(&array);
    assert(receive_json(1,&instruction)==1);
    struct control s; int fd=state_open(&s); assert(fd>=0);
    assert(s.final_seen && !strcmp(s.text,"test text")); state_close(fd,NULL);
    state_for_test(BOUND,"other-test");
    assert(receive_json(1,&instruction)==0); j_dtor(&instruction);
    fd=state_open(&s); assert(fd>=0); assert(!s.final_seen && !s.text[0]); state_close(fd,NULL);
    state_for_test(NATIVE_HANDOFF,"owned-test");
    header(&instruction,"SpeechRecognizer","StopCapture","owned-test",0);
    assert(receive_json(1,&instruction)==0); j_dtor(&instruction);
    header(&instruction,"SpeechRecognizer","StopCapture","ordinary-test",0);
    assert(receive_json(1,&instruction)==1); j_dtor(&instruction);
    test_real_json_reader();
    unlink(CONTROL_FILE); unlink(CONTROL_DIR "/events.log");
    assert(rmdir(CONTROL_DIR)==0);
    puts("S12_JSON_TEST_OK: RequestControl, real reader hook, final, stale result, native handoff");
    return 0;
}
