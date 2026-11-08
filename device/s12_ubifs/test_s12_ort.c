/* Standalone AArch64/Silero v5 runtime smoke test. Input is synthetic zeros;
 * it never accesses microphones, native processes, or the network. Supply the
 * official ONNX Runtime 1.17.1 header separately at build time. */
#define _POSIX_C_SOURCE 200809L
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "onnxruntime_c_api.h"

typedef const OrtApiBase *(*get_base_fn)(void);
#define CHECK(expr) do { \
    OrtStatus *status_ = (expr); \
    if (status_) { fprintf(stderr, "ORT: %s\n", api->GetErrorMessage(status_)); \
        api->ReleaseStatus(status_); goto cleanup; } \
} while (0)

int main(int argc, char **argv) {
    if (argc != 3) { fprintf(stderr,"usage: %s ORT_SO MODEL\n",argv[0]); return 2; }
    void *lib = dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);
    if (!lib) { fprintf(stderr,"runtime unavailable: %s\n",dlerror()); return 1; }
    get_base_fn get_base = NULL;
    *(void **)(&get_base) = dlsym(lib,"OrtGetApiBase");
    if (!get_base) { fprintf(stderr,"missing OrtGetApiBase\n"); dlclose(lib); return 1; }
    const OrtApiBase *base = get_base();
    const OrtApi *api = base ? base->GetApi(ORT_API_VERSION) : NULL;
    if (!api) { fprintf(stderr,"incompatible ORT API\n"); dlclose(lib); return 1; }
    OrtEnv *env = NULL; OrtSessionOptions *options = NULL; OrtSession *session = NULL;
    OrtMemoryInfo *memory = NULL; OrtValue *inputs[3] = {NULL,NULL,NULL};
    OrtValue *outputs[2] = {NULL,NULL}; OrtTensorTypeAndShapeInfo *shape = NULL;
    int result = 1; size_t in_count = 0, out_count = 0;
    float samples[576] = {0}, state[256] = {0};
    int64_t rate = 16000, input_shape[2] = {1,576}, state_shape[3] = {2,1,128}, rate_shape[1] = {1};
    const char *input_names[3] = {"input","state","sr"};
    const char *output_names[2] = {"output","stateN"};
    CHECK(api->CreateEnv(ORT_LOGGING_LEVEL_ERROR,"s12-synthetic-test",&env));
    CHECK(api->CreateSessionOptions(&options));
    CHECK(api->SetIntraOpNumThreads(options,1));
    CHECK(api->SetInterOpNumThreads(options,1));
    CHECK(api->SetSessionGraphOptimizationLevel(options,ORT_ENABLE_BASIC));
    CHECK(api->CreateSession(env,argv[2],options,&session));
    CHECK(api->SessionGetInputCount(session,&in_count));
    CHECK(api->SessionGetOutputCount(session,&out_count));
    if (in_count!=3 || out_count!=2) { fprintf(stderr,"unexpected model inputs/outputs\n"); goto cleanup; }
    CHECK(api->CreateCpuMemoryInfo(OrtArenaAllocator,OrtMemTypeDefault,&memory));
    CHECK(api->CreateTensorWithDataAsOrtValue(memory,samples,sizeof(samples),input_shape,2,
                                              ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,&inputs[0]));
    CHECK(api->CreateTensorWithDataAsOrtValue(memory,state,sizeof(state),state_shape,3,
                                              ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,&inputs[1]));
    CHECK(api->CreateTensorWithDataAsOrtValue(memory,&rate,sizeof(rate),rate_shape,1,
                                              ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64,&inputs[2]));
    struct timespec start, end;
    if (clock_gettime(CLOCK_MONOTONIC,&start)) goto cleanup;
    float probability = 0;
    for (unsigned i=0; i<16; i++) {
        CHECK(api->Run(session,NULL,input_names,(const OrtValue *const *)inputs,3,
                       output_names,2,outputs));
        size_t count = 0; void *data = NULL;
        CHECK(api->GetTensorTypeAndShape(outputs[0],&shape));
        CHECK(api->GetTensorShapeElementCount(shape,&count));
        api->ReleaseTensorTypeAndShapeInfo(shape); shape=NULL;
        if (count!=1) { fprintf(stderr,"unexpected probability shape\n"); goto cleanup; }
        CHECK(api->GetTensorMutableData(outputs[0],&data));
        probability=*(float *)data;
        if (!(probability>=0 && probability<=1)) { fprintf(stderr,"invalid probability\n"); goto cleanup; }
        CHECK(api->GetTensorTypeAndShape(outputs[1],&shape));
        CHECK(api->GetTensorShapeElementCount(shape,&count));
        api->ReleaseTensorTypeAndShapeInfo(shape); shape=NULL;
        if (count!=256) { fprintf(stderr,"unexpected state shape\n"); goto cleanup; }
        CHECK(api->GetTensorMutableData(outputs[1],&data));
        memcpy(state,data,sizeof(state));
        api->ReleaseValue(outputs[0]); outputs[0]=NULL;
        api->ReleaseValue(outputs[1]); outputs[1]=NULL;
    }
    if (clock_gettime(CLOCK_MONOTONIC,&end)) goto cleanup;
    long elapsed_ms=(long)((end.tv_sec-start.tv_sec)*1000+(end.tv_nsec-start.tv_nsec)/1000000);
    printf("S12_ORT_TEST_OK version=%s frames=16 elapsed_ms=%ld silence_probability=%.6f\n",
           base->GetVersionString(),elapsed_ms,(double)probability);
    result = 0;
cleanup:
    if (shape) api->ReleaseTensorTypeAndShapeInfo(shape);
    for (unsigned i=0;i<2;i++) if (outputs[i]) api->ReleaseValue(outputs[i]);
    for (unsigned i=0;i<3;i++) if (inputs[i]) api->ReleaseValue(inputs[i]);
    if (memory) api->ReleaseMemoryInfo(memory);
    if (session) api->ReleaseSession(session);
    if (options) api->ReleaseSessionOptions(options);
    if (env) api->ReleaseEnv(env);
    dlclose(lib);
    return result;
}
