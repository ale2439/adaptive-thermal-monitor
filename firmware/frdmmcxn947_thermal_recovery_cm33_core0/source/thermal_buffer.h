#ifndef THERMAL_BUFFER_H
#define THERMAL_BUFFER_H
#include <stdbool.h>
#include <stdint.h>
#define THERMAL_BUFFER_CAPACITY 300U
typedef struct {
    uint32_t seq, sampled_ms;
    int32_t temperature_mc;
    bool alert;
} thermal_sample_t;
typedef struct {
    thermal_sample_t samples[THERMAL_BUFFER_CAPACITY];
    uint32_t head, count, dropped;
} thermal_buffer_t;
/* Owned exclusively by app_task; callbacks only post events. */
static inline void ThermalBuffer_Push(thermal_buffer_t *b, thermal_sample_t s)
{
    if (b->count == THERMAL_BUFFER_CAPACITY) {
        b->head = (b->head + 1U) % THERMAL_BUFFER_CAPACITY;
        b->count--;
        b->dropped++;
    }
    b->samples[(b->head + b->count) % THERMAL_BUFFER_CAPACITY] = s;
    b->count++;
}
static inline void ThermalBuffer_Acknowledge(thermal_buffer_t *b, uint32_t seq)
{
    /* Normal adaptive reports cover suppressed samples through this sequence.
       Recovery always sends the oldest entry, so this removes one at a time. */
    while (b->count && b->samples[b->head].seq <= seq) {
        b->head = (b->head + 1U) % THERMAL_BUFFER_CAPACITY;
        b->count--;
    }
}
#endif
