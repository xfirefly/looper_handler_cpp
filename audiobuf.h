#ifndef SC_AUDIOBUF_H
#define SC_AUDIOBUF_H

#include <assert.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined (__cplusplus)
extern "C" {
#endif

/**
 * Wrapper around bytebuf to read and write samples
 *
 * Each sample takes sample_size bytes.
 */
struct audiobuf {
    uint8_t *data;
    uint32_t alloc_size; // in samples
    size_t sample_size;

    atomic_uint_least32_t head; // writer cursor, in samples
    atomic_uint_least32_t tail; // reader cursor, in samples
    // empty: tail == head
    // full: ((tail + 1) % alloc_size) == head
};

static inline uint32_t
audiobuf_to_samples(struct audiobuf *buf, size_t bytes) {
    assert(bytes % buf->sample_size == 0);
    return bytes / buf->sample_size;
}

static inline size_t
audiobuf_to_bytes(struct audiobuf *buf, uint32_t samples) {
    return samples * buf->sample_size;
}

bool
audiobuf_init(struct audiobuf *buf, size_t sample_size,
                 uint32_t capacity);

void
audiobuf_destroy(struct audiobuf *buf);

uint32_t
audiobuf_read(struct audiobuf *buf, void *to, uint32_t samples_count);

uint32_t
audiobuf_write(struct audiobuf *buf, const void *from,
                  uint32_t samples_count);

uint32_t
audiobuf_write_silence(struct audiobuf *buf, uint32_t samples);

static inline uint32_t
audiobuf_capacity(struct audiobuf *buf) {
    assert(buf->alloc_size);
    return buf->alloc_size - 1;
}

static inline uint32_t
audiobuf_can_read(struct audiobuf *buf) {
    uint32_t head = atomic_load_explicit(&buf->head, memory_order_acquire);
    uint32_t tail = atomic_load_explicit(&buf->tail, memory_order_acquire);
    return (buf->alloc_size + head - tail) % buf->alloc_size;
}

#if defined (__cplusplus)
}
#endif
#endif
