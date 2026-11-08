/* Offline research scaffold, not a deployable probe. It observes only packet
 * type/length counters; no PCM bytes or text are copied or logged. */
#ifndef S12_PCM_PROBE_UNIT_TEST
#error "Compile only in the isolated mock test until the prior panic is understood"
#endif
#include <dlfcn.h>
#include <stdint.h>
#include <string.h>

typedef void (*s12_asr_cb)(void *,const void *);
struct s12_pcm_counts {
    uint32_t registered, packets, audio_packets, audio_bytes;
    uint32_t valid_frames, invalid_frames, last_type, last_bytes;
};
static struct s12_pcm_counts *s12_counts;
static s12_asr_cb s12_asr_original;
#ifndef S12_EXPECTED_ASR_CB
#define S12_EXPECTED_ASR_CB ((uintptr_t)0x41b008)
#endif
#ifndef S12_EXPECTED_WAKE_CB
#define S12_EXPECTED_WAKE_CB ((uintptr_t)0x41afa4)
#endif
#ifndef S12_EXPECTED_VOIP_CB
#define S12_EXPECTED_VOIP_CB ((uintptr_t)0x41b148)
#endif
#ifndef S12_EXPECTED_EVENT_CB
#define S12_EXPECTED_EVENT_CB ((uintptr_t)0x41a82c)
#endif

static void s12_observe_asr(void *ctx,const void *packet) {
    struct s12_pcm_counts *s=s12_counts;
    if (s && packet) {
        uint32_t type,bytes;
        memcpy(&type,(const unsigned char *)packet,sizeof(type));
        memcpy(&bytes,(const unsigned char *)packet+8,sizeof(bytes));
        __atomic_fetch_add(&s->packets,1u,__ATOMIC_RELAXED);
        __atomic_store_n(&s->last_type,type,__ATOMIC_RELAXED);
        __atomic_store_n(&s->last_bytes,bytes,__ATOMIC_RELAXED);
        if (type==2) {
            const void *data=NULL;
            memcpy(&data,(const unsigned char *)packet+24,sizeof(data));
            __atomic_fetch_add(&s->audio_packets,1u,__ATOMIC_RELAXED);
            if (data && bytes && bytes<=8192 && bytes%320==0) {
                __atomic_fetch_add(&s->valid_frames,bytes/320,__ATOMIC_RELAXED);
                __atomic_fetch_add(&s->audio_bytes,bytes,__ATOMIC_RELAXED);
            } else __atomic_fetch_add(&s->invalid_frames,1u,__ATOMIC_RELAXED);
        }
    }
    if(s12_asr_original)s12_asr_original(ctx,packet);
}

void register_vpm_data_upload_callback(s12_asr_cb asr,void *wakeup,void *voip,void *event,void *ctx) {
    void (*fn)(s12_asr_cb,void *,void *,void *,void *)=dlsym(RTLD_NEXT,"register_vpm_data_upload_callback");
    if(!fn)return;
    if(s12_counts && (uintptr_t)asr==S12_EXPECTED_ASR_CB &&
       (uintptr_t)wakeup==S12_EXPECTED_WAKE_CB && (uintptr_t)voip==S12_EXPECTED_VOIP_CB &&
       (uintptr_t)event==S12_EXPECTED_EVENT_CB && ctx) {
        s12_asr_original=asr;
        __atomic_store_n(&s12_counts->registered,1u,__ATOMIC_RELEASE);
        fn(s12_observe_asr,wakeup,voip,event,ctx);return;
    }
    fn(asr,wakeup,voip,event,ctx);
}
