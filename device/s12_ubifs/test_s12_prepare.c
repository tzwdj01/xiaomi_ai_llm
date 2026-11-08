/* Standalone process; no native service, microphone, or network access. */
#define main s12_descriptor_probe_main
#include "abi_probe.c"
#undef main
#include "s12_prepare.h"
#include <assert.h>

static const void *expected_passthrough;
static size_t mock_pack(const void *message, void *out) {
    assert(message == expected_passthrough && !out);
    return 7;
}
static size_t grow_size(const void *message) {
    const unsigned char *up, *prep;
    uint32_t mode;
    memcpy(&up, (const unsigned char *)message + 32, sizeof(up));
    memcpy(&prep, up + 40, sizeof(prep));
    memcpy(&mode, prep + 24, sizeof(mode));
    return mode ? 5 : 4;
}

int main(void) {
    assert(sizeof(void *) == 8);
    void *lib = dlopen("/usr/lib/libaivs-message-util.so", RTLD_NOW | RTLD_LOCAL);
    assert(lib);
    const message_desc *top = dlsym(lib, "speech_message__descriptor");
    const message_desc *up = dlsym(lib, "speech_message__upward_message__descriptor");
    const message_desc *prep = dlsym(lib, "speech_message__upward_message__stream_prepare_request_message__descriptor");
    s12_pack_fn pack = dlsym(lib, "speech_message__pack");
    size_t (*size_fn)(const void *) = dlsym(lib, "speech_message__get_packed_size");
    void *(*unpack)(void *, size_t, const void *) = dlsym(lib, "speech_message__unpack");
    void (*free_message)(void *, void *) = dlsym(lib, "speech_message__free_unpacked");
    assert(top && up && prep && pack && size_fn && unpack && free_message);
    assert(top->size == 48 && up->size == 72 && prep->size == 40);
    assert(field(top, "type")->offset == 24 && field(top, "upward")->offset == 32);
    assert(field(up, "type")->offset == 24 && field(up, "stream_prepare_request")->offset == 40);
    assert(field(prep, "activate_mode")->offset == 24);
    unsigned char *t = calloc(1, top->size), *u = calloc(1, up->size), *p = calloc(1, prep->size);
    assert(t && u && p);
    top->init(t); up->init(u); prep->init(p);
    uint32_t zero = 0, one = 1;
    memcpy(t + 24, &zero, 4); memcpy(t + 32, &u, sizeof(u));
    memcpy(u + 24, &one, 4); memcpy(u + 40, &p, sizeof(p));
    memcpy(p + 24, &zero, 4);
    unsigned char orig_t[48], orig_u[72], orig_p[40];
    memcpy(orig_t,t,48); memcpy(orig_u,u,72); memcpy(orig_p,p,40);
    unsigned char output[4096];
    size_t original_size = size_fn(t);
    assert(original_size < sizeof(output));
    int rewritten = 0;
    assert(s12_pack_nonwakeup(t,NULL,mock_pack,grow_size,&rewritten) == 0 && !rewritten);
    assert(s12_pack_nonwakeup(t,NULL,mock_pack,NULL,&rewritten) == 0 && !rewritten);
    size_t n = s12_pack_nonwakeup(t, output, pack, size_fn, &rewritten);
    assert(rewritten && n && n < sizeof(output));
    assert(!memcmp(t,orig_t,48) && !memcmp(u,orig_u,72) && !memcmp(p,orig_p,40));
    unsigned char *decoded = unpack(NULL,n,output), *decoded_up = NULL, *decoded_prepare = NULL;
    assert(decoded);
    memcpy(&decoded_up, decoded + 32, sizeof(decoded_up)); assert(decoded_up);
    memcpy(&decoded_prepare, decoded_up + 40, sizeof(decoded_prepare)); assert(decoded_prepare);
    uint32_t mode = 0;
    memcpy(&mode, decoded_prepare + 24, sizeof(mode)); assert(mode == 1);
    free_message(decoded,NULL);
    expected_passthrough = t;
    uint32_t other = 2;
    memcpy(t + 24, &other, 4);
    assert(s12_pack_nonwakeup(t,NULL,mock_pack,NULL,&rewritten) == 7 && !rewritten);
    memcpy(t + 24, &zero, 4);
    memcpy(u + 24, &other, 4);
    assert(s12_pack_nonwakeup(t,NULL,mock_pack,NULL,&rewritten) == 7 && !rewritten);
    memcpy(u + 24, &one, 4);
    void *null_pointer = NULL;
    memcpy(u + 40, &null_pointer, sizeof(null_pointer));
    assert(s12_pack_nonwakeup(t,NULL,mock_pack,NULL,&rewritten) == 7 && !rewritten);
    free(t); free(u); free(p); dlclose(lib);
    printf("S12_PREPARE_TEST_OK: original=%zu rewritten=%zu; originals untouched; passthrough\n", original_size,n);
    return 0;
}
