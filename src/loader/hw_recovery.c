/**
 * @file hw_recovery.c
 * @brief Hardware Error Recovery Implementation
 * 
 * Provides comprehensive hardware error recovery mechanisms
 * for 3Com NICs (3C509B/3C515) with bounded retry logic
 * and escalation to full reset when needed.
 */

#include "hw_recovery.h"
#include "timer_services.h"
#include "../include/logging.h"
#include "../include/hardware.h"
#include <i86.h>
#include <string.h>

/* 3C509/515 I/O helpers */
static __inline void io_delay(void) { outp(0x80, 0); }
static __inline void el3_outw(uint16_t port, uint16_t val) { outpw(port, val); }
static __inline uint16_t el3_inw(uint16_t port) { return inpw(port); }
static __inline void el3_outb(uint16_t port, uint8_t val) { outp(port, val); }
static __inline uint8_t el3_inb(uint16_t port) { return inp(port); }

/* Port offsets and commands (from existing hardware headers) */
#define EL3_CMD(base)      ((base) + 0x0E)
#define EL3_STATUS(base)   ((base) + 0x0E)
#define EL3_IOBP(base,ofs) ((base) + (ofs))

/* Commands */
#define CMD_SELECT_WINDOW(n)   (0x0800 | ((n) & 7))
#define CMD_TX_RESET           0x0001
#define CMD_RX_RESET           0x0002
#define CMD_TOTAL_RESET        0x0000
#define CMD_RX_DISABLE         0x0016
#define CMD_RX_ENABLE          0x0004
#define CMD_TX_ENABLE          0x0008
#define CMD_TX_DISABLE         0x000A
#define CMD_ACK_INTR(mask)     (0x0A00 | (mask))
#define CMD_SET_INTR_MASK(m)   (0x0C00 | (m))

/* Status bits */
#define STATUS_CIP_BIT         0x1000

/* Window 1 offsets */
#define W1_RX_STATUS(base)     EL3_IOBP(base, 0x08)
#define W1_TX_STATUS(base)     EL3_IOBP(base, 0x0B)
#define W1_TX_FREE(base)       EL3_IOBP(base, 0x0C)

/* Interrupt masks */
#define INTR_LATCHED_RX        0x0001
#define INTR_LATCHED_TX        0x0004
#define INTR_RX_OVERRUN        0x0040
#define INTR_ADAPTER_FAILURE   0x0020
#define INTR_TX_JABBER         0x0008
#define INTR_ALL               0xFFFF

/* Recovery statistics */
static hw_recovery_stats_t g_recovery_stats;
static int g_recovery_initialized = 0;

/**
 * @brief Wait for Command-In-Progress to clear
 */
static int el3_wait_cip_clear(uint16_t base, int bound)
{
    while (bound-- > 0) {
        if ((el3_inw(EL3_STATUS(base)) & STATUS_CIP_BIT) == 0)
            return 0;
        io_delay();
    }
    return -1;
}

/**
 * @brief Select register window
 */
static void el3_select_window(uint16_t base, uint8_t w)
{
    el3_outw(EL3_CMD(base), CMD_SELECT_WINDOW(w));
    (void)el3_wait_cip_clear(base, HW_RECOVERY_POLL_BOUND);
}

/**
 * @brief Initialize hardware recovery system
 */
int hw_recovery_init(void)
{
    if (g_recovery_initialized) {
        return RECOVERY_SUCCESS;
    }
    
    memset(&g_recovery_stats, 0, sizeof(hw_recovery_stats_t));
    g_recovery_initialized = 1;
    
    LOG_INFO("Hardware recovery system initialized");
    return RECOVERY_SUCCESS;
}

/**
 * @brief Recover stuck TX queue
 */
int hw_recover_tx(uint16_t io_base, uint8_t nic_type)
{
    int retry_count = 0;
    uint32_t start_time = get_millisecond_timestamp();
    
    LOG_DEBUG("Starting TX recovery for NIC at I/O 0x%X", io_base);
    
    while (retry_count < HW_RECOVERY_MAX_RETRIES) {
        /* Step 1: Disable TX */
        el3_outw(EL3_CMD(io_base), CMD_TX_DISABLE);
        if (el3_wait_cip_clear(io_base, HW_RECOVERY_POLL_BOUND) != 0) {
            LOG_WARNING("TX disable timeout on attempt %d", retry_count + 1);
            retry_count++;
            continue;
        }
        
        /* Step 2: Reset TX engine */
        el3_outw(EL3_CMD(io_base), CMD_TX_RESET);
        if (el3_wait_cip_clear(io_base, HW_RECOVERY_POLL_BOUND) != 0) {
            LOG_WARNING("TX reset timeout on attempt %d", retry_count + 1);
            retry_count++;
            continue;
        }
        
        /* Step 3: Clear TX status */
        el3_select_window(io_base, 1);
        el3_outw(W1_TX_STATUS(base), 0xFFFF);
        
        /* Step 4: Re-enable TX */
        el3_outw(EL3_CMD(io_base), CMD_TX_ENABLE);
        if (el3_wait_cip_clear(io_base, HW_RECOVERY_POLL_BOUND) != 0) {
            LOG_WARNING("TX enable timeout on attempt %d", retry_count + 1);
            retry_count++;
            continue;
        }
        
        /* Success */
        uint32_t recovery_time = get_millisecond_timestamp() - start_time;
        LOG_INFO("TX recovery successful in %u ms (attempt %d)", 
                 recovery_time, retry_count + 1);
        g_recovery_stats.tx_recoveries++;
        return RECOVERY_SUCCESS;
    }
    
    /* All retries failed */
    uint32_t total_time = get_millisecond_timestamp() - start_time;
    LOG_ERROR("TX recovery failed after %d attempts (%u ms)", 
              HW_RECOVERY_MAX_RETRIES, total_time);
    g_recovery_stats.failed_recoveries++;
    return RECOVERY_ERROR_TIMEOUT;
}

/**
 * @brief Recover RX buffer overflow
 */
int hw_recover_rx_overflow(uint16_t io_base, uint8_t nic_type)
{
    int retry_count = 0;
    uint32_t start_time = get_millisecond_timestamp();
    
    LOG_DEBUG("Starting RX overflow recovery for NIC at I/O 0x%X", io_base);
    
    while (retry_count < HW_RECOVERY_MAX_RETRIES) {
        /* Step 1: Disable RX */
        el3_outw(EL3_CMD(io_base), CMD_RX_DISABLE);
        if (el3_wait_cip_clear(io_base, HW_RECOVERY_POLL_BOUND) != 0) {
            LOG_WARNING("RX disable timeout on attempt %d", retry_count + 1);
            retry_count++;
            continue;
        }
        
        /* Step 2: Reset RX engine */
        el3_outw(EL3_CMD(io_base), CMD_RX_RESET);
        if (el3_wait_cip_clear(io_base, HW_RECOVERY_POLL_BOUND * 2) != 0) {
            LOG_WARNING("RX reset timeout on attempt %d", retry_count + 1);
            retry_count++;
            continue;
        }
        
        /* Step 3: Clear RX status */
        el3_select_window(io_base, 1);
        el3_outw(W1_RX_STATUS(base), 0xFFFF);
        
        /* Step 4: Acknowledge overflow interrupts */
        el3_outw(EL3_CMD(io_base), CMD_ACK_INTR(INTR_RX_OVERRUN | INTR_LATCHED_RX));
        
        /* Step 5: Re-enable RX */
        el3_outw(EL3_CMD(io_base), CMD_RX_ENABLE);
        if (el3_wait_cip_clear(io_base, HW_RECOVERY_POLL_BOUND) != 0) {
            LOG_WARNING("RX enable timeout on attempt %d", retry_count + 1);
            retry_count++;
            continue;
        }
        
        /* Success */
        uint32_t recovery_time = get_millisecond_timestamp() - start_time;
        LOG_INFO("RX overflow recovery successful in %u ms (attempt %d)", 
                 recovery_time, retry_count + 1);
        g_recovery_stats.rx_recoveries++;
        return RECOVERY_SUCCESS;
    }
    
    /* All retries failed */
    uint32_t total_time = get_millisecond_timestamp() - start_time;
    LOG_ERROR("RX overflow recovery failed after %d attempts (%u ms)", 
              HW_RECOVERY_MAX_RETRIES, total_time);
    g_recovery_stats.failed_recoveries++;
    return RECOVERY_ERROR_TIMEOUT;
}

/**
 * @brief Recover interrupt problems
 */
int hw_recover_interrupts(uint16_t io_base, uint8_t nic_type)
{
    int retry_count = 0;
    uint32_t start_time = get_millisecond_timestamp();
    
    LOG_DEBUG("Starting interrupt recovery for NIC at I/O 0x%X", io_base);
    
    while (retry_count < HW_RECOVERY_MAX_RETRIES) {
        /* Step 1: Mask all interrupts */
        el3_outw(EL3_CMD(io_base), CMD_SET_INTR_MASK(0));
        if (el3_wait_cip_clear(io_base, HW_RECOVERY_POLL_BOUND) != 0) {
            LOG_WARNING("Interrupt mask timeout on attempt %d", retry_count + 1);
            retry_count++;
            continue;
        }
        
        /* Step 2: Acknowledge all latched interrupts */
        el3_outw(EL3_CMD(io_base), CMD_ACK_INTR(INTR_ALL));
        if (el3_wait_cip_clear(io_base, HW_RECOVERY_POLL_BOUND) != 0) {
            LOG_WARNING("Interrupt ack timeout on attempt %d", retry_count + 1);
            retry_count++;
            continue;
        }
        
        /* Step 3: Clear status registers */
        el3_select_window(io_base, 1);
        el3_outw(W1_RX_STATUS(base), 0xFFFF);
        el3_outw(W1_TX_STATUS(base), 0xFFFF);
        
        /* Step 4: Re-enable normal interrupt mask with rate limiting */
        /* Use a conservative mask initially */
        uint16_t safe_mask = INTR_LATCHED_RX | INTR_LATCHED_TX | INTR_ADAPTER_FAILURE;
        el3_outw(EL3_CMD(io_base), CMD_SET_INTR_MASK(safe_mask));
        if (el3_wait_cip_clear(io_base, HW_RECOVERY_POLL_BOUND) != 0) {
            LOG_WARNING("Interrupt unmask timeout on attempt %d", retry_count + 1);
            retry_count++;
            continue;
        }
        
        /* Success */
        uint32_t recovery_time = get_millisecond_timestamp() - start_time;
        LOG_INFO("Interrupt recovery successful in %u ms (attempt %d)", 
                 recovery_time, retry_count + 1);
        g_recovery_stats.interrupt_recoveries++;
        return RECOVERY_SUCCESS;
    }
    
    /* All retries failed */
    uint32_t total_time = get_millisecond_timestamp() - start_time;
    LOG_ERROR("Interrupt recovery failed after %d attempts (%u ms)", 
              HW_RECOVERY_MAX_RETRIES, total_time);
    g_recovery_stats.failed_recoveries++;
    return RECOVERY_ERROR_TIMEOUT;
}

/**
 * @brief Perform full hardware reset
 */
int hw_full_reset(uint16_t io_base, uint8_t nic_type, int restore_config)
{
    uint32_t start_time = get_millisecond_timestamp();
    int i;
    
    LOG_WARNING("Performing full hardware reset for NIC at I/O 0x%X", io_base);
    
    /* Step 1: Total reset */
    el3_outw(EL3_CMD(io_base), CMD_TOTAL_RESET);
    
    /* Step 2: Wait for reset to complete (>1ms) */
    for (i = 0; i < 1000; ++i) io_delay();
    
    /* Step 3: Wait for hardware to stabilize */
    delay_milliseconds(10);
    
    /* Step 4: Basic reinitalization */
    /* Select window 0 for basic setup */
    el3_select_window(io_base, 0);
    
    if (restore_config) {
        /* Step 5: Restore configuration based on NIC type */
        if (nic_type == NIC_TYPE_3C515_TX) {
            /* For 3C515, need to reprogram PHY/MII as well */
            LOG_DEBUG("Restoring 3C515 PHY configuration");
            /* Note: This would call existing PHY init functions */
            /* mii_init_3c515(io_base); */
        }
        
        /* Restore basic configuration */
        LOG_DEBUG("Restoring basic NIC configuration");
        /* Note: This would call existing config restore functions */
        /* restore_nic_config(io_base, nic_type); */
    }
    
    /* Step 6: Re-enable RX/TX */
    el3_outw(EL3_CMD(io_base), CMD_RX_ENABLE);
    el3_wait_cip_clear(io_base, HW_RECOVERY_POLL_BOUND);
    
    el3_outw(EL3_CMD(io_base), CMD_TX_ENABLE);
    el3_wait_cip_clear(io_base, HW_RECOVERY_POLL_BOUND);
    
    /* Step 7: Enable basic interrupts */
    uint16_t basic_mask = INTR_LATCHED_RX | INTR_LATCHED_TX | INTR_ADAPTER_FAILURE;
    el3_outw(EL3_CMD(io_base), CMD_SET_INTR_MASK(basic_mask));
    
    uint32_t reset_time = get_millisecond_timestamp() - start_time;
    LOG_WARNING("Full hardware reset completed in %u ms", reset_time);
    g_recovery_stats.hardware_resets++;
    
    return RECOVERY_SUCCESS;
}

/**
 * @brief Generic recovery dispatch
 */
int hw_recovery_dispatch(uint16_t io_base, uint8_t nic_type, 
                        recovery_type_t recovery_type, int escalate_on_failure)
{
    int result;
    
    if (!g_recovery_initialized) {
        hw_recovery_init();
    }
    
    LOG_DEBUG("Dispatching %s recovery for NIC at I/O 0x%X", 
              (recovery_type == RECOVERY_TYPE_TX) ? "TX" :
              (recovery_type == RECOVERY_TYPE_RX) ? "RX" :
              (recovery_type == RECOVERY_TYPE_INTERRUPT) ? "INTERRUPT" :
              "HARDWARE", io_base);
    
    /* Attempt specific recovery */
    switch (recovery_type) {
        case RECOVERY_TYPE_TX:
            result = hw_recover_tx(io_base, nic_type);
            break;
        case RECOVERY_TYPE_RX:
            result = hw_recover_rx_overflow(io_base, nic_type);
            break;
        case RECOVERY_TYPE_INTERRUPT:
            result = hw_recover_interrupts(io_base, nic_type);
            break;
        case RECOVERY_TYPE_HARDWARE:
            result = hw_full_reset(io_base, nic_type, 1);
            break;
        default:
            LOG_ERROR("Invalid recovery type: %d", recovery_type);
            return RECOVERY_ERROR_INVALID;
    }
    
    /* Escalate to full reset if requested and initial recovery failed */
    if (result != RECOVERY_SUCCESS && escalate_on_failure && 
        recovery_type != RECOVERY_TYPE_HARDWARE) {
        LOG_WARNING("Primary recovery failed, escalating to full reset");
        result = hw_full_reset(io_base, nic_type, 1);
        if (result == RECOVERY_SUCCESS) {
            g_recovery_stats.escalations++;
            return RECOVERY_ESCALATED;
        }
    }
    
    return result;
}

/**
 * @brief Check if hardware appears healthy
 */
int hw_health_check(uint16_t io_base, uint8_t nic_type)
{
    uint16_t status;
    
    /* Basic status check */
    status = el3_inw(EL3_STATUS(io_base));
    
    /* Check if command-in-progress is stuck */
    if (status & STATUS_CIP_BIT) {
        /* Wait briefly and check again */
        delay_milliseconds(1);
        status = el3_inw(EL3_STATUS(io_base));
        if (status & STATUS_CIP_BIT) {
            LOG_DEBUG("Health check failed: CIP stuck");
            return 0;
        }
    }
    
    /* Check for adapter failure */
    if (status & INTR_ADAPTER_FAILURE) {
        LOG_DEBUG("Health check failed: adapter failure bit set");
        return 0;
    }
    
    /* Additional checks based on NIC type */
    if (nic_type == NIC_TYPE_3C515_TX) {
        /* Check TX/RX enable status by attempting to read FIFO space */
        el3_select_window(io_base, 1);
        uint16_t tx_free = el3_inw(W1_TX_FREE(base));
        if (tx_free == 0 || tx_free > 8192) {  /* Sanity check */
            LOG_DEBUG("Health check failed: invalid TX free space: %u", tx_free);
            return 0;
        }
    }
    
    return 1; /* Hardware appears healthy */
}

/**
 * @brief Get recovery statistics
 */
void hw_recovery_get_stats(hw_recovery_stats_t *stats)
{
    if (stats) {
        *stats = g_recovery_stats;
    }
}

/**
 * @brief Reset recovery statistics
 */
void hw_recovery_reset_stats(void)
{
    memset(&g_recovery_stats, 0, sizeof(hw_recovery_stats_t));
    LOG_DEBUG("Hardware recovery statistics reset");
}

/**
 * @brief Cleanup recovery system
 */
void hw_recovery_cleanup(void)
{
    if (g_recovery_initialized) {
        LOG_DEBUG("Hardware recovery system cleanup");
        g_recovery_initialized = 0;
    }
}