/**
 * @file timestamp.c
 * @brief Central timestamp functions using DOS BIOS timer
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This file is part of the 3Com Packet Driver project.
 *
 * Provides consistent timing functions across all modules using
 * DOS INT 1Ah (Get System Time) BIOS interrupt.
 */

#include <dos.h>
#include <stdint.h>
#include "../include/common.h"

/* DOS BIOS timer constants */
#define TIMER_TICKS_PER_SECOND  18.2    /* BIOS timer frequency */
#define MS_PER_TICK             54.925  /* Milliseconds per timer tick (1000/18.2) */
#define TICKS_PER_DAY           0x1800B0L /* Timer ticks in 24 hours (resets at midnight) */

/**
 * @brief Get BIOS timer ticks since midnight
 * @return Number of timer ticks since midnight (18.2 Hz frequency)
 * 
 * Uses DOS INT 1Ah, AH=0 to get the current timer tick count.
 * The timer resets to 0 at midnight and counts up to 0x1800B0 ticks per day.
 */
uint32_t get_system_timestamp_ticks(void) {
    union REGS regs;
    uint32_t ticks;
    
    /* INT 1Ah, AH=0: Get system timer ticks */
    regs.h.ah = 0x00;
    int86(0x1A, &regs, &regs);
    
    /* Combine CX:DX into 32-bit tick count */
    ticks = ((uint32_t)regs.x.cx << 16) | regs.x.dx;
    
    return ticks;
}

/**
 * @brief Get system timestamp in milliseconds
 * @return Current timestamp in milliseconds since midnight
 * 
 * Converts BIOS timer ticks to milliseconds for easier time calculations.
 * Note: Resets to 0 at midnight along with the BIOS timer.
 */
uint32_t get_system_timestamp_ms(void) {
    uint32_t ticks = get_system_timestamp_ticks();
    
    /* Convert ticks to milliseconds */
    /* Using integer math to avoid floating point: (ticks * 1000) / 18.2 */
    /* Approximation: (ticks * 1000 * 10) / 182 for better precision */
    return (ticks * 10000UL) / 182UL;
}

/**
 * @brief Calculate elapsed milliseconds from a starting tick count
 * @param start_ticks Starting tick count from get_system_timestamp_ticks()
 * @return Elapsed milliseconds since start_ticks
 * 
 * Handles day rollover properly - if current ticks < start_ticks,
 * assumes timer rolled over at midnight.
 */
uint32_t get_timestamp_elapsed_ms(uint32_t start_ticks) {
    uint32_t current_ticks = get_system_timestamp_ticks();
    uint32_t elapsed_ticks;
    
    /* Handle day rollover (timer resets at midnight) */
    if (current_ticks >= start_ticks) {
        elapsed_ticks = current_ticks - start_ticks;
    } else {
        /* Timer rolled over at midnight */
        elapsed_ticks = (TICKS_PER_DAY - start_ticks) + current_ticks;
    }
    
    /* Convert to milliseconds */
    return (elapsed_ticks * 10000UL) / 182UL;
}