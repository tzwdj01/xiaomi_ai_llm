/* Research helper for the verified S12 1.74.18 64-bit protobuf layout.
 * It is not a deployable hook: a production caller must first verify all
 * firmware fingerprints and isolate ownership of the specific dialog. */
#ifndef S12_PREPARE_H
#define S12_PREPARE_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef size_t (*s12_pack_fn)(const void *, void *);
typedef size_t (*s12_size_fn)(const void *);
#define S12_PREPARE_MAX_BYTES 4096u

static inline int s12_is_prepare(const void *message) {
    if (!message) return 0;
    const unsigned char *top=message,*up=NULL,*prepare=NULL;
    uint32_t type;
    memcpy(&type,top+24,sizeof(type)); if (type!=0) return 0;
    memcpy(&up,top+32,sizeof(up)); if (!up) return 0;
    memcpy(&type,up+24,sizeof(type)); if (type!=1) return 0;
    memcpy(&prepare,up+40,sizeof(prepare)); return prepare!=NULL;
}

static inline size_t s12_pack_nonwakeup(const void *message, void *out,
                                        s12_pack_fn pack, s12_size_fn packed_size,
                                        int *rewritten) {
    if (rewritten) *rewritten = 0;
    if (!message || !pack) return 0;
    const unsigned char *top = message, *up = NULL, *prepare = NULL;
    uint32_t type;
    memcpy(&type, top + 24, sizeof(type));
    if (type != 0) return pack(message, out);
    memcpy(&up, top + 32, sizeof(up));
    if (!up) return pack(message, out);
    memcpy(&type, up + 24, sizeof(type));
    if (type != 1) return pack(message, out);
    memcpy(&prepare, up + 40, sizeof(prepare));
    if (!prepare) return pack(message, out);

    union { uint64_t align; unsigned char bytes[48]; } t;
    union { uint64_t align; unsigned char bytes[72]; } u;
    union { uint64_t align; unsigned char bytes[40]; } p;
    memcpy(t.bytes, top, sizeof(t.bytes));
    memcpy(u.bytes, up, sizeof(u.bytes));
    memcpy(p.bytes, prepare, sizeof(p.bytes));
    uint32_t nonwakeup = 1;
    memcpy(p.bytes + 24, &nonwakeup, sizeof(nonwakeup));
    unsigned char *p_copy = p.bytes, *u_copy = u.bytes;
    memcpy(u.bytes + 40, &p_copy, sizeof(p_copy));
    memcpy(t.bytes + 32, &u_copy, sizeof(u_copy));
    /* An interposed pack() does not know its caller's buffer capacity. The
     * caller's original allocation must already fit the original message;
     * refuse the rewrite if it would require even one extra byte. */
    if (!packed_size) return 0;
    size_t old_size = packed_size(message), new_size = packed_size(t.bytes);
    if (!old_size || old_size>S12_PREPARE_MAX_BYTES || !new_size || new_size>old_size) return 0;
    size_t result = pack(t.bytes, out);
    if (rewritten) *rewritten = 1;
    return result;
}
#endif
