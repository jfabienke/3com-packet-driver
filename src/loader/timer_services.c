/**
 * @file timer_services.c
 * @brief DOS Timer Services Implementation
 * 
 * Provides millisecond precision timestamps and timer functions
 * for the packet driver using DOS-compatible timing mechanisms.
 */

#include <i86.h>
#include <dos.h>
#include <stdint.h>

/* PIT and BDA definitions */
#define PIT_CTRL 0x43
#define PIT_CNT0 0x40

static uint32_t read_bda_ticks(void)
{
    uint32_t t;
    uint16_t lo, hi;
    uint32_t __far *ticks = (uint32_t __far *)MK_FP(0x0040, 0x006C);

    _disable();
    /* BDA tick is a dword; read low/high with ints disabled */
    lo = *(uint16_t __far *)ticks;
    hi = *(((uint16_t __far *)ticks) + 1);
    _enable();

    t = ((uint32_t)hi << 16) | lo;
    return t;
}

/**
 * @brief Get millisecond timestamp using BIOS tick + PIT fraction
 * 
 * Provides monotonic millisecond timestamps suitable for timeouts
 * and performance measurements in DOS environment.
 * 
 * @return Current timestamp in milliseconds
 */
uint32_t get_millisecond_timestamp(void)
{
    uint32_t t1, t2;
    uint16_t cnt;
    uint8_t l, h;

    while (1) {
        _disable();
        t1 = read_bda_ticks();

        outp(PIT_CTRL, 0x00);         /* latch counter 0 */
        l = inp(PIT_CNT0);
        h = inp(PIT_CNT0);
        cnt = ((uint16_t)h << 8) | l;

        t2 = read_bda_ticks();
        _enable();

        if (t2 == t1) break;          /* consistent sample */
        /* else, IRQ0 occurred between reads; try again */
    }

    /* PIT is counting down from 65536 at 1.193182 MHz, tick at ~18.2065 Hz
       Approx ms = ticks*55 + (fraction_of_tick)*55, where fraction = (65535 - cnt)/65536 */
    /* Compute fractional milliseconds with 16x16->32 math: ( (65535 - cnt) * 55 ) >> 16 */
    uint16_t frac_counts = (uint16_t)(65535u - cnt);
    uint32_t frac_ms = ((uint32_t)frac_counts * 55u) >> 16;

    return t1 * 55u + (uint16_t)frac_ms;
}

/**
 * @brief Delay execution for specified milliseconds
 * 
 * @param delay_ms Milliseconds to delay
 */
void delay_milliseconds(uint32_t delay_ms)
{
    uint32_t start_time = get_millisecond_timestamp();
    uint32_t target_time = start_time + delay_ms;
    
    /* Handle potential wraparound */
    if (target_time < start_time) {
        /* Wait for wraparound */
        while (get_millisecond_timestamp() >= start_time) {
            /* Allow other tasks to run */
            __asm { int 28h };  /* DOS idle interrupt */
        }
        /* Now wait normally */
        while (get_millisecond_timestamp() < (target_time & 0xFFFFFFFF)) {
            __asm { int 28h };
        }
    } else {
        /* Normal case */
        while (get_millisecond_timestamp() < target_time) {
            __asm { int 28h };
        }
    }
}

/**
 * @brief Get high precision timestamp in microseconds
 * 
 * @return Current timestamp in microseconds (approximate)
 */
uint32_t get_microsecond_timestamp(void)
{
    uint32_t ms = get_millisecond_timestamp();
    
    /* Add sub-millisecond precision by reading PIT directly */
    _disable();
    outp(PIT_CTRL, 0x00);         /* latch counter 0 */
    uint8_t l = inp(PIT_CNT0);
    uint8_t h = inp(PIT_CNT0);
    _enable();
    
    uint16_t cnt = ((uint16_t)h << 8) | l;
    uint16_t frac_counts = (uint16_t)(65535u - cnt);
    
    /* Convert to microseconds: (frac_counts * 54925) >> 16 ≈ frac_counts * 838 µs */
    uint32_t frac_us = ((uint32_t)frac_counts * 838u) >> 10;
    
    return (ms * 1000u) + frac_us;
}

/**
 * @brief Check if specified timeout has elapsed
 * 
 * @param start_time Starting timestamp
 * @param timeout_ms Timeout in milliseconds
 * @return 1 if timeout elapsed, 0 otherwise
 */
int is_timeout_elapsed(uint32_t start_time, uint32_t timeout_ms)
{
    uint32_t current_time = get_millisecond_timestamp();
    uint32_t elapsed;
    
    /* Handle wraparound */
    if (current_time >= start_time) {
        elapsed = current_time - start_time;
    } else {
        /* Wraparound occurred */
        elapsed = (0xFFFFFFFF - start_time) + current_time + 1;
    }
    
    return (elapsed >= timeout_ms) ? 1 : 0;
}