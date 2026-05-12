# Implementing a Circular Buffer in C for Embedded Systems

> *The canonical embedded interview question — and everything they actually want to hear*

---

## Why They're Asking

A circular buffer (ring buffer) is a fixed-size FIFO data structure that wraps around itself. Interviewers use this question because it simultaneously probes:

- Whether you understand memory constraints (no dynamic allocation)
- Whether you think about concurrency (ISR/thread safety)
- Whether you know the tradeoffs between index-based and pointer-based designs
- Whether you actually write C or just think you do

The "correct" answer isn't a single implementation — it's demonstrating that you understand the design space.

---

## The Core Concept

A circular buffer uses a fixed array with two indices: `head` (write position) and `tail` (read position). When either index reaches the end of the array, it wraps back to zero. The buffer is:

- **Empty** when `head == tail`
- **Full** when advancing `head` would equal `tail`

This means a buffer of capacity `N` can only hold `N-1` elements — one slot is sacrificed to disambiguate full from empty. (The alternative is a separate `count` field, discussed below.)

```
  Array: [ _ | A | B | C | _ | _ ]
  Index:   0   1   2   3   4   5
               ^               ^
             tail            head
             (read)          (write)

  Contains: A, B, C  (3 elements)
  Capacity: 5 usable slots (size=6, one wasted)
```

---

## Implementation 1: The Baseline (Single-Threaded)

Start here. Establish correctness before adding complexity.

```c
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define CBUF_SIZE 64  /* Must be a power of 2 for the fast variant */

typedef struct {
    uint8_t  buf[CBUF_SIZE];
    uint32_t head;   /* Next write position */
    uint32_t tail;   /* Next read position  */
} cbuf_t;

static inline void cbuf_init(cbuf_t *cb)
{
    cb->head = 0;
    cb->tail = 0;
}

static inline bool cbuf_is_empty(const cbuf_t *cb)
{
    return cb->head == cb->tail;
}

static inline bool cbuf_is_full(const cbuf_t *cb)
{
    return ((cb->head + 1) % CBUF_SIZE) == cb->tail;
}

static inline uint32_t cbuf_count(const cbuf_t *cb)
{
    return (cb->head - cb->tail + CBUF_SIZE) % CBUF_SIZE;
}

bool cbuf_push(cbuf_t *cb, uint8_t byte)
{
    if (cbuf_is_full(cb)) {
        return false;  /* Drop — caller decides what to do */
    }
    cb->buf[cb->head] = byte;
    cb->head = (cb->head + 1) % CBUF_SIZE;
    return true;
}

bool cbuf_pop(cbuf_t *cb, uint8_t *out)
{
    if (cbuf_is_empty(cb)) {
        return false;
    }
    *out = cb->buf[cb->tail];
    cb->tail = (cb->tail + 1) % CBUF_SIZE;
    return true;
}

/* Peek without consuming */
bool cbuf_peek(const cbuf_t *cb, uint8_t *out)
{
    if (cbuf_is_empty(cb)) {
        return false;
    }
    *out = cb->buf[cb->tail];
    return true;
}
```

**Interview point**: The `% CBUF_SIZE` modulo is correct but costs a division on architectures without hardware dividers (ARM Cortex-M0, AVR). Mention it — and mention the fix (next section).

---

## Implementation 2: Power-of-2 Optimization

If `CBUF_SIZE` is a power of 2, replace every `% CBUF_SIZE` with `& (CBUF_SIZE - 1)`. This is a single AND instruction on any CPU.

```c
#define CBUF_SIZE 64        /* 64 = 2^6 */
#define CBUF_MASK (CBUF_SIZE - 1)  /* 0x3F */

/* Before: */
cb->head = (cb->head + 1) % CBUF_SIZE;

/* After: */
cb->head = (cb->head + 1) & CBUF_MASK;
```

Add a compile-time assertion to enforce the constraint:

```c
_Static_assert((CBUF_SIZE & CBUF_MASK) == 0,
               "CBUF_SIZE must be a power of 2");
```

This costs nothing at runtime and catches misconfiguration at compile time. Always do this.

---

## Implementation 3: ISR-Safe (Single Producer, Single Consumer)

This is the real embedded question. Your UART RX ISR writes to the buffer; your main loop reads from it. They run in different execution contexts with no RTOS.

**The key insight**: In a single-producer/single-consumer scenario on a single-core MCU, you only need memory barriers — not disabling interrupts — *if* you structure the code correctly.

The rule is:
- **Writer (ISR)** only modifies `head` — reads `tail` as a snapshot
- **Reader (main)** only modifies `tail` — reads `head` as a snapshot

Each side only writes one variable. Since writes to `uint32_t` are atomic on 32-bit ARMs (and often on 8/16-bit with careful sizing), this is safe *if* the compiler doesn't reorder the operations.

```c
#include <stdatomic.h>  /* C11 — or use __sync builtins for GCC <C11 */

typedef struct {
    uint8_t          buf[CBUF_SIZE];
    volatile uint32_t head;  /* Written by ISR only  */
    volatile uint32_t tail;  /* Written by main only */
} cbuf_isr_t;

/* Called from ISR context */
bool cbuf_isr_push(cbuf_isr_t *cb, uint8_t byte)
{
    uint32_t next = (cb->head + 1) & CBUF_MASK;
    if (next == cb->tail) {     /* Full — snapshot tail, no write */
        return false;
    }
    cb->buf[cb->head] = byte;
    /* Compiler barrier: buf write must complete before head advances */
    __asm__ volatile ("" ::: "memory");
    cb->head = next;
    return true;
}

/* Called from main/task context */
bool cbuf_isr_pop(cbuf_isr_t *cb, uint8_t *out)
{
    if (cb->tail == cb->head) {  /* Snapshot head */
        return false;
    }
    *out = cb->buf[cb->tail];
    /* Compiler barrier: buf read must complete before tail advances */
    __asm__ volatile ("" ::: "memory");
    cb->tail = (cb->tail + 1) & CBUF_MASK;
    return true;
}
```

**Why `volatile` isn't enough by itself**: `volatile` prevents the compiler from caching the variable in a register but does *not* prevent instruction reordering. The `__asm__ volatile ("" ::: "memory")` compiler barrier prevents the compiler from reordering memory accesses across that point. On strongly-ordered architectures (x86, Cortex-M), this is sufficient. On weakly-ordered architectures (Cortex-A, RISC-V), you also need hardware memory barriers (`dmb`, `fence`).

**When to disable interrupts instead**: If you need multi-producer or multi-consumer access, or you're on an 8-bit MCU where a 16/32-bit index isn't written atomically, disable interrupts around the critical section:

```c
bool cbuf_push_safe(cbuf_t *cb, uint8_t byte)
{
    bool result;
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    result = cbuf_push(cb, byte);   /* From Implementation 1 */
    if (!primask) __enable_irq();   /* Restore, don't unconditionally enable */
    return result;
}
```

**Always restore, never unconditionally re-enable.** If you're called from inside a critical section, you must not enable interrupts on exit.

---

## Implementation 4: Overwrite-on-Full (Streaming Data)

For streaming sensor data or logging where losing the oldest sample is better than dropping the newest:

```c
void cbuf_push_overwrite(cbuf_t *cb, uint8_t byte)
{
    cb->buf[cb->head] = byte;
    cb->head = (cb->head + 1) & CBUF_MASK;
    if (cb->head == cb->tail) {
        /* Buffer was full — advance tail to discard oldest */
        cb->tail = (cb->tail + 1) & CBUF_MASK;
    }
}
```

Note: this variant is **not** ISR-safe without protection, because it writes both `head` and potentially `tail`.

---

## Implementation 5: Block Transfer (DMA-Friendly)

UART DMA, SPI DMA, and ADC DMA operate on contiguous memory regions. You need to expose the buffer internals without copying.

```c
typedef struct {
    uint8_t *ptr;
    uint32_t len;
} cbuf_region_t;

/*
 * Returns up to two contiguous regions covering all unread data.
 * region[1].len == 0 if data doesn't wrap.
 *
 *  Case 1 (no wrap):  [ _ | A | B | C | _ ]
 *                           ^           ^
 *                          tail        head
 *  region[0] = {&buf[tail], 3},  region[1] = {NULL, 0}
 *
 *  Case 2 (wrapped):  [ C | D | _ | A | B ]
 *                               ^   ^
 *                              head tail
 *  region[0] = {&buf[tail], 2},  region[1] = {&buf[0], 2}
 */
void cbuf_read_regions(cbuf_t *cb, cbuf_region_t regions[2])
{
    uint32_t h = cb->head;
    uint32_t t = cb->tail;

    if (h >= t) {
        regions[0].ptr = &cb->buf[t];
        regions[0].len = h - t;
        regions[1].ptr = NULL;
        regions[1].len = 0;
    } else {
        regions[0].ptr = &cb->buf[t];
        regions[0].len = CBUF_SIZE - t;
        regions[1].ptr = &cb->buf[0];
        regions[1].len = h;
    }
}

/* Call after DMA read completes to advance tail */
void cbuf_consume(cbuf_t *cb, uint32_t n)
{
    cb->tail = (cb->tail + n) & CBUF_MASK;
}

/* Returns contiguous write region for DMA RX */
void cbuf_write_region(cbuf_t *cb, cbuf_region_t *region)
{
    uint32_t h = cb->head;
    uint32_t t = cb->tail;
    uint32_t free_contiguous;

    if (h >= t) {
        /* Free space wraps: head to end, then 0 to tail-1 */
        /* Return only the contiguous tail segment for simplicity */
        free_contiguous = (t == 0) ? (CBUF_SIZE - h - 1) : (CBUF_SIZE - h);
    } else {
        free_contiguous = t - h - 1;
    }

    region->ptr = &cb->buf[h];
    region->len = free_contiguous;
}

void cbuf_produce(cbuf_t *cb, uint32_t n)
{
    cb->head = (cb->head + n) & CBUF_MASK;
}
```

---

## The `count` Field Alternative

Instead of wasting one slot to distinguish full from empty, maintain an explicit count:

```c
typedef struct {
    uint8_t  buf[CBUF_SIZE];
    uint32_t head;
    uint32_t tail;
    uint32_t count;
} cbuf_counted_t;

bool cbuf_push_c(cbuf_counted_t *cb, uint8_t byte)
{
    if (cb->count == CBUF_SIZE) return false;
    cb->buf[cb->head] = byte;
    cb->head = (cb->head + 1) & CBUF_MASK;
    cb->count++;
    return true;
}
```

**Tradeoff**: Full utilization of the array at the cost of a third field that must be kept consistent. In ISR contexts this creates a 3-variable invariant that's harder to keep race-free than the 2-variable one. For single-threaded code, perfectly fine.

---

## Design Decisions Summary

| Decision | Option A | Option B | When to pick B |
|---|---|---|---|
| Modulo | `% N` | `& (N-1)` | Always, if you can enforce power-of-2 |
| Full detection | Wasted slot | Count field | Need 100% utilization; single-threaded |
| ISR safety | Disable interrupts | Lockless (SPSC) | Single producer, single consumer, 32-bit MCU |
| Full behavior | Return false | Overwrite oldest | Streaming/logging; can't afford backpressure |
| Transfer | Byte-at-a-time | Block region API | DMA, need zero-copy |

---

## What a Strong Candidate Says Out Loud

1. **"My first question is: what are the concurrency requirements?"** — ISR? RTOS tasks? Multiple producers? This drives the entire design.

2. **"I'd make the size a power of 2 and use a bitmask instead of modulo"** — shows awareness of the hardware cost of division.

3. **"On this MCU, `volatile` prevents register caching but not instruction reordering — I need a compiler barrier too"** — separates people who've debugged ISR races from people who've just read about them.

4. **"If this is feeding a DMA engine, I need to expose contiguous regions, not a byte API"** — shows systems-level thinking.

5. **"I'd add `_Static_assert` to enforce the power-of-2 constraint at compile time"** — defensive programming instinct.

---

## Common Bugs to Name-Drop

- **Forgetting the compiler barrier**: buffer corruption that only appears at `-O2` or higher, because the compiler legally reorders `buf[head] = byte` and `head = next`.
- **Unconditionally re-enabling interrupts**: nested critical sections accidentally re-enable before the outer section is done.
- **Off-by-one on full detection**: `head == tail` means empty; `(head+1) % N == tail` means full. Getting these backwards produces a buffer that silently drops every Nth byte.
- **Non-atomic index on 8-bit MCU**: a 16-bit index updated as two 8-bit writes can be read mid-update by an ISR, producing a garbage index value.
- **Using `sizeof(buf)` instead of the size constant**: if buf is a pointer (passed to a function), `sizeof` gives you pointer width, not array length.

---

## Complete, Deployable Header

```c
/* cbuf.h — Circular buffer for embedded C, SPSC ISR-safe
 * Size must be a power of 2. Adjust CBUF_SIZE as needed.
 */
#ifndef CBUF_H
#define CBUF_H

#include <stdint.h>
#include <stdbool.h>

#define CBUF_SIZE 64
#define CBUF_MASK (CBUF_SIZE - 1u)

_Static_assert((CBUF_SIZE & CBUF_MASK) == 0,
               "CBUF_SIZE must be a power of 2");

typedef struct {
    uint8_t           buf[CBUF_SIZE];
    volatile uint32_t head;
    volatile uint32_t tail;
} cbuf_t;

#define CBUF_COMPILER_BARRIER() __asm__ volatile ("" ::: "memory")

static inline void cbuf_init(cbuf_t *cb)
    { cb->head = cb->tail = 0; }

static inline bool cbuf_is_empty(const cbuf_t *cb)
    { return cb->head == cb->tail; }

static inline bool cbuf_is_full(const cbuf_t *cb)
    { return ((cb->head + 1u) & CBUF_MASK) == cb->tail; }

static inline uint32_t cbuf_count(const cbuf_t *cb)
    { return (cb->head - cb->tail) & CBUF_MASK; }

/* Safe to call from ISR (single producer) */
static inline bool cbuf_push(cbuf_t *cb, uint8_t b)
{
    uint32_t next = (cb->head + 1u) & CBUF_MASK;
    if (next == cb->tail) return false;
    cb->buf[cb->head] = b;
    CBUF_COMPILER_BARRIER();
    cb->head = next;
    return true;
}

/* Safe to call from main/task (single consumer) */
static inline bool cbuf_pop(cbuf_t *cb, uint8_t *out)
{
    if (cb->tail == cb->head) return false;
    *out = cb->buf[cb->tail];
    CBUF_COMPILER_BARRIER();
    cb->tail = (cb->tail + 1u) & CBUF_MASK;
    return true;
}

static inline bool cbuf_peek(const cbuf_t *cb, uint8_t *out)
{
    if (cb->tail == cb->head) return false;
    *out = cb->buf[cb->tail];
    return true;
}

#endif /* CBUF_H */
```

---

*This implementation handles the 95% case in embedded firmware: UART RX ISR writing, main loop reading, no RTOS. Scale the design up (add mutexes, make size runtime-configurable, add a DMA region API) as your requirements warrant.*