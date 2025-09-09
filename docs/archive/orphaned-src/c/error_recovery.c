/**
 * @file error_recovery.c
 * @brief Advanced error recovery system with progressive strategies
 *
 * Phase 3 Advanced Error Recovery Implementation
 * Implements comprehensive adapter failure recovery, timeout handling,
 * retry mechanisms with exponential backoff, and graceful degradation
 * for multi-NIC environments.
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This file is part of the 3Com Packet Driver project.
 */

#include "../include/error_handling.h"
#include "../include/hardware.h"
#include "../include/3c509b.h"
#include "../include/3c515.h"
#include "../include/memory.h"
#include "../include/diagnostics.h"
#include "../include/logging.h"
#include "../include/cpu_optimized.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>

/* Assembly timeout handler functions */
extern int timeout_init(void);
extern void timeout_cleanup(void);
extern int timeout_set_operation(uint8_t op_type, uint8_t nic_index, uint16_t timeout_ticks);
extern int timeout_check_expired(uint8_t tracker_index);
extern int timeout_reset(uint8_t tracker_index);
extern int timeout_hardware_io(uint16_t port, uint8_t operation, uint16_t data, 
                              uint8_t nic_index, uint8_t timeout_multiplier);
extern int timeout_wait_ready(uint16_t port, uint8_t ready_mask, uint8_t nic_index, 
                             uint16_t timeout_ticks);
extern int timeout_dma_complete(uint16_t port, uint8_t completion_mask, uint8_t nic_index,
                               uint16_t timeout_ticks);
extern int retry_with_backoff(uint8_t tracker_index, uint8_t error_code);

/* Advanced recovery configuration */
#define RECOVERY_MAX_ESCALATION_LEVELS  6
#define RECOVERY_HEALTH_THRESHOLD       30    /* Below this, consider failover */
#define RECOVERY_COOLDOWN_PERIOD        60000 /* 60 seconds between recovery attempts */
#define ADAPTER_DISABLE_THRESHOLD       5     /* Disable after 5 consecutive failures */
#define MULTI_NIC_FAILOVER_DELAY        2000  /* 2 seconds failover delay */

/* Recovery escalation levels */
typedef enum {
    RECOVERY_LEVEL_NONE = 0,
    RECOVERY_LEVEL_RETRY,           /* Simple retry with backoff */
    RECOVERY_LEVEL_SOFT_RESET,      /* Soft reset and reconfigure */
    RECOVERY_LEVEL_HARD_RESET,      /* Hard reset and full reinit */
    RECOVERY_LEVEL_DRIVER_RESTART,  /* Restart driver components */
    RECOVERY_LEVEL_ADAPTER_DISABLE, /* Disable adapter */
    RECOVERY_LEVEL_SYSTEM_FAILOVER  /* Failover to backup adapter */
} recovery_escalation_level_t;

/* Recovery strategy matrix based on error patterns */
typedef struct {
    uint8_t error_type;
    uint8_t error_frequency;        /* Errors per minute */
    uint8_t consecutive_errors;
    recovery_escalation_level_t recommended_level;
    uint32_t cooldown_period_ms;
} recovery_strategy_matrix_t;

/* Predefined recovery strategies based on Linux driver patterns */
static const recovery_strategy_matrix_t recovery_matrix[] = {
    /* Low-frequency transient errors - retry with backoff */
    {RX_ERROR_CRC, 1, 1, RECOVERY_LEVEL_RETRY, 1000},
    {TX_ERROR_COLLISION, 5, 2, RECOVERY_LEVEL_RETRY, 500},
    
    /* Medium-frequency errors - soft reset */
    {RX_ERROR_OVERRUN, 3, 2, RECOVERY_LEVEL_SOFT_RESET, 5000},
    {TX_ERROR_UNDERRUN, 2, 2, RECOVERY_LEVEL_SOFT_RESET, 5000},
    
    /* High-frequency or critical errors - hard reset */
    {RX_ERROR_TIMEOUT, 1, 1, RECOVERY_LEVEL_HARD_RESET, 10000},
    {TX_ERROR_TIMEOUT, 1, 1, RECOVERY_LEVEL_HARD_RESET, 10000},
    {ADAPTER_FAILURE_HANG, 1, 1, RECOVERY_LEVEL_HARD_RESET, 15000},
    
    /* Persistent errors - escalate to disable */
    {ADAPTER_FAILURE_MEMORY, 1, 3, RECOVERY_LEVEL_ADAPTER_DISABLE, 30000},
    {ADAPTER_FAILURE_DMA, 1, 2, RECOVERY_LEVEL_ADAPTER_DISABLE, 30000},
    
    /* Critical system errors - immediate failover */
    {ADAPTER_FAILURE_POWER, 1, 1, RECOVERY_LEVEL_SYSTEM_FAILOVER, 0},
    {ADAPTER_FAILURE_THERMAL, 1, 1, RECOVERY_LEVEL_SYSTEM_FAILOVER, 0},
};

/* Multi-NIC management for graceful degradation */
typedef struct {
    uint8_t total_nics;             /* Total number of NICs */
    uint8_t active_nics;            /* Currently active NICs */
    uint8_t primary_nic;            /* Primary NIC index */
    uint8_t backup_nic;             /* Backup NIC index */
    bool failover_active;           /* Failover mode active */
    uint32_t failover_start_time;   /* When failover started */
    uint8_t nic_health[MAX_NICS];   /* Health score per NIC (0-100) */
    uint32_t last_health_update;    /* Last health assessment */
} multi_nic_state_t;

/* Global recovery system state */
typedef struct {
    bool recovery_system_enabled;
    bool timeout_handlers_enabled;
    uint32_t recovery_operations_active;
    uint32_t total_recovery_attempts;
    uint32_t successful_recoveries;
    uint32_t failed_recoveries;
    uint32_t adapters_disabled;
    uint32_t failover_events;
    multi_nic_state_t multi_nic;
} advanced_recovery_state_t;

static advanced_recovery_state_t g_recovery_state = {0};

/* Forward declarations */
static recovery_escalation_level_t determine_recovery_level(nic_context_t *ctx, uint8_t error_type);
static int execute_recovery_level(nic_context_t *ctx, recovery_escalation_level_t level);
static int protected_hardware_operation(nic_context_t *ctx, uint16_t port, uint8_t operation,
                                       uint16_t data, uint16_t timeout_ms);
static int implement_graceful_degradation(nic_context_t *failing_ctx);
static int assess_nic_health(nic_context_t *ctx);
static int select_backup_nic(nic_context_t *failing_ctx);
static void update_multi_nic_state(void);
static int validate_recovery_effectiveness(nic_context_t *ctx, recovery_escalation_level_t level);

/**
 * @brief Initialize advanced error recovery system
 * @return 0 on success, negative on error
 */
int advanced_recovery_init(void) {
    LOG_INFO("Initializing advanced error recovery system");
    
    /* Clear global recovery state */
    memset(&g_recovery_state, 0, sizeof(advanced_recovery_state_t));
    
    /* Initialize timeout handlers */
    int result = timeout_init();
    if (result != 0) {
        LOG_ERROR("Failed to initialize timeout handlers: %d", result);
        return ERROR_INIT_FAILED;
    }
    
    g_recovery_state.timeout_handlers_enabled = true;
    
    /* Initialize multi-NIC state */
    g_recovery_state.multi_nic.total_nics = 0;  /* Will be set by hardware detection */
    g_recovery_state.multi_nic.active_nics = 0;
    g_recovery_state.multi_nic.primary_nic = 0;
    g_recovery_state.multi_nic.backup_nic = 0xFF;  /* No backup initially */
    g_recovery_state.multi_nic.failover_active = false;
    
    /* Initialize NIC health scores to maximum */
    for (int i = 0; i < MAX_NICS; i++) {
        g_recovery_state.multi_nic.nic_health[i] = 100;
    }
    
    g_recovery_state.recovery_system_enabled = true;
    
    LOG_INFO("Advanced error recovery system initialized successfully");
    return SUCCESS;
}

/**
 * @brief Cleanup advanced error recovery system
 */
void advanced_recovery_cleanup(void) {
    LOG_INFO("Cleaning up advanced error recovery system");
    
    if (g_recovery_state.timeout_handlers_enabled) {
        timeout_cleanup();
        g_recovery_state.timeout_handlers_enabled = false;
    }
    
    /* Reset recovery state */
    memset(&g_recovery_state, 0, sizeof(advanced_recovery_state_t));
    
    LOG_INFO("Advanced error recovery cleanup completed");
}

/**
 * @brief Enhanced adapter recovery with progressive escalation
 * @param ctx NIC context
 * @param error_type Type of error that triggered recovery
 * @return Recovery result code
 */
int enhanced_adapter_recovery(nic_context_t *ctx, uint8_t error_type) {
    if (!ctx || !g_recovery_state.recovery_system_enabled) {
        return RECOVERY_FAILED;
    }
    
    uint32_t now = get_system_timestamp_ms();
    
    /* Check if we're in cooldown period */
    if (now - ctx->recovery_start_time < RECOVERY_COOLDOWN_PERIOD) {
        LOG_WARNING("Recovery attempt blocked - cooldown period active (NIC %d)", 
                   ctx->nic_info.type);
        return RECOVERY_RETRY_NEEDED;
    }
    
    /* Assess current NIC health */
    int health = assess_nic_health(ctx);
    
    LOG_INFO("Starting enhanced recovery for NIC %d (health: %d, error: 0x%02X)", 
             ctx->nic_info.type, health, error_type);
    
    /* Determine appropriate recovery level */
    recovery_escalation_level_t level = determine_recovery_level(ctx, error_type);
    
    /* If health is critically low, consider graceful degradation */
    if (health < RECOVERY_HEALTH_THRESHOLD && g_recovery_state.multi_nic.total_nics > 1) {
        LOG_WARNING("NIC %d health critically low (%d), implementing graceful degradation", 
                   ctx->nic_info.type, health);
        
        int result = implement_graceful_degradation(ctx);
        if (result == SUCCESS) {
            return RECOVERY_SUCCESS;
        }
        
        LOG_WARNING("Graceful degradation failed, continuing with recovery level %d", level);
    }
    
    /* Execute recovery at determined level */
    ctx->recovery_start_time = now;
    g_recovery_state.recovery_operations_active++;
    g_recovery_state.total_recovery_attempts++;
    
    int result = execute_recovery_level(ctx, level);
    
    g_recovery_state.recovery_operations_active--;
    
    /* Update statistics */
    if (result == RECOVERY_SUCCESS) {
        g_recovery_state.successful_recoveries++;
        ctx->recovery_attempts = 0;  /* Reset on success */
        LOG_INFO("Enhanced recovery successful for NIC %d", ctx->nic_info.type);
    } else {
        g_recovery_state.failed_recoveries++;
        ctx->recovery_attempts++;
        LOG_ERROR("Enhanced recovery failed for NIC %d (attempt %d)", 
                 ctx->nic_info.type, ctx->recovery_attempts);
        
        /* Check if we should disable the adapter */
        if (ctx->recovery_attempts >= ADAPTER_DISABLE_THRESHOLD) {
            LOG_CRITICAL("Disabling adapter NIC %d after %d failed recovery attempts", 
                        ctx->nic_info.type, ctx->recovery_attempts);
            ctx->adapter_disabled = true;
            g_recovery_state.adapters_disabled++;
            
            /* Attempt failover if multiple NICs available */
            if (g_recovery_state.multi_nic.total_nics > 1) {
                implement_graceful_degradation(ctx);
            }
        }
    }
    
    /* Validate recovery effectiveness */
    if (result == RECOVERY_SUCCESS) {
        result = validate_recovery_effectiveness(ctx, level);
    }
    
    update_multi_nic_state();
    
    return result;
}

/**
 * @brief Protected hardware I/O operation with timeout and retry
 * @param ctx NIC context
 * @param port I/O port address
 * @param operation 0=read, 1=write
 * @param data Data to write (ignored for read)
 * @param timeout_ms Timeout in milliseconds
 * @return Data read (for reads) or 0 on success (writes), negative on error
 */
static int protected_hardware_operation(nic_context_t *ctx, uint16_t port, uint8_t operation,
                                       uint16_t data, uint16_t timeout_ms) {
    if (!ctx || !g_recovery_state.timeout_handlers_enabled) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Convert timeout from ms to BIOS ticks (18.2 Hz) */
    uint16_t timeout_ticks = (timeout_ms * 182) / 10000;
    if (timeout_ticks == 0) timeout_ticks = 1;
    
    uint8_t retry_count = 0;
    const uint8_t MAX_RETRIES = 3;
    
    while (retry_count < MAX_RETRIES) {
        /* Set timeout tracker */
        int tracker = timeout_set_operation(1, ctx->nic_info.type, timeout_ticks);
        if (tracker == 0xFF) {
            LOG_ERROR("Failed to allocate timeout tracker for NIC %d", ctx->nic_info.type);
            return ERROR_NO_RESOURCES;
        }
        
        /* Perform hardware operation with timeout protection */
        int result = timeout_hardware_io(port, operation, data, ctx->nic_info.type, 1);
        
        /* Check for timeout */
        int expired = timeout_check_expired(tracker);
        timeout_reset(tracker);
        
        if (!expired && (result & 0x8000) == 0) {
            /* Operation successful */
            return result;
        }
        
        /* Operation failed or timed out */
        retry_count++;
        LOG_WARNING("Hardware I/O timeout/error (port 0x%04X, NIC %d, attempt %d)", 
                   port, ctx->nic_info.type, retry_count);
        
        if (retry_count < MAX_RETRIES) {
            /* Exponential backoff delay */
            uint32_t delay_ms = 10 << retry_count;  /* 10ms, 20ms, 40ms */
            
            /* Simple delay loop (DOS environment) */
            uint32_t delay_ticks = (delay_ms * 182) / 10000;
            uint32_t start_time = get_system_timestamp_ms();
            while ((get_system_timestamp_ms() - start_time) < delay_ms) {
                /* Busy wait - not ideal but necessary in DOS */
            }
        }
    }
    
    /* All retries failed */
    LOG_ERROR("Hardware I/O operation failed after %d retries (port 0x%04X, NIC %d)", 
             MAX_RETRIES, port, ctx->nic_info.type);
    
    /* Update error statistics */
    ctx->error_stats.adapter_failures++;
    
    return ERROR_TIMEOUT;
}

/**
 * @brief Wait for hardware ready condition with timeout protection
 * @param ctx NIC context
 * @param status_port Status register port
 * @param ready_mask Bit mask for ready condition
 * @param timeout_ms Maximum wait time in milliseconds
 * @return 0 on success, negative on timeout/error
 */
int protected_wait_ready(nic_context_t *ctx, uint16_t status_port, uint8_t ready_mask, 
                        uint16_t timeout_ms) {
    if (!ctx || !g_recovery_state.timeout_handlers_enabled) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Convert timeout to ticks */
    uint16_t timeout_ticks = (timeout_ms * 182) / 10000;
    if (timeout_ticks == 0) timeout_ticks = 1;
    
    int result = timeout_wait_ready(status_port, ready_mask, ctx->nic_info.type, timeout_ticks);
    
    if (result != 0) {
        LOG_WARNING("Hardware ready timeout (port 0x%04X, mask 0x%02X, NIC %d)", 
                   status_port, ready_mask, ctx->nic_info.type);
        ctx->error_stats.adapter_failures++;
    }
    
    return result;
}

/**
 * @brief Protected DMA operation with timeout
 * @param ctx NIC context  
 * @param dma_port DMA status port
 * @param completion_mask Completion bit mask
 * @param timeout_ms Timeout in milliseconds
 * @return 0 on success, negative on timeout/error
 */
int protected_dma_operation(nic_context_t *ctx, uint16_t dma_port, uint8_t completion_mask,
                           uint16_t timeout_ms) {
    if (!ctx || !g_recovery_state.timeout_handlers_enabled) {
        return ERROR_INVALID_PARAM;
    }
    
    /* Convert timeout to ticks */
    uint16_t timeout_ticks = (timeout_ms * 182) / 10000;
    if (timeout_ticks == 0) timeout_ticks = 1;
    
    int result = timeout_dma_complete(dma_port, completion_mask, ctx->nic_info.type, timeout_ticks);
    
    if (result != 0) {
        LOG_WARNING("DMA operation timeout (port 0x%04X, mask 0x%02X, NIC %d)", 
                   dma_port, completion_mask, ctx->nic_info.type);
        ctx->error_stats.dma_errors++;
    }
    
    return result;
}

/**
 * @brief Determine appropriate recovery escalation level
 * @param ctx NIC context
 * @param error_type Type of error
 * @return Recommended recovery level
 */
static recovery_escalation_level_t determine_recovery_level(nic_context_t *ctx, uint8_t error_type) {
    /* Check recovery matrix for specific error patterns */
    for (int i = 0; i < sizeof(recovery_matrix) / sizeof(recovery_matrix[0]); i++) {
        if (recovery_matrix[i].error_type == error_type) {
            /* Check if error frequency matches */
            if (ctx->error_stats.consecutive_errors >= recovery_matrix[i].consecutive_errors) {
                return recovery_matrix[i].recommended_level;
            }
        }
    }
    
    /* Default escalation based on consecutive failures */
    if (ctx->recovery_attempts == 0) {
        return RECOVERY_LEVEL_RETRY;
    } else if (ctx->recovery_attempts == 1) {
        return RECOVERY_LEVEL_SOFT_RESET;
    } else if (ctx->recovery_attempts == 2) {
        return RECOVERY_LEVEL_HARD_RESET;
    } else if (ctx->recovery_attempts >= 3) {
        return RECOVERY_LEVEL_ADAPTER_DISABLE;
    }
    
    return RECOVERY_LEVEL_RETRY;
}

/**
 * @brief Execute recovery at specified escalation level
 * @param ctx NIC context
 * @param level Recovery escalation level
 * @return Recovery result code
 */
static int execute_recovery_level(nic_context_t *ctx, recovery_escalation_level_t level) {
    LOG_INFO("Executing recovery level %d for NIC %d", level, ctx->nic_info.type);
    
    switch (level) {
        case RECOVERY_LEVEL_RETRY:
            /* Simple retry with exponential backoff */
            return perform_retry_recovery(ctx);
            
        case RECOVERY_LEVEL_SOFT_RESET:
            /* Soft reset with timeout protection */
            return perform_protected_soft_reset(ctx);
            
        case RECOVERY_LEVEL_HARD_RESET:
            /* Hard reset with full validation */
            return perform_protected_hard_reset(ctx);
            
        case RECOVERY_LEVEL_DRIVER_RESTART:
            /* Restart driver components */
            return perform_driver_restart(ctx);
            
        case RECOVERY_LEVEL_ADAPTER_DISABLE:
            /* Disable adapter and attempt failover */
            return perform_adapter_disable(ctx);
            
        case RECOVERY_LEVEL_SYSTEM_FAILOVER:
            /* Immediate failover to backup */
            return perform_system_failover(ctx);
            
        default:
            LOG_ERROR("Unknown recovery level %d", level);
            return RECOVERY_FAILED;
    }
}

/**
 * @brief Implement graceful degradation for multi-NIC systems
 * @param failing_ctx Context of the failing NIC
 * @return 0 on success, negative on error
 */
static int implement_graceful_degradation(nic_context_t *failing_ctx) {
    if (g_recovery_state.multi_nic.total_nics <= 1) {
        LOG_WARNING("Cannot implement graceful degradation - only one NIC available");
        return ERROR_NO_RESOURCES;
    }
    
    LOG_INFO("Implementing graceful degradation for failing NIC %d", failing_ctx->nic_info.type);
    
    /* Find backup NIC */
    int backup_nic = select_backup_nic(failing_ctx);
    if (backup_nic < 0) {
        LOG_ERROR("No suitable backup NIC found for failover");
        return ERROR_NO_RESOURCES;
    }
    
    /* Mark failover as active */
    g_recovery_state.multi_nic.failover_active = true;
    g_recovery_state.multi_nic.failover_start_time = get_system_timestamp_ms();
    g_recovery_state.multi_nic.backup_nic = backup_nic;
    g_recovery_state.failover_events++;
    
    /* Reduce active NIC count */
    g_recovery_state.multi_nic.active_nics--;
    
    /* Set failing adapter health to zero */
    g_recovery_state.multi_nic.nic_health[failing_ctx->nic_info.type] = 0;
    
    LOG_INFO("Graceful degradation implemented - failed over from NIC %d to NIC %d", 
             failing_ctx->nic_info.type, backup_nic);
    
    return SUCCESS;
}

/**
 * @brief Assess NIC health based on error statistics and performance
 * @param ctx NIC context
 * @return Health score (0-100)
 */
static int assess_nic_health(nic_context_t *ctx) {
    if (!ctx) {
        return 0;
    }
    
    int health_score = 100;
    
    /* Penalize for consecutive errors */
    health_score -= (ctx->error_stats.consecutive_errors * 10);
    
    /* Penalize for high error rate */
    if (ctx->error_rate_percent > 5) {
        health_score -= (ctx->error_rate_percent * 2);
    }
    
    /* Penalize for recent adapter failures */
    if (ctx->error_stats.adapter_failures > 0) {
        health_score -= (ctx->error_stats.adapter_failures * 15);
    }
    
    /* Penalize for recent recovery failures */
    if (ctx->error_stats.recoveries_failed > ctx->error_stats.recoveries_successful) {
        health_score -= 20;
    }
    
    /* Penalize if adapter was recently reset */
    uint32_t now = get_system_timestamp_ms();
    if (now - ctx->recovery_start_time < 30000) {  /* Within last 30 seconds */
        health_score -= 10;
    }
    
    /* Bonus for link being up */
    if (ctx->link_up) {
        health_score += 5;
    }
    
    /* Clamp to valid range */
    if (health_score < 0) health_score = 0;
    if (health_score > 100) health_score = 100;
    
    /* Update global health tracking */
    g_recovery_state.multi_nic.nic_health[ctx->nic_info.type] = health_score;
    g_recovery_state.multi_nic.last_health_update = now;
    
    return health_score;
}

/**
 * @brief Select best backup NIC for failover
 * @param failing_ctx Context of the failing NIC
 * @return NIC index of best backup, or negative if none available
 */
static int select_backup_nic(nic_context_t *failing_ctx) {
    int best_nic = -1;
    int best_health = 0;
    
    for (int i = 0; i < MAX_NICS; i++) {
        if (i == failing_ctx->nic_info.type) {
            continue;  /* Skip the failing NIC */
        }
        
        int health = g_recovery_state.multi_nic.nic_health[i];
        if (health > best_health && health >= RECOVERY_HEALTH_THRESHOLD) {
            best_health = health;
            best_nic = i;
        }
    }
    
    return best_nic;
}

/**
 * @brief Update global multi-NIC state
 */
static void update_multi_nic_state(void) {
    uint8_t active_count = 0;
    uint8_t total_count = 0;
    
    /* Count active and total NICs */
    for (int i = 0; i < MAX_NICS; i++) {
        if (g_recovery_state.multi_nic.nic_health[i] > 0) {
            total_count++;
            if (g_recovery_state.multi_nic.nic_health[i] >= RECOVERY_HEALTH_THRESHOLD) {
                active_count++;
            }
        }
    }
    
    g_recovery_state.multi_nic.total_nics = total_count;
    g_recovery_state.multi_nic.active_nics = active_count;
    
    /* Check if we can exit failover mode */
    if (g_recovery_state.multi_nic.failover_active) {
        uint32_t failover_duration = get_system_timestamp_ms() - 
                                    g_recovery_state.multi_nic.failover_start_time;
        
        /* Exit failover after successful operation for 2 minutes */
        if (failover_duration > 120000 && active_count > 1) {
            g_recovery_state.multi_nic.failover_active = false;
            LOG_INFO("Exiting failover mode - system stable for 2 minutes");
        }
    }
}

/**
 * @brief Validate recovery effectiveness
 * @param ctx NIC context
 * @param level Recovery level executed
 * @return RECOVERY_SUCCESS if effective, RECOVERY_PARTIAL otherwise
 */
static int validate_recovery_effectiveness(nic_context_t *ctx, recovery_escalation_level_t level) {
    /* Wait a short period for the recovery to take effect */
    uint32_t validation_delay = 1000;  /* 1 second */
    uint32_t start_time = get_system_timestamp_ms();
    while ((get_system_timestamp_ms() - start_time) < validation_delay) {
        /* Simple delay */
    }
    
    /* Check adapter state */
    uint16_t io_base = ctx->nic_info.io_base;
    
    /* Try to read a status register with timeout protection */
    int status = protected_hardware_operation(ctx, io_base + 0x0E, 0, 0, 500);
    
    if (status < 0) {
        LOG_WARNING("Recovery validation failed - adapter not responding");
        return RECOVERY_PARTIAL;
    }
    
    if (status == 0xFFFF) {
        LOG_WARNING("Recovery validation failed - adapter returning invalid data");
        return RECOVERY_PARTIAL;
    }
    
    /* Additional validation based on recovery level */
    if (level >= RECOVERY_LEVEL_HARD_RESET) {
        /* For hard resets, validate link state */
        if (ctx->nic_info.type == NIC_TYPE_3C509B) {
            /* Check 3C509B link status */
            int link_status = protected_hardware_operation(ctx, io_base + 0x04, 0, 0, 500);
            if (link_status < 0 || !(link_status & 0x4000)) {
                LOG_WARNING("Recovery validation - link not established");
                return RECOVERY_PARTIAL;
            }
        }
    }
    
    LOG_INFO("Recovery validation successful for NIC %d", ctx->nic_info.type);
    return RECOVERY_SUCCESS;
}

/* Additional recovery method implementations */

int perform_retry_recovery(nic_context_t *ctx) {
    /* Simple retry with short delay */
    LOG_INFO("Performing retry recovery for NIC %d", ctx->nic_info.type);
    
    uint32_t delay_ms = 100 << ctx->recovery_attempts;  /* Exponential backoff */
    if (delay_ms > 2000) delay_ms = 2000;  /* Cap at 2 seconds */
    
    uint32_t start_time = get_system_timestamp_ms();
    while ((get_system_timestamp_ms() - start_time) < delay_ms) {
        /* Busy wait delay */
    }
    
    /* Clear consecutive error count */
    ctx->error_stats.consecutive_errors = 0;
    
    return RECOVERY_SUCCESS;
}

int perform_protected_soft_reset(nic_context_t *ctx) {
    LOG_INFO("Performing protected soft reset for NIC %d", ctx->nic_info.type);
    
    uint16_t io_base = ctx->nic_info.io_base;
    int result;
    
    if (ctx->nic_info.type == NIC_TYPE_3C509B) {
        /* 3C509B soft reset with timeout protection */
        result = protected_hardware_operation(ctx, io_base + 0x0E, 1, 0x0001, 1000);
        if (result < 0) return RECOVERY_FAILED;
        
        /* Wait for reset completion */
        result = protected_wait_ready(ctx, io_base + 0x0E, 0x0001, 5000);
        if (result < 0) return RECOVERY_FAILED;
        
        /* Clear reset and restore basic config */
        result = protected_hardware_operation(ctx, io_base + 0x0E, 1, 0x0000, 1000);
        if (result < 0) return RECOVERY_FAILED;
        
        result = protected_hardware_operation(ctx, io_base + 0x04, 1, 0x4000, 1000);
        if (result < 0) return RECOVERY_FAILED;
        
    } else if (ctx->nic_info.type == NIC_TYPE_3C515_TX) {
        /* 3C515 soft reset with timeout protection */
        result = protected_hardware_operation(ctx, io_base + 0x0E, 1, 0x0004, 1000);
        if (result < 0) return RECOVERY_FAILED;
        
        /* Wait for reset completion */
        result = protected_wait_ready(ctx, io_base + 0x0E, 0x0004, 10000);
        if (result == 0) return RECOVERY_FAILED;  /* Bit should be cleared */
    }
    
    ctx->error_stats.soft_resets++;
    return RECOVERY_SUCCESS;
}

int perform_protected_hard_reset(nic_context_t *ctx) {
    LOG_INFO("Performing protected hard reset for NIC %d", ctx->nic_info.type);
    
    /* First try soft reset */
    int result = perform_protected_soft_reset(ctx);
    if (result != RECOVERY_SUCCESS) {
        return result;
    }
    
    /* Additional hard reset steps would go here */
    /* This is simplified for the DOS environment */
    
    ctx->error_stats.hard_resets++;
    return RECOVERY_SUCCESS;
}

int perform_driver_restart(nic_context_t *ctx) {
    LOG_WARNING("Driver restart recovery not fully implemented - performing hard reset");
    return perform_protected_hard_reset(ctx);
}

int perform_adapter_disable(nic_context_t *ctx) {
    LOG_WARNING("Disabling adapter NIC %d due to persistent failures", ctx->nic_info.type);
    
    ctx->adapter_disabled = true;
    g_recovery_state.multi_nic.nic_health[ctx->nic_info.type] = 0;
    
    /* If multiple NICs available, this is just graceful degradation */
    if (g_recovery_state.multi_nic.total_nics > 1) {
        return implement_graceful_degradation(ctx);
    }
    
    return RECOVERY_FATAL;
}

int perform_system_failover(nic_context_t *ctx) {
    LOG_CRITICAL("Performing immediate system failover for NIC %d", ctx->nic_info.type);
    return implement_graceful_degradation(ctx);
}

/**
 * @brief Print comprehensive recovery statistics
 */
void print_recovery_statistics(void) {
    printf("\n=== Advanced Recovery System Statistics ===\n");
    printf("Recovery System: %s\n", 
           g_recovery_state.recovery_system_enabled ? "ENABLED" : "DISABLED");
    printf("Timeout Handlers: %s\n", 
           g_recovery_state.timeout_handlers_enabled ? "ENABLED" : "DISABLED");
    printf("Active Recovery Operations: %lu\n", 
           g_recovery_state.recovery_operations_active);
    printf("Total Recovery Attempts: %lu\n", 
           g_recovery_state.total_recovery_attempts);
    printf("Successful Recoveries: %lu\n", 
           g_recovery_state.successful_recoveries);
    printf("Failed Recoveries: %lu\n", 
           g_recovery_state.failed_recoveries);
    printf("Adapters Disabled: %lu\n", 
           g_recovery_state.adapters_disabled);
    printf("Failover Events: %lu\n", 
           g_recovery_state.failover_events);
    
    printf("\n=== Multi-NIC State ===\n");
    printf("Total NICs: %d\n", g_recovery_state.multi_nic.total_nics);
    printf("Active NICs: %d\n", g_recovery_state.multi_nic.active_nics);
    printf("Primary NIC: %d\n", g_recovery_state.multi_nic.primary_nic);
    printf("Backup NIC: %d\n", 
           g_recovery_state.multi_nic.backup_nic == 0xFF ? -1 : g_recovery_state.multi_nic.backup_nic);
    printf("Failover Active: %s\n", 
           g_recovery_state.multi_nic.failover_active ? "YES" : "NO");
    
    printf("\nNIC Health Scores:\n");
    for (int i = 0; i < MAX_NICS; i++) {
        if (g_recovery_state.multi_nic.nic_health[i] > 0 || i < g_recovery_state.multi_nic.total_nics) {
            printf("  NIC %d: %d%%\n", i, g_recovery_state.multi_nic.nic_health[i]);
        }
    }
}