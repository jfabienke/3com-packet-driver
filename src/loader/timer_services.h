/**
 * @file timer_services.h
 * @brief DOS Timer Services Interface
 * 
 * Provides millisecond precision timestamps and timer functions
 * for the packet driver using DOS-compatible timing mechanisms.
 */

#ifndef TIMER_SERVICES_H
#define TIMER_SERVICES_H

#include <stdint.h>

/**
 * @brief Get millisecond timestamp using BIOS tick + PIT fraction
 * 
 * Provides monotonic millisecond timestamps suitable for timeouts
 * and performance measurements in DOS environment.
 * 
 * @return Current timestamp in milliseconds
 */
uint32_t get_millisecond_timestamp(void);

/**
 * @brief Delay execution for specified milliseconds
 * 
 * @param delay_ms Milliseconds to delay
 */
void delay_milliseconds(uint32_t delay_ms);

/**
 * @brief Get high precision timestamp in microseconds
 * 
 * @return Current timestamp in microseconds (approximate)
 */
uint32_t get_microsecond_timestamp(void);

/**
 * @brief Check if specified timeout has elapsed
 * 
 * @param start_time Starting timestamp
 * @param timeout_ms Timeout in milliseconds
 * @return 1 if timeout elapsed, 0 otherwise
 */
int is_timeout_elapsed(uint32_t start_time, uint32_t timeout_ms);

#endif /* TIMER_SERVICES_H */