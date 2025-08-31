/**
 * @file hw_recovery.h
 * @brief Hardware Error Recovery Interface
 * 
 * Provides comprehensive hardware error recovery mechanisms
 * for 3Com NICs (3C509B/3C515) with bounded retry logic
 * and escalation to full reset when needed.
 */

#ifndef HW_RECOVERY_H
#define HW_RECOVERY_H

#include <stdint.h>

/* Recovery result codes */
#define RECOVERY_SUCCESS           0
#define RECOVERY_ERROR_TIMEOUT    -1
#define RECOVERY_ERROR_HARDWARE   -2
#define RECOVERY_ERROR_INVALID    -3
#define RECOVERY_ESCALATED        -4

/* Recovery types */
typedef enum {
    RECOVERY_TYPE_TX,         /* TX queue stuck/timeout */
    RECOVERY_TYPE_RX,         /* RX buffer overflow */
    RECOVERY_TYPE_INTERRUPT,  /* Interrupt storm/lost */
    RECOVERY_TYPE_HARDWARE    /* General hardware lockup */
} recovery_type_t;

/* Recovery statistics */
typedef struct {
    uint32_t tx_recoveries;
    uint32_t rx_recoveries;
    uint32_t interrupt_recoveries;
    uint32_t hardware_resets;
    uint32_t failed_recoveries;
    uint32_t escalations;
} hw_recovery_stats_t;

/**
 * @brief Initialize hardware recovery system
 * 
 * @return 0 on success, negative on error
 */
int hw_recovery_init(void);

/**
 * @brief Recover stuck TX queue
 * 
 * Implements: TX disable → TX reset → clear status → TX enable → rebuild queue
 * 
 * @param io_base NIC I/O base address
 * @param nic_type NIC type (3C509B or 3C515)
 * @return RECOVERY_SUCCESS or negative error code
 */
int hw_recover_tx(uint16_t io_base, uint8_t nic_type);

/**
 * @brief Recover RX buffer overflow
 * 
 * Implements: RX disable → RX reset → clear status → ack interrupts → RX enable
 * 
 * @param io_base NIC I/O base address
 * @param nic_type NIC type (3C509B or 3C515)
 * @return RECOVERY_SUCCESS or negative error code
 */
int hw_recover_rx_overflow(uint16_t io_base, uint8_t nic_type);

/**
 * @brief Recover interrupt problems (storm/lost)
 * 
 * Implements: mask → ack all → drain → clear status → re-enable with limits
 * 
 * @param io_base NIC I/O base address
 * @param nic_type NIC type (3C509B or 3C515)
 * @return RECOVERY_SUCCESS or negative error code
 */
int hw_recover_interrupts(uint16_t io_base, uint8_t nic_type);

/**
 * @brief Perform full hardware reset
 * 
 * Implements: Total reset → reinitialize → restore config → reprogram PHY
 * 
 * @param io_base NIC I/O base address
 * @param nic_type NIC type (3C509B or 3C515)
 * @param restore_config Restore previous configuration
 * @return RECOVERY_SUCCESS or negative error code
 */
int hw_full_reset(uint16_t io_base, uint8_t nic_type, int restore_config);

/**
 * @brief Generic recovery dispatch
 * 
 * Chooses appropriate recovery method and escalates if needed.
 * 
 * @param io_base NIC I/O base address
 * @param nic_type NIC type
 * @param recovery_type Type of recovery needed
 * @param escalate_on_failure Escalate to full reset on failure
 * @return RECOVERY_SUCCESS or negative error code
 */
int hw_recovery_dispatch(uint16_t io_base, uint8_t nic_type, 
                        recovery_type_t recovery_type, int escalate_on_failure);

/**
 * @brief Get recovery statistics
 * 
 * @param stats Structure to fill with statistics
 */
void hw_recovery_get_stats(hw_recovery_stats_t *stats);

/**
 * @brief Reset recovery statistics
 */
void hw_recovery_reset_stats(void);

/**
 * @brief Check if hardware appears healthy
 * 
 * Performs basic health checks without recovery actions.
 * 
 * @param io_base NIC I/O base address
 * @param nic_type NIC type
 * @return 1 if healthy, 0 if problems detected
 */
int hw_health_check(uint16_t io_base, uint8_t nic_type);

/**
 * @brief Cleanup recovery system
 */
void hw_recovery_cleanup(void);

/* Recovery configuration */
#define HW_RECOVERY_MAX_RETRIES    3    /* Max retry attempts */
#define HW_RECOVERY_TIMEOUT_MS     100  /* Timeout for operations */
#define HW_RECOVERY_POLL_BOUND     200  /* Max polling loops */

#endif /* HW_RECOVERY_H */