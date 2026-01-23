/**
 * @file perf_stats.h
 * @brief Performance statistics tracking interface
 * 
 * Provides comprehensive performance metrics collection and reporting
 * for validating optimization effectiveness against DRIVER_TUNING.md targets.
 */

#ifndef _PERF_STATS_H_
#define _PERF_STATS_H_

#include <stdint.h>

/* Optimization type codes */
#define PERF_OPT_LAZY_TX    0
#define PERF_OPT_BATCH_RX   1
#define PERF_OPT_COPY_BREAK 2
#define PERF_OPT_SMC_PATCH  3

/**
 * @brief Initialize performance statistics
 */
void perf_stats_init(void);

/**
 * @brief Update packet statistics
 * 
 * @param nic_index NIC index (0-3)
 * @param is_tx 1 for TX, 0 for RX
 * @param length Packet length
 * @param success 1 if successful, 0 if error
 */
void perf_stats_update_packet(uint8_t nic_index, uint8_t is_tx,
                              uint16_t length, uint8_t success);

/**
 * @brief Update interrupt statistics
 * 
 * @param nic_index NIC index (0-3)
 */
void perf_stats_update_interrupt(uint8_t nic_index);

/**
 * @brief Update optimization statistics
 * 
 * @param nic_index NIC index (0-3)
 * @param opt_type Optimization type (PERF_OPT_*)
 * @param value Value to add to counter
 */
void perf_stats_update_optimization(uint8_t nic_index, uint8_t opt_type,
                                   uint32_t value);

/**
 * @brief Get performance statistics for a NIC
 * 
 * @param nic_index NIC index (0-3)
 * @param buffer Output buffer (must be at least sizeof(nic_stats_t))
 */
void perf_stats_get(uint8_t nic_index, void *buffer);

/**
 * @brief Display performance summary
 * 
 * @param nic_index NIC index (0-3)
 */
void perf_stats_display(uint8_t nic_index);

/**
 * @brief Reset performance statistics
 * 
 * @param nic_index NIC index (0-3)
 */
void perf_stats_reset(uint8_t nic_index);

/**
 * @brief Check if performance targets are met
 * 
 * @param nic_index NIC index (0-3)
 * @return 1 if all targets met, 0 otherwise
 */
uint8_t perf_stats_targets_met(uint8_t nic_index);

/**
 * @brief Enable/disable statistics collection
 * 
 * @param enable 1 to enable, 0 to disable
 */
void perf_stats_enable(uint8_t enable);

#endif /* _PERF_STATS_H_ */