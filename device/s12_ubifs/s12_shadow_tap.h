/* S12 PCM shadow tap for isolated tests only; NOT a live native hook.
 * Its mapping and session identity must remain pinned for the callback's
 * lifetime; the owner must not reinitialize or reuse the stream in place.
 * It never performs I/O, allocates memory, or calls a stop function. */
#ifndef S12_SHADOW_TAP_H
#define S12_SHADOW_TAP_H
#ifndef S12_SHADOW_ISOLATED_TEST
#error "Do not load the shadow tap into a native service"
#endif
#include <stdint.h>
#include <string.h>
#include "../endpoint_probe/neural_stream.h"

struct s12_shadow_tap {
    struct neural_stream *stream;
    uint32_t sequence,owner,producer;
    unsigned lock,fault;
};

static inline int s12_shadow_ok(const struct s12_shadow_tap *tap) {
    return tap && !__atomic_load_n(&tap->fault,__ATOMIC_ACQUIRE);
}

/* Observe only a matching, already-claimed session. A stale generation must
 * never invalidate a replacement session. Concurrent packets fail closed. */
static inline int s12_shadow_observe(struct s12_shadow_tap *tap,const void *packet) {
    if(!tap || !tap->stream || !packet)return 0;
    if(!s12_shadow_ok(tap))return -1;
    struct neural_stream *s=tap->stream;
    uint32_t type=0,bytes=0;const void *data=NULL;
    memcpy(&type,packet,sizeof(type));
    if(type!=2)return 0;
    if(!ns_identity(s,tap->sequence,tap->owner) || s->producer!=tap->producer ||
       __atomic_load_n(&s->state,__ATOMIC_ACQUIRE)!=NS_LIVE)return 0;
    if(__atomic_exchange_n(&tap->lock,1u,__ATOMIC_ACQUIRE)){
        __atomic_store_n(&tap->fault,1u,__ATOMIC_RELEASE);return -1;
    }
    int result=0;
    if(ns_identity(s,tap->sequence,tap->owner) && s->producer==tap->producer &&
       __atomic_load_n(&s->state,__ATOMIC_ACQUIRE)==NS_LIVE) {
        memcpy(&bytes,(const unsigned char *)packet+8,sizeof(bytes));
        memcpy(&data,(const unsigned char *)packet+24,sizeof(data));
        uint32_t used=__atomic_load_n(&s->used,__ATOMIC_ACQUIRE);
        if(!data || !bytes || bytes>8192 || bytes%320 || used>NS_BYTES || bytes>NS_BYTES-used){
            ns_close(s,0);result=-1;
        } else if(ns_append(s,data,bytes))result=1;
        else {ns_close(s,0);result=-1;}
    }
    if(!s12_shadow_ok(tap) && ns_identity(s,tap->sequence,tap->owner) && s->producer==tap->producer){
        ns_close(s,0);result=-1;
    }
    __atomic_store_n(&tap->lock,0u,__ATOMIC_RELEASE);
    return result;
}

typedef void (*s12_shadow_native_callback)(void *,const void *);
static inline void s12_shadow_forward(struct s12_shadow_tap *tap,
                                      s12_shadow_native_callback original,
                                      void *context,const void *packet) {
    (void)s12_shadow_observe(tap,packet);
    if(original)original(context,packet);
}
#endif
