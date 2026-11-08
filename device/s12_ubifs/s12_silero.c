/* Experimental observer, not an endpoint controller.
 * Framing and hysteresis follow sherpa-onnx v1.10.36 Silero v5:
 * https://github.com/k2-fsa/sherpa-onnx/blob/v1.10.36/sherpa-onnx/csrc/silero-vad-model.cc
 * and voice-activity-detector.cc, copyright 2023 Xiaomi Corporation,
 * distributed by that project under the Apache License, Version 2.0.
 * The observer emits only speech/segment status; it does not retain segments. */
#include "s12_silero.h"
#include "onnxruntime_c_api.h"
#include <dlfcn.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define WINDOW 576u
#define SHIFT 512u
#define MIN_SPEECH 1920u
#define MIN_SILENCE 32000u
#define MAX_STREAM_SAMPLES (20u * 16000u)
#define QUEUE_LIMIT 16u
typedef const OrtApiBase *(*base_fn)(void);
struct s12_silero {
    void *library; const OrtApi *api;
    OrtEnv *env; OrtSessionOptions *options; OrtSession *session;
    OrtMemoryInfo *memory; OrtValue *inputs[3];
    float window[WINDOW], state[256];
    int64_t rate, window_shape[2], state_shape[3], rate_shape[1];
    size_t pending;
    uint64_t accepted, current, temp_start, temp_end, head, tail, speech_start;
    unsigned queued;
    int triggered, detected, failed;
};

static int good(struct s12_silero *v, OrtStatus *status) {
    if (!status) return 1;
    v->api->ReleaseStatus(status); v->failed=1; return 0;
}

void s12_silero_close(struct s12_silero *v) {
    if (!v) return;
    const OrtApi *a=v->api;
    if (a) {
        for (unsigned i=0;i<3;i++) if (v->inputs[i]) a->ReleaseValue(v->inputs[i]);
        if (v->memory) a->ReleaseMemoryInfo(v->memory);
        if (v->session) a->ReleaseSession(v->session);
        if (v->options) a->ReleaseSessionOptions(v->options);
        if (v->env) a->ReleaseEnv(v->env);
    }
    if (v->library) dlclose(v->library);
    free(v);
}

struct s12_silero *s12_silero_open(const char *runtime, const char *model) {
    if (!runtime || !model) return NULL;
    struct s12_silero *v=calloc(1,sizeof(*v)); if (!v) return NULL;
    v->library=dlopen(runtime,RTLD_NOW|RTLD_LOCAL);
    base_fn get_base=NULL;
    if (v->library) *(void **)(&get_base)=dlsym(v->library,"OrtGetApiBase");
    const OrtApiBase *base=get_base?get_base():NULL;
    v->api=base?base->GetApi(ORT_API_VERSION):NULL;
    if (!v->api) goto fail;
    const OrtApi *a=v->api;
    v->rate=16000; v->window_shape[0]=1; v->window_shape[1]=WINDOW;
    v->state_shape[0]=2; v->state_shape[1]=1; v->state_shape[2]=128; v->rate_shape[0]=1;
    size_t in=0,out=0;
    if (!good(v,a->CreateEnv(ORT_LOGGING_LEVEL_ERROR,"s12-silero",&v->env)) ||
        !good(v,a->CreateSessionOptions(&v->options)) ||
        !good(v,a->SetIntraOpNumThreads(v->options,1)) ||
        !good(v,a->SetInterOpNumThreads(v->options,1)) ||
        !good(v,a->SetSessionGraphOptimizationLevel(v->options,ORT_ENABLE_BASIC)) ||
        !good(v,a->CreateSession(v->env,model,v->options,&v->session)) ||
        !good(v,a->SessionGetInputCount(v->session,&in)) ||
        !good(v,a->SessionGetOutputCount(v->session,&out)) || in!=3 || out!=2 ||
        !good(v,a->CreateCpuMemoryInfo(OrtArenaAllocator,OrtMemTypeDefault,&v->memory)) ||
        !good(v,a->CreateTensorWithDataAsOrtValue(v->memory,v->window,sizeof(v->window),v->window_shape,2,
                                                  ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,&v->inputs[0])) ||
        !good(v,a->CreateTensorWithDataAsOrtValue(v->memory,v->state,sizeof(v->state),v->state_shape,3,
                                                  ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,&v->inputs[1])) ||
        !good(v,a->CreateTensorWithDataAsOrtValue(v->memory,&v->rate,sizeof(v->rate),v->rate_shape,1,
                                                  ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64,&v->inputs[2]))) goto fail;
    return v;
fail:
    s12_silero_close(v); return NULL;
}

static int infer(struct s12_silero *v, float *probability) {
    const char *names[3]={"input","state","sr"}, *outputs_names[2]={"output","stateN"};
    OrtValue *outputs[2]={NULL,NULL}; OrtTensorTypeAndShapeInfo *shape=NULL;
    const OrtApi *a=v->api; void *data=NULL; size_t count=0; int ok=0;
    if (!good(v,a->Run(v->session,NULL,names,(const OrtValue *const *)v->inputs,3,
                       outputs_names,2,outputs)) ||
        !good(v,a->GetTensorTypeAndShape(outputs[0],&shape)) ||
        !good(v,a->GetTensorShapeElementCount(shape,&count))) goto done;
    a->ReleaseTensorTypeAndShapeInfo(shape); shape=NULL;
    if (count!=1 || !good(v,a->GetTensorMutableData(outputs[0],&data))) goto done;
    *probability=*(float *)data;
    if (!(*probability>=0 && *probability<=1)) goto done;
    if (!good(v,a->GetTensorTypeAndShape(outputs[1],&shape)) ||
        !good(v,a->GetTensorShapeElementCount(shape,&count))) goto done;
    if (count!=256 || !good(v,a->GetTensorMutableData(outputs[1],&data))) goto done;
    memcpy(v->state,data,sizeof(v->state)); ok=1;
done:
    if (shape) a->ReleaseTensorTypeAndShapeInfo(shape);
    for (unsigned i=0;i<2;i++) if (outputs[i]) a->ReleaseValue(outputs[i]);
    if (!ok) v->failed=1;
    return ok;
}

static int classify(struct s12_silero *v, float prob) {
    const float threshold=0.25f;
    v->current+=SHIFT;
    if (prob>threshold && v->temp_end) v->temp_end=0;
    if (prob>threshold && !v->temp_start) {v->temp_start=v->current; return 0;}
    if (prob>threshold && v->temp_start && !v->triggered) {
        if (v->current-v->temp_start<MIN_SPEECH) return 0;
        v->triggered=1; return 1;
    }
    if (prob<threshold && !v->triggered) {v->temp_start=0;v->temp_end=0;return 0;}
    if (prob>threshold-0.15f && v->triggered) return 1;
    if (prob>threshold && !v->triggered) {v->triggered=1;return 1;}
    if (prob<threshold && v->triggered) {
        if (!v->temp_end) v->temp_end=v->current;
        if (v->current-v->temp_end<MIN_SILENCE) return 1;
        v->temp_start=0;v->temp_end=0;v->triggered=0;return 0;
    }
    return 0;
}

int s12_silero_accept(struct s12_silero *v, const float *samples, size_t count) {
    if (!v || v->failed) return 0;
    if ((!samples && count) || count>4096 || count>MAX_STREAM_SAMPLES-v->accepted) {
        v->failed=1;return 0;
    }
    v->accepted+=count;
    int processed=0, speech=0;
    while (count) {
        size_t n=WINDOW-v->pending; if (n>count) n=count;
        memcpy(v->window+v->pending,samples,n*sizeof(float));
        samples+=n;count-=n;v->pending+=n;
        if (v->pending==WINDOW) {
            float p;
            v->tail+=SHIFT;
            if (!infer(v,&p)) return 0;
            speech |= classify(v,p); processed=1;
            memmove(v->window,v->window+SHIFT,(WINDOW-SHIFT)*sizeof(float));
            v->pending=WINDOW-SHIFT;
        }
    }
    if (!processed) return 1;
    if (speech) {
        if (!v->detected) {
            uint64_t before=2*WINDOW+MIN_SPEECH;
            v->speech_start=v->tail>before?v->tail-before:0;
            if (v->speech_start<v->head) v->speech_start=v->head;
        }
        v->detected=1;
    } else {
        if (v->detected && v->tail>v->head) {
            if (v->queued>=QUEUE_LIMIT || v->tail<MIN_SILENCE ||
                v->tail-MIN_SILENCE<v->speech_start) {v->failed=1;return 0;}
            v->queued++;v->head=v->tail-MIN_SILENCE;
        } else if (!v->detected) {
            uint64_t before=2*WINDOW+MIN_SPEECH;
            uint64_t end=v->tail>before?v->tail-before:0;
            if (end>v->head) v->head=end;
        }
        v->detected=0;
    }
    return 1;
}

int s12_silero_detected(const struct s12_silero *v) {return !v || v->failed ? -1 : v->detected;}
int s12_silero_pop_segment(struct s12_silero *v) {
    if (!v || v->failed) return -1;
    if (!v->queued) return 0;
    v->queued--;return 1;
}
int s12_silero_reset(struct s12_silero *v) {
    if (!v || v->failed) return 0;
    memset(v->window,0,sizeof(v->window));memset(v->state,0,sizeof(v->state));
    v->pending=0;v->accepted=0;v->current=0;v->temp_start=0;v->temp_end=0;
    v->head=0;v->tail=0;v->speech_start=0;v->queued=0;v->triggered=0;v->detected=0;
    return 1;
}
