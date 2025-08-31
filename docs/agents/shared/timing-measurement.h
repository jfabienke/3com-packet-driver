/* Timing Measurement v1.0 - 3Com Packet Driver Modular Architecture
 * Version: 1.0
 * Date: 2025-08-22
 * 
 * PIT-based timing measurement for DOS real-mode on 286+ CPUs
 * MANDATORY for all agents - use these macros for all timing validation
 */

#ifndef TIMING_MEASUREMENT_H
#define TIMING_MEASUREMENT_H

#include <stdint.h>

/* PIT (Programmable Interval Timer) 8253/8254 Constants */
#define PIT_CONTROL_PORT    0x43
#define PIT_COUNTER0_PORT   0x40
#define PIT_COUNTER1_PORT   0x41
#define PIT_COUNTER2_PORT   0x42

/* PIT Control Word for Counter 0 (system timer) */
#define PIT_CMD_COUNTER0    0x00    /* Select counter 0 */
#define PIT_CMD_LATCH       0x00    /* Latch counter value */
#define PIT_CMD_LOBYTE      0x10    /* Read/write low byte only */
#define PIT_CMD_HIBYTE      0x20    /* Read/write high byte only */
#define PIT_CMD_LOHI        0x30    /* Read/write low byte then high byte */
#define PIT_CMD_MODE0       0x00    /* Mode 0: Interrupt on terminal count */
#define PIT_CMD_MODE2       0x04    /* Mode 2: Rate generator (default) */
#define PIT_CMD_MODE3       0x06    /* Mode 3: Square wave generator */
#define PIT_CMD_BINARY      0x00    /* Binary counting */
#define PIT_CMD_BCD         0x01    /* BCD counting */

/* PIT frequency: 1.193182 MHz (1193182 Hz) */
#define PIT_FREQUENCY       1193182L
#define PIT_US_PER_TICK     ((1000000L + PIT_FREQUENCY/2) / PIT_FREQUENCY)  /* ~0.838 μs */

/* Maximum measurable time with 16-bit counter: ~54.925 ms */
#define PIT_MAX_COUNT       65536L
#define PIT_MAX_US          ((PIT_MAX_COUNT * 1000000L) / PIT_FREQUENCY)

/* Timing measurement structure */
typedef struct {
    uint16_t start_count;       /* PIT counter at start */
    uint16_t end_count;         /* PIT counter at end */
    uint32_t elapsed_us;        /* Calculated elapsed time in microseconds */
    uint8_t  overflow;          /* 1 if timer overflow occurred */
} pit_timing_t;

/* Assembly macros for timing measurement */

/* Initialize PIT for timing measurements (call once at startup) */
#define PIT_INIT() \
    __asm__ __volatile__( \
        "pushf\n\t" \
        "cli\n\t" \
        "mov $0x34, %%al\n\t"       /* Counter 0, lo/hi byte, mode 2, binary */ \
        "out %%al, $0x43\n\t"       /* Send to PIT control port */ \
        "xor %%ax, %%ax\n\t"        /* Count = 0 (65536) for maximum range */ \
        "out %%al, $0x40\n\t"       /* Send low byte */ \
        "out %%al, $0x40\n\t"       /* Send high byte */ \
        "popf\n\t" \
        : : : "ax" \
    )

/* Read current PIT counter value (returns in AX) */
#define PIT_READ_COUNTER(result) \
    __asm__ __volatile__( \
        "pushf\n\t" \
        "cli\n\t" \
        "xor %%al, %%al\n\t"        /* Latch counter 0 */ \
        "out %%al, $0x43\n\t" \
        "in $0x40, %%al\n\t"        /* Read low byte */ \
        "mov %%al, %%ah\n\t" \
        "in $0x40, %%al\n\t"        /* Read high byte */ \
        "xchg %%al, %%ah\n\t"       /* AX = high:low */ \
        "popf\n\t" \
        : "=a" (result) \
        : \
        : "memory" \
    )

/* Start timing measurement */
#define PIT_START_TIMING(timing_ptr) \
    do { \
        (timing_ptr)->overflow = 0; \
        PIT_READ_COUNTER((timing_ptr)->start_count); \
    } while(0)

/* End timing measurement and calculate elapsed time */
#define PIT_END_TIMING(timing_ptr) \
    do { \
        PIT_READ_COUNTER((timing_ptr)->end_count); \
        pit_calculate_elapsed(timing_ptr); \
    } while(0)

/* Calculate elapsed time from PIT counter readings */
static inline void pit_calculate_elapsed(pit_timing_t *timing) {
    uint16_t start = timing->start_count;
    uint16_t end = timing->end_count;
    uint32_t ticks;
    
    /* PIT counts down from 65535 to 0, then wraps */
    if (end <= start) {
        /* Normal case: no overflow */
        ticks = start - end;
    } else {
        /* Overflow occurred: counter wrapped around */
        ticks = (65536L - end) + start;
        timing->overflow = 1;
    }
    
    /* Convert ticks to microseconds */
    timing->elapsed_us = (ticks * 1000000L + PIT_FREQUENCY/2) / PIT_FREQUENCY;
}

/* High-level timing macros for common use cases */

/* Time a critical section with CLI/STI */
#define TIME_CLI_SECTION(timing_ptr, code) \
    do { \
        PIT_START_TIMING(timing_ptr); \
        __asm__ __volatile__("cli"); \
        code; \
        __asm__ __volatile__("sti"); \
        PIT_END_TIMING(timing_ptr); \
    } while(0)

/* Time an ISR execution */
#define TIME_ISR_EXECUTION(timing_ptr, isr_code) \
    do { \
        PIT_START_TIMING(timing_ptr); \
        isr_code; \
        PIT_END_TIMING(timing_ptr); \
    } while(0)

/* Time a function call */
#define TIME_FUNCTION_CALL(timing_ptr, function_call) \
    do { \
        PIT_START_TIMING(timing_ptr); \
        function_call; \
        PIT_END_TIMING(timing_ptr); \
    } while(0)

/* Validation macros for timing constraints */

/* Validate CLI section duration (must be ≤8 microseconds) */
#define VALIDATE_CLI_TIMING(timing_ptr) \
    ((timing_ptr)->elapsed_us <= 8 && !(timing_ptr)->overflow)

/* Validate ISR duration (must be ≤60 microseconds for receive path) */
#define VALIDATE_ISR_TIMING(timing_ptr) \
    ((timing_ptr)->elapsed_us <= 60 && !(timing_ptr)->overflow)

/* Validate module init timing (must be ≤100 milliseconds) */
#define VALIDATE_INIT_TIMING(timing_ptr) \
    ((timing_ptr)->elapsed_us <= 100000L && !(timing_ptr)->overflow)

/* Logging and debugging support */

/* Format timing result for logging */
#define FORMAT_TIMING_RESULT(timing_ptr, buffer, buffer_size) \
    do { \
        if ((timing_ptr)->overflow) { \
            snprintf(buffer, buffer_size, "OVERFLOW (>%luus)", PIT_MAX_US); \
        } else { \
            snprintf(buffer, buffer_size, "%luus", (timing_ptr)->elapsed_us); \
        } \
    } while(0)

/* Timing statistics collection */
typedef struct {
    uint32_t min_us;            /* Minimum recorded time */
    uint32_t max_us;            /* Maximum recorded time */
    uint32_t total_us;          /* Total accumulated time */
    uint32_t count;             /* Number of measurements */
    uint32_t overflow_count;    /* Number of overflow measurements */
} timing_stats_t;

/* Update timing statistics */
static inline void update_timing_stats(timing_stats_t *stats, const pit_timing_t *timing) {
    if (timing->overflow) {
        stats->overflow_count++;
        return;
    }
    
    if (stats->count == 0) {
        stats->min_us = timing->elapsed_us;
        stats->max_us = timing->elapsed_us;
    } else {
        if (timing->elapsed_us < stats->min_us) {
            stats->min_us = timing->elapsed_us;
        }
        if (timing->elapsed_us > stats->max_us) {
            stats->max_us = timing->elapsed_us;
        }
    }
    
    stats->total_us += timing->elapsed_us;
    stats->count++;
}

/* Calculate average timing */
#define AVERAGE_TIMING_US(stats) \
    ((stats)->count > 0 ? (stats)->total_us / (stats)->count : 0)

/* Example usage patterns */

#if 0  /* Example code - not compiled */

/* Example 1: Timing a CLI critical section */
void example_cli_timing(void) {
    pit_timing_t timing;
    
    TIME_CLI_SECTION(&timing, {
        /* Critical code that must complete in <8μs */
        outb(0x20, 0x20);  /* Send EOI to PIC */
    });
    
    if (!VALIDATE_CLI_TIMING(&timing)) {
        /* Error: CLI section took too long */
        log_error("CLI section exceeded 8μs limit: %luμs", timing.elapsed_us);
    }
}

/* Example 2: ISR performance measurement */
void example_isr_timing(void) {
    pit_timing_t timing;
    timing_stats_t isr_stats = {0};
    
    for (int i = 0; i < 1000; i++) {
        TIME_ISR_EXECUTION(&timing, {
            /* Simulate ISR code */
            handle_packet_interrupt();
        });
        
        update_timing_stats(&isr_stats, &timing);
    }
    
    log_info("ISR timing: min=%luμs, max=%luμs, avg=%luμs, overflows=%lu",
             isr_stats.min_us, isr_stats.max_us, 
             AVERAGE_TIMING_US(&isr_stats), isr_stats.overflow_count);
}

/* Example 3: Function call overhead measurement */
void example_function_timing(void) {
    pit_timing_t timing;
    
    TIME_FUNCTION_CALL(&timing, {
        far_module_call();
    });
    
    if (timing.elapsed_us > 25) {
        log_warning("Far call overhead high: %luμs", timing.elapsed_us);
    }
}

#endif /* Example code */

/* Compile-time configuration */
#ifndef TIMING_DISABLED
#define TIMING_ENABLED 1
#else
#define TIMING_ENABLED 0
#endif

/* Conditional compilation macros for release builds */
#if TIMING_ENABLED
#define TIMING_MEASURE(timing_ptr, code) TIME_FUNCTION_CALL(timing_ptr, code)
#define TIMING_VALIDATE_CLI(timing_ptr) VALIDATE_CLI_TIMING(timing_ptr)
#define TIMING_VALIDATE_ISR(timing_ptr) VALIDATE_ISR_TIMING(timing_ptr)
#else
#define TIMING_MEASURE(timing_ptr, code) code
#define TIMING_VALIDATE_CLI(timing_ptr) (1)
#define TIMING_VALIDATE_ISR(timing_ptr) (1)
#endif

#endif /* TIMING_MEASUREMENT_H */