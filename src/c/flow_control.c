/**
 * @file flow_control.c
 * @brief 802.3x Flow Control Implementation for 3Com Packet Driver
 * 
 * Sprint 2.3: 802.3x Flow Control Implementation
 * 
 * This module implements comprehensive 802.3x flow control (PAUSE frame) support
 * for the 3Com packet driver. The implementation provides software-based flow
 * control for ISA-generation NICs (3C515-TX and 3C509B) that lack hardware
 * PAUSE frame support.
 * 
 * Key Implementation Features:
 * - Complete PAUSE frame parsing and generation
 * - Timer-based transmission throttling mechanism
 * - Buffer monitoring with automatic PAUSE generation
 * - State machine for flow control lifecycle management
 * - Integration with interrupt mitigation and buffer management
 * - Comprehensive statistics collection and error handling
 * 
 * Hardware Integration:
 * - 3C515-TX: DMA integration with ring buffer monitoring
 * - 3C509B: PIO integration with FIFO monitoring
 * - Future-ready architecture for hardware PAUSE support
 * 
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 * This file is part of the 3Com Packet Driver project.
 */

#include "flow_control.h"
#include "logging.h"
#include "memory.h"
#include "enhanced_ring_context.h"
#include "nic_buffer_pools.h"
#include "timestamp.h"
#include <string.h>

/* ========================================================================== */
/* INTERNAL CONSTANTS AND DEFINITIONS                                       */
/* ========================================================================== */

/* Timing constants for DOS environment */
#define PAUSE_TIMER_TICK_MS              1     /* Timer tick resolution */
#define STATE_MACHINE_UPDATE_INTERVAL    10    /* State machine update interval */
#define BUFFER_CHECK_INTERVAL_MS         5     /* Buffer monitoring interval */

/* 3C515-TX specific flow control constants */
#define FLOW_CONTROL_3C515_RING_HIGH_WATERMARK    13  /* High watermark (13/16 descriptors) */
#define FLOW_CONTROL_3C515_RING_LOW_WATERMARK     8   /* Low watermark (8/16 descriptors) */

/* 3C509B specific flow control constants */
#define FLOW_CONTROL_3C509B_FIFO_HIGH_WATERMARK   85  /* 85% FIFO usage */
#define FLOW_CONTROL_3C509B_FIFO_LOW_WATERMARK    60  /* 60% FIFO usage */

/* Error recovery constants */
#define MAX_ERROR_RECOVERY_ATTEMPTS       3    /* Maximum error recovery attempts */
#define ERROR_RECOVERY_TIMEOUT_MS         1000 /* Error recovery timeout */

/* PAUSE frame destination MAC address */
static const uint8_t PAUSE_DEST_MAC[6] = PAUSE_FRAME_DEST_MAC;

/* ========================================================================== */
/* INTERNAL FUNCTION DECLARATIONS                                           */
/* ========================================================================== */

/* State machine internal functions */
static int flow_control_state_machine_update(flow_control_context_t *ctx);
static int flow_control_handle_state_disabled(flow_control_context_t *ctx);
static int flow_control_handle_state_idle(flow_control_context_t *ctx);
static int flow_control_handle_state_pause_requested(flow_control_context_t *ctx);
static int flow_control_handle_state_pause_active(flow_control_context_t *ctx);
static int flow_control_handle_state_resume_pending(flow_control_context_t *ctx);
static int flow_control_handle_state_error(flow_control_context_t *ctx);

/* Internal helper functions */
static bool flow_control_is_pause_frame(const uint8_t *packet, uint16_t length);
static int flow_control_update_pause_timer(flow_control_context_t *ctx);
static int flow_control_get_nic_buffer_usage(flow_control_context_t *ctx);
static int flow_control_send_pause_frame_internal(flow_control_context_t *ctx, uint16_t pause_time);
static void flow_control_update_statistics(flow_control_context_t *ctx, const char *event);
static int flow_control_validate_context(const flow_control_context_t *ctx);

/* Hardware-specific functions */
static int flow_control_get_3c515_buffer_usage(flow_control_context_t *ctx);
static int flow_control_get_3c509b_buffer_usage(flow_control_context_t *ctx);

/* ========================================================================== */
/* PUBLIC API IMPLEMENTATION                                                */
/* ========================================================================== */

/**
 * @brief Initialize flow control subsystem
 */
int flow_control_init(flow_control_context_t *ctx, 
                      nic_context_t *nic_ctx,
                      const flow_control_config_t *config) {
    if (!ctx || !nic_ctx) {
        LOG_ERROR("Invalid parameters for flow control initialization");
        return FLOW_CONTROL_INVALID_PARAM;
    }
    
    LOG_INFO("Initializing 802.3x flow control subsystem");
    
    /* Clear the context structure */
    memset(ctx, 0, sizeof(flow_control_context_t));
    
    /* Set up basic context */
    ctx->nic_ctx = nic_ctx;
    ctx->state = FLOW_CONTROL_STATE_DISABLED;
    ctx->last_state_change_time = get_timestamp_ms();
    ctx->last_buffer_check_time = get_timestamp_ms();
    
    /* Apply configuration or use defaults */
    if (config) {
        ctx->config = *config;
    } else {
        flow_control_get_default_config(nic_ctx->info->nic_type, &ctx->config);
    }
    
    /* Detect hardware capabilities */
    ctx->config.capabilities = flow_control_detect_capabilities(nic_ctx);
    
    LOG_DEBUG("Flow control capabilities detected: 0x%04X", ctx->config.capabilities);
    
    /* Initialize based on NIC type */
    switch (nic_ctx->info->nic_type) {
        case NIC_TYPE_3C515:
            LOG_INFO("Configuring flow control for 3C515-TX (DMA/Ring buffers)");
            break;
            
        case NIC_TYPE_3C509B:
            LOG_INFO("Configuring flow control for 3C509B (PIO/FIFO)");
            break;
            
        default:
            LOG_WARNING("Unknown NIC type for flow control: %d", nic_ctx->info->nic_type);
            break;
    }
    
    /* Set initial state based on configuration */
    if (ctx->config.enabled) {
        ctx->state = FLOW_CONTROL_STATE_IDLE;
        LOG_INFO("Flow control enabled and ready");
    } else {
        LOG_INFO("Flow control disabled by configuration");
    }
    
    ctx->initialized = true;
    
    LOG_INFO("802.3x flow control initialization completed successfully");
    return FLOW_CONTROL_SUCCESS;
}

/**
 * @brief Cleanup flow control resources
 */
void flow_control_cleanup(flow_control_context_t *ctx) {
    if (!ctx || !ctx->initialized) {
        return;
    }
    
    LOG_INFO("Cleaning up 802.3x flow control subsystem");
    
    /* Force resume any active pause */
    if (FLOW_CONTROL_IS_ACTIVE(ctx)) {
        flow_control_force_resume_transmission(ctx);
    }
    
    /* Log final statistics */
    LOG_INFO("Flow control final statistics:");
    LOG_INFO("  PAUSE frames received: %lu", ctx->stats.pause_frames_received);
    LOG_INFO("  PAUSE frames sent: %lu", ctx->stats.pause_frames_sent);
    LOG_INFO("  Flow control activations: %lu", ctx->stats.flow_control_activations);
    LOG_INFO("  Total pause time: %lu ms", ctx->stats.total_pause_time_ms);
    
    /* Clear context */
    ctx->initialized = false;
    ctx->state = FLOW_CONTROL_STATE_DISABLED;
    
    LOG_INFO("Flow control cleanup completed");
}

/**
 * @brief Reset flow control state
 */
int flow_control_reset(flow_control_context_t *ctx) {
    if (flow_control_validate_context(ctx) != FLOW_CONTROL_SUCCESS) {
        return FLOW_CONTROL_INVALID_PARAM;
    }
    
    LOG_DEBUG("Resetting flow control state");
    
    /* Reset state machine */
    ctx->state = ctx->config.enabled ? FLOW_CONTROL_STATE_IDLE : FLOW_CONTROL_STATE_DISABLED;
    ctx->pause_duration_remaining = 0;
    ctx->pause_start_time = 0;
    ctx->last_pause_time_received = 0;
    ctx->high_watermark_reached = false;
    ctx->error_recovery_attempts = 0;
    ctx->last_state_change_time = get_timestamp_ms();
    
    LOG_DEBUG("Flow control state reset completed");
    return FLOW_CONTROL_SUCCESS;
}

/**
 * @brief Enable or disable flow control
 */
int flow_control_set_enabled(flow_control_context_t *ctx, bool enabled) {
    if (flow_control_validate_context(ctx) != FLOW_CONTROL_SUCCESS) {
        return FLOW_CONTROL_INVALID_PARAM;
    }
    
    if (ctx->config.enabled == enabled) {
        return FLOW_CONTROL_SUCCESS; /* No change needed */
    }
    
    LOG_INFO("Flow control %s", enabled ? "enabled" : "disabled");
    
    ctx->config.enabled = enabled;
    
    if (enabled) {
        ctx->state = FLOW_CONTROL_STATE_IDLE;
    } else {
        /* Force resume any active pause */
        if (FLOW_CONTROL_IS_ACTIVE(ctx)) {
            flow_control_force_resume_transmission(ctx);
        }
        ctx->state = FLOW_CONTROL_STATE_DISABLED;
    }
    
    ctx->last_state_change_time = get_timestamp_ms();
    return FLOW_CONTROL_SUCCESS;
}

/**
 * @brief Check if flow control is enabled
 */
bool flow_control_is_enabled(const flow_control_context_t *ctx) {
    return ctx && ctx->initialized && ctx->config.enabled;
}

/* ========================================================================== */
/* PAUSE FRAME PROCESSING IMPLEMENTATION                                    */
/* ========================================================================== */

/**
 * @brief Process received packet for PAUSE frame detection
 */
int flow_control_process_received_packet(flow_control_context_t *ctx,
                                        const uint8_t *packet,
                                        uint16_t length) {
    pause_frame_t pause_frame;
    int result;
    
    if (flow_control_validate_context(ctx) != FLOW_CONTROL_SUCCESS || !packet) {
        return FLOW_CONTROL_INVALID_PARAM;
    }
    
    /* Check if flow control is enabled and can process PAUSE frames */
    if (!ctx->config.enabled || !ctx->config.rx_pause_enabled) {
        return 0; /* Not processing PAUSE frames */
    }
    
    /* Quick check if this could be a PAUSE frame */
    if (!flow_control_is_pause_frame(packet, length)) {
        return 0; /* Not a PAUSE frame */
    }
    
    /* Parse the PAUSE frame */
    result = flow_control_parse_pause_frame(packet, length, &pause_frame);
    if (result <= 0) {
        if (result < 0) {
            ctx->stats.invalid_pause_frames++;
            LOG_DEBUG("Invalid PAUSE frame received");
        }
        return result;
    }
    
    LOG_TRACE("PAUSE frame received: pause_time=%d quanta", pause_frame.pause_time);
    
    /* Update statistics */
    ctx->stats.pause_frames_received++;
    ctx->last_pause_time_received = pause_frame.pause_time;
    ctx->partner_last_pause_time = get_timestamp_ms();
    ctx->partner_supports_flow_control = true;
    
    /* Process the PAUSE request */
    if (pause_frame.pause_time > 0) {
        /* PAUSE request */
        uint32_t pause_duration_ms = flow_control_quanta_to_ms(pause_frame.pause_time, 
                                                             ctx->nic_ctx->speed);
        
        /* Limit pause duration for safety */
        if (pause_duration_ms > ctx->config.max_pause_duration_ms) {
            LOG_WARNING("PAUSE duration %lu ms exceeds maximum %lu ms, limiting", 
                       pause_duration_ms, ctx->config.max_pause_duration_ms);
            pause_duration_ms = ctx->config.max_pause_duration_ms;
        }
        
        /* Update flow control state */
        if (ctx->state == FLOW_CONTROL_STATE_IDLE || 
            ctx->state == FLOW_CONTROL_STATE_RESUME_PENDING) {
            ctx->state = FLOW_CONTROL_STATE_PAUSE_REQUESTED;
            ctx->stats.flow_control_activations++;
            flow_control_update_statistics(ctx, "pause_requested");
        }
        
        ctx->pause_duration_remaining = pause_duration_ms;
        ctx->pause_start_time = get_timestamp_ms();
        ctx->last_state_change_time = get_timestamp_ms();
        
        LOG_DEBUG("Transmission paused for %lu ms", pause_duration_ms);
    } else {
        /* PAUSE resume (pause_time = 0) */
        if (FLOW_CONTROL_IS_ACTIVE(ctx)) {
            ctx->state = FLOW_CONTROL_STATE_RESUME_PENDING;
            ctx->pause_duration_remaining = 0;
            ctx->stats.flow_control_deactivations++;
            flow_control_update_statistics(ctx, "pause_resume");
            LOG_DEBUG("PAUSE resume received");
        }
    }
    
    return 1; /* PAUSE frame processed */
}

/**
 * @brief Parse PAUSE frame from packet data
 */
int flow_control_parse_pause_frame(const uint8_t *packet,
                                  uint16_t length,
                                  pause_frame_t *pause_frame) {
    const uint8_t *payload;
    
    if (!packet || !pause_frame || length < sizeof(pause_frame_t)) {
        return FLOW_CONTROL_INVALID_PARAM;
    }
    
    /* Check minimum frame size */
    if (length < PAUSE_FRAME_MIN_SIZE) {
        return FLOW_CONTROL_PARSE_ERROR;
    }
    
    /* Check EtherType for MAC Control frames */
    uint16_t ethertype = (packet[12] << 8) | packet[13];
    if (ethertype != FLOW_CONTROL_ETHERTYPE) {
        return 0; /* Not a MAC control frame */
    }
    
    /* Parse MAC Control payload */
    payload = packet + 14; /* Skip Ethernet header */
    uint16_t opcode = (payload[0] << 8) | payload[1];
    
    if (opcode != PAUSE_FRAME_OPCODE) {
        return 0; /* Not a PAUSE frame */
    }
    
    /* Extract PAUSE frame data */
    memcpy(pause_frame->dest_mac, packet, 6);
    memcpy(pause_frame->src_mac, packet + 6, 6);
    pause_frame->ethertype = ethertype;
    pause_frame->opcode = opcode;
    pause_frame->pause_time = (payload[2] << 8) | payload[3];
    
    /* Validate PAUSE frame structure */
    if (!flow_control_validate_pause_frame(pause_frame)) {
        return FLOW_CONTROL_PARSE_ERROR;
    }
    
    return 1; /* Valid PAUSE frame parsed */
}

/**
 * @brief Generate PAUSE frame
 */
int flow_control_generate_pause_frame(flow_control_context_t *ctx,
                                     uint16_t pause_time,
                                     uint8_t *frame_buffer,
                                     uint16_t buffer_size) {
    pause_frame_t *frame;
    
    if (flow_control_validate_context(ctx) != FLOW_CONTROL_SUCCESS || 
        !frame_buffer || buffer_size < PAUSE_FRAME_MIN_SIZE) {
        return FLOW_CONTROL_INVALID_PARAM;
    }
    
    /* Clear frame buffer */
    memset(frame_buffer, 0, buffer_size);
    frame = (pause_frame_t *)frame_buffer;
    
    /* Build PAUSE frame */
    memcpy(frame->dest_mac, PAUSE_DEST_MAC, 6);
    memcpy(frame->src_mac, ctx->nic_ctx->mac, 6);
    frame->ethertype = htons(FLOW_CONTROL_ETHERTYPE);
    frame->opcode = htons(PAUSE_FRAME_OPCODE);
    frame->pause_time = htons(pause_time);
    
    /* Padding is already zeroed by memset */
    
    LOG_TRACE("Generated PAUSE frame: pause_time=%d quanta", pause_time);
    
    return PAUSE_FRAME_MIN_SIZE;
}

/**
 * @brief Send PAUSE frame to request transmission pause
 */
int flow_control_send_pause_frame(flow_control_context_t *ctx, uint16_t pause_time) {
    if (flow_control_validate_context(ctx) != FLOW_CONTROL_SUCCESS) {
        return FLOW_CONTROL_INVALID_PARAM;
    }
    
    if (!ctx->config.enabled || !ctx->config.tx_pause_enabled) {
        return FLOW_CONTROL_NOT_SUPPORTED;
    }
    
    return flow_control_send_pause_frame_internal(ctx, pause_time);
}

/* ========================================================================== */
/* TRANSMISSION CONTROL IMPLEMENTATION                                      */
/* ========================================================================== */

/**
 * @brief Check if transmission should be paused
 */
bool flow_control_should_pause_transmission(const flow_control_context_t *ctx) {
    if (!ctx || !ctx->initialized || !ctx->config.enabled) {
        return false;
    }
    
    return FLOW_CONTROL_IS_ACTIVE(ctx) && ctx->pause_duration_remaining > 0;
}

/**
 * @brief Process transmission request (called before each packet transmission)
 */
int flow_control_process_transmission_request(flow_control_context_t *ctx) {
    if (flow_control_validate_context(ctx) != FLOW_CONTROL_SUCCESS) {
        return FLOW_CONTROL_INVALID_PARAM;
    }
    
    /* Update flow control state */
    flow_control_update_timer_state(ctx);
    
    /* Check if transmission should be paused */
    if (flow_control_should_pause_transmission(ctx)) {
        return 1; /* Transmission paused */
    }
    
    return 0; /* Transmission allowed */
}

/**
 * @brief Update flow control state based on timer expiration
 */
int flow_control_update_timer_state(flow_control_context_t *ctx) {
    uint32_t current_time, elapsed_time;
    
    if (flow_control_validate_context(ctx) != FLOW_CONTROL_SUCCESS) {
        return FLOW_CONTROL_INVALID_PARAM;
    }
    
    current_time = get_timestamp_ms();
    
    /* Update pause timer if active */
    if (FLOW_CONTROL_IS_ACTIVE(ctx) && ctx->pause_duration_remaining > 0) {
        elapsed_time = current_time - ctx->pause_start_time;
        
        if (elapsed_time >= ctx->pause_duration_remaining) {
            /* Pause timer expired */
            ctx->pause_duration_remaining = 0;
            ctx->state = FLOW_CONTROL_STATE_RESUME_PENDING;
            ctx->last_state_change_time = current_time;
            ctx->stats.transmission_resumes++;
            flow_control_update_statistics(ctx, "pause_expired");
            
            LOG_DEBUG("PAUSE timer expired, resuming transmission");
        } else {
            ctx->pause_duration_remaining = ctx->pause_duration_remaining - elapsed_time;
            ctx->pause_start_time = current_time;
        }
    }
    
    /* Check for timeout conditions */
    if (FLOW_CONTROL_TIME_IN_STATE(ctx) > FLOW_CONTROL_TIMEOUT_MS) {
        LOG_WARNING("Flow control state timeout, forcing reset");
        ctx->stats.pause_timeout_events++;
        flow_control_reset(ctx);
    }
    
    return FLOW_CONTROL_SUCCESS;
}

/**
 * @brief Force resume transmission (emergency override)
 */
int flow_control_force_resume_transmission(flow_control_context_t *ctx) {
    if (flow_control_validate_context(ctx) != FLOW_CONTROL_SUCCESS) {
        return FLOW_CONTROL_INVALID_PARAM;
    }
    
    LOG_DEBUG("Forcing transmission resume");
    
    if (FLOW_CONTROL_IS_ACTIVE(ctx)) {
        ctx->pause_duration_remaining = 0;
        ctx->state = FLOW_CONTROL_STATE_IDLE;
        ctx->last_state_change_time = get_timestamp_ms();
        ctx->stats.transmission_resumes++;
        flow_control_update_statistics(ctx, "forced_resume");
    }
    
    return FLOW_CONTROL_SUCCESS;
}

/* ========================================================================== */
/* BUFFER MONITORING IMPLEMENTATION                                         */
/* ========================================================================== */

/**
 * @brief Monitor buffer levels and generate PAUSE frames if needed
 */
int flow_control_monitor_buffer_levels(flow_control_context_t *ctx) {
    uint32_t current_time;
    int buffer_usage;
    
    if (flow_control_validate_context(ctx) != FLOW_CONTROL_SUCCESS) {
        return FLOW_CONTROL_INVALID_PARAM;
    }
    
    current_time = get_timestamp_ms();
    
    /* Check if it's time for buffer monitoring */
    if ((current_time - ctx->last_buffer_check_time) < BUFFER_CHECK_INTERVAL_MS) {
        return FLOW_CONTROL_SUCCESS;
    }
    
    ctx->last_buffer_check_time = current_time;
    
    /* Get current buffer usage */
    buffer_usage = flow_control_get_buffer_usage_percent(ctx);
    if (buffer_usage < 0) {
        return buffer_usage; /* Error */
    }
    
    ctx->current_buffer_usage_percent = (uint16_t)buffer_usage;
    
    /* Check thresholds */
    if (buffer_usage >= ctx->config.high_watermark_percent) {
        if (!ctx->high_watermark_reached) {
            ctx->high_watermark_reached = true;
            ctx->stats.buffer_watermark_triggers++;
            
            /* Send PAUSE frame if enabled */
            if (ctx->config.enabled && ctx->config.tx_pause_enabled) {
                int result = flow_control_send_pause_frame_internal(ctx, ctx->config.pause_time_default);
                if (result == FLOW_CONTROL_SUCCESS) {
                    ctx->stats.buffer_overflow_prevented++;
                    LOG_DEBUG("PAUSE frame sent due to high buffer usage: %d%%", buffer_usage);
                }
            }
        }
        
        /* Check for emergency threshold */
        if (buffer_usage >= FLOW_CONTROL_EMERGENCY_THRESHOLD) {
            flow_control_trigger_emergency_pause(ctx);
        }
    } else if (buffer_usage <= ctx->config.low_watermark_percent) {
        if (ctx->high_watermark_reached) {
            ctx->high_watermark_reached = false;
            
            /* Send resume (pause_time = 0) if we were sending PAUSE frames */
            if (ctx->config.enabled && ctx->config.tx_pause_enabled) {
                flow_control_send_pause_frame_internal(ctx, 0);
                LOG_DEBUG("PAUSE resume sent due to low buffer usage: %d%%", buffer_usage);
            }
        }
    }
    
    return FLOW_CONTROL_SUCCESS;
}

/**
 * @brief Get current buffer usage percentage
 */
int flow_control_get_buffer_usage_percent(flow_control_context_t *ctx) {
    if (flow_control_validate_context(ctx) != FLOW_CONTROL_SUCCESS) {
        return FLOW_CONTROL_INVALID_PARAM;
    }
    
    return flow_control_get_nic_buffer_usage(ctx);
}

/**
 * @brief Check if high watermark threshold is reached
 */
bool flow_control_is_high_watermark_reached(const flow_control_context_t *ctx) {
    return ctx && ctx->initialized && ctx->high_watermark_reached;
}

/**
 * @brief Trigger emergency PAUSE frame generation
 */
int flow_control_trigger_emergency_pause(flow_control_context_t *ctx) {
    if (flow_control_validate_context(ctx) != FLOW_CONTROL_SUCCESS) {
        return FLOW_CONTROL_INVALID_PARAM;
    }
    
    LOG_WARNING("Emergency PAUSE triggered - buffer usage critical");
    
    ctx->stats.emergency_pause_events++;
    
    if (ctx->config.enabled && ctx->config.tx_pause_enabled) {
        /* Send maximum pause time */
        return flow_control_send_pause_frame_internal(ctx, MAX_PAUSE_QUANTA);
    }
    
    return FLOW_CONTROL_SUCCESS;
}

/* ========================================================================== */
/* STATE MACHINE IMPLEMENTATION                                             */
/* ========================================================================== */

/**
 * @brief Get current flow control state
 */
flow_control_state_t flow_control_get_state(const flow_control_context_t *ctx) {
    return ctx && ctx->initialized ? ctx->state : FLOW_CONTROL_STATE_DISABLED;
}

/**
 * @brief Convert flow control state to string
 */
const char* flow_control_state_to_string(flow_control_state_t state) {
    switch (state) {
        case FLOW_CONTROL_STATE_DISABLED:      return "DISABLED";
        case FLOW_CONTROL_STATE_IDLE:          return "IDLE";
        case FLOW_CONTROL_STATE_PAUSE_REQUESTED: return "PAUSE_REQUESTED";
        case FLOW_CONTROL_STATE_PAUSE_ACTIVE:  return "PAUSE_ACTIVE";
        case FLOW_CONTROL_STATE_RESUME_PENDING: return "RESUME_PENDING";
        case FLOW_CONTROL_STATE_ERROR:         return "ERROR";
        default:                               return "UNKNOWN";
    }
}

/**
 * @brief Process flow control state machine
 */
int flow_control_process_state_machine(flow_control_context_t *ctx) {
    if (flow_control_validate_context(ctx) != FLOW_CONTROL_SUCCESS) {
        return FLOW_CONTROL_INVALID_PARAM;
    }
    
    return flow_control_state_machine_update(ctx);
}

/**
 * @brief Handle state transition
 */
int flow_control_transition_state(flow_control_context_t *ctx,
                                 flow_control_state_t new_state) {
    if (flow_control_validate_context(ctx) != FLOW_CONTROL_SUCCESS) {
        return FLOW_CONTROL_INVALID_PARAM;
    }
    
    if (ctx->state == new_state) {
        return FLOW_CONTROL_SUCCESS; /* No change needed */
    }
    
    LOG_TRACE("Flow control state transition: %s -> %s",
              flow_control_state_to_string(ctx->state),
              flow_control_state_to_string(new_state));
    
    ctx->state = new_state;
    ctx->last_state_change_time = get_timestamp_ms();
    
    return FLOW_CONTROL_SUCCESS;
}

/* ========================================================================== */
/* STATISTICS AND MONITORING IMPLEMENTATION                                 */
/* ========================================================================== */

/**
 * @brief Get flow control statistics
 */
int flow_control_get_statistics(const flow_control_context_t *ctx,
                               flow_control_stats_t *stats) {
    if (!ctx || !stats) {
        return FLOW_CONTROL_INVALID_PARAM;
    }
    
    *stats = ctx->stats;
    
    /* Update calculated statistics */
    if (ctx->stats.flow_control_activations > 0) {
        stats->avg_pause_duration_ms = ctx->stats.total_pause_time_ms / ctx->stats.flow_control_activations;
    }
    
    return FLOW_CONTROL_SUCCESS;
}

/**
 * @brief Clear flow control statistics
 */
void flow_control_clear_statistics(flow_control_context_t *ctx) {
    if (ctx && ctx->initialized) {
        memset(&ctx->stats, 0, sizeof(flow_control_stats_t));
        LOG_DEBUG("Flow control statistics cleared");
    }
}

/**
 * @brief Get flow control performance metrics
 */
int flow_control_get_performance_metrics(const flow_control_context_t *ctx,
                                        uint32_t *avg_pause_duration_ms,
                                        uint32_t *pause_efficiency_percent,
                                        uint32_t *buffer_overflow_prevention_rate) {
    if (!ctx || !avg_pause_duration_ms || !pause_efficiency_percent || !buffer_overflow_prevention_rate) {
        return FLOW_CONTROL_INVALID_PARAM;
    }
    
    /* Calculate average pause duration */
    if (ctx->stats.flow_control_activations > 0) {
        *avg_pause_duration_ms = ctx->stats.total_pause_time_ms / ctx->stats.flow_control_activations;
    } else {
        *avg_pause_duration_ms = 0;
    }
    
    /* Calculate pause efficiency (successful deactivations / activations) */
    if (ctx->stats.flow_control_activations > 0) {
        *pause_efficiency_percent = (ctx->stats.flow_control_deactivations * 100) / 
                                   ctx->stats.flow_control_activations;
    } else {
        *pause_efficiency_percent = 100;
    }
    
    /* Calculate buffer overflow prevention rate */
    if (ctx->stats.buffer_watermark_triggers > 0) {
        *buffer_overflow_prevention_rate = (ctx->stats.buffer_overflow_prevented * 100) /
                                          ctx->stats.buffer_watermark_triggers;
    } else {
        *buffer_overflow_prevention_rate = 0;
    }
    
    return FLOW_CONTROL_SUCCESS;
}

/* ========================================================================== */
/* CONFIGURATION MANAGEMENT IMPLEMENTATION                                  */
/* ========================================================================== */

/**
 * @brief Get current flow control configuration
 */
int flow_control_get_config(const flow_control_context_t *ctx,
                           flow_control_config_t *config) {
    if (!ctx || !config) {
        return FLOW_CONTROL_INVALID_PARAM;
    }
    
    *config = ctx->config;
    return FLOW_CONTROL_SUCCESS;
}

/**
 * @brief Update flow control configuration
 */
int flow_control_set_config(flow_control_context_t *ctx,
                           const flow_control_config_t *config) {
    if (flow_control_validate_context(ctx) != FLOW_CONTROL_SUCCESS || !config) {
        return FLOW_CONTROL_INVALID_PARAM;
    }
    
    /* Validate configuration */
    if (config->high_watermark_percent <= config->low_watermark_percent ||
        config->high_watermark_percent > 100 ||
        config->max_pause_duration_ms == 0) {
        return FLOW_CONTROL_INVALID_PARAM;
    }
    
    /* Apply new configuration */
    bool was_enabled = ctx->config.enabled;
    ctx->config = *config;
    
    /* Handle enable/disable state change */
    if (was_enabled != config->enabled) {
        flow_control_set_enabled(ctx, config->enabled);
    }
    
    LOG_DEBUG("Flow control configuration updated");
    return FLOW_CONTROL_SUCCESS;
}

/**
 * @brief Get default flow control configuration
 */
int flow_control_get_default_config(nic_type_t nic_type,
                                   flow_control_config_t *config) {
    if (!config) {
        return FLOW_CONTROL_INVALID_PARAM;
    }
    
    /* Set default configuration */
    config->enabled = true;
    config->rx_pause_enabled = true;
    config->tx_pause_enabled = true;
    config->auto_negotiate = true;
    config->pause_time_default = DEFAULT_PAUSE_TIME;
    config->high_watermark_percent = FLOW_CONTROL_HIGH_WATERMARK;
    config->low_watermark_percent = FLOW_CONTROL_LOW_WATERMARK;
    config->max_pause_duration_ms = MAX_PAUSE_DURATION_MS;
    
    /* Set capabilities based on NIC type */
    switch (nic_type) {
        case NIC_TYPE_3C515:
            config->capabilities = FLOW_CONTROL_CAP_RX_PAUSE | FLOW_CONTROL_CAP_TX_PAUSE;
            break;
            
        case NIC_TYPE_3C509B:
            config->capabilities = FLOW_CONTROL_CAP_RX_PAUSE | FLOW_CONTROL_CAP_TX_PAUSE;
            break;
            
        default:
            config->capabilities = FLOW_CONTROL_CAP_NONE;
            config->enabled = false;
            break;
    }
    
    return FLOW_CONTROL_SUCCESS;
}

/* ========================================================================== */
/* CAPABILITY DETECTION IMPLEMENTATION                                      */
/* ========================================================================== */

/**
 * @brief Detect flow control capabilities for NIC
 */
flow_control_capabilities_t flow_control_detect_capabilities(const nic_context_t *nic_ctx) {
    if (!nic_ctx) {
        return FLOW_CONTROL_CAP_NONE;
    }
    
    /* ISA-generation NICs don't have hardware flow control support */
    /* All flow control is implemented in software */
    
    switch (nic_ctx->info->nic_type) {
        case NIC_TYPE_3C515:
            /* 3C515-TX can do software flow control with DMA integration */
            return FLOW_CONTROL_CAP_RX_PAUSE | FLOW_CONTROL_CAP_TX_PAUSE | FLOW_CONTROL_CAP_SYMMETRIC;
            
        case NIC_TYPE_3C509B:
            /* 3C509B can do software flow control with PIO integration */
            return FLOW_CONTROL_CAP_RX_PAUSE | FLOW_CONTROL_CAP_TX_PAUSE | FLOW_CONTROL_CAP_SYMMETRIC;
            
        default:
            return FLOW_CONTROL_CAP_NONE;
    }
}

/**
 * @brief Check if partner supports flow control
 */
bool flow_control_partner_supports_flow_control(const flow_control_context_t *ctx) {
    return ctx && ctx->initialized && ctx->partner_supports_flow_control;
}

/**
 * @brief Negotiate flow control with partner
 */
int flow_control_negotiate_with_partner(flow_control_context_t *ctx) {
    if (flow_control_validate_context(ctx) != FLOW_CONTROL_SUCCESS) {
        return FLOW_CONTROL_INVALID_PARAM;
    }
    
    /* For ISA NICs, flow control negotiation is typically done by sending
     * test PAUSE frames and waiting for responses, or through manual configuration */
    
    LOG_DEBUG("Flow control negotiation not implemented for ISA NICs");
    return FLOW_CONTROL_NOT_SUPPORTED;
}

/* ========================================================================== */
/* INTEGRATION FUNCTIONS IMPLEMENTATION                                     */
/* ========================================================================== */

/**
 * @brief Integration with interrupt mitigation system
 */
int flow_control_integrate_interrupt_mitigation(flow_control_context_t *ctx,
                                               interrupt_mitigation_context_t *im_ctx) {
    if (flow_control_validate_context(ctx) != FLOW_CONTROL_SUCCESS || !im_ctx) {
        return FLOW_CONTROL_INVALID_PARAM;
    }
    
    ctx->im_ctx = im_ctx;
    LOG_DEBUG("Flow control integrated with interrupt mitigation");
    return FLOW_CONTROL_SUCCESS;
}

/**
 * @brief Process flow control during interrupt handling
 */
int flow_control_process_interrupt_event(flow_control_context_t *ctx,
                                        interrupt_event_type_t event_type) {
    if (flow_control_validate_context(ctx) != FLOW_CONTROL_SUCCESS) {
        return FLOW_CONTROL_INVALID_PARAM;
    }
    
    /* Process flow control based on interrupt event type */
    switch (event_type) {
        case EVENT_TYPE_RX_COMPLETE:
            /* Check if received packet was a PAUSE frame - this would be handled
             * by the main packet processing path */
            break;
            
        case EVENT_TYPE_TX_COMPLETE:
            /* Check if we can resume transmission */
            if (ctx->state == FLOW_CONTROL_STATE_RESUME_PENDING) {
                flow_control_transition_state(ctx, FLOW_CONTROL_STATE_IDLE);
            }
            break;
            
        case EVENT_TYPE_RX_ERROR:
        case EVENT_TYPE_TX_ERROR:
            /* Error handling - may need to reset flow control state */
            ctx->stats.flow_control_errors++;
            break;
            
        default:
            break;
    }
    
    /* Update buffer monitoring during interrupt processing */
    flow_control_monitor_buffer_levels(ctx);
    
    return FLOW_CONTROL_SUCCESS;
}

/**
 * @brief Periodic flow control maintenance (called from timer/scheduler)
 */
int flow_control_periodic_maintenance(flow_control_context_t *ctx) {
    if (flow_control_validate_context(ctx) != FLOW_CONTROL_SUCCESS) {
        return FLOW_CONTROL_INVALID_PARAM;
    }
    
    /* Update timer state */
    flow_control_update_timer_state(ctx);
    
    /* Process state machine */
    flow_control_process_state_machine(ctx);
    
    /* Monitor buffer levels */
    flow_control_monitor_buffer_levels(ctx);
    
    return FLOW_CONTROL_SUCCESS;
}

/* ========================================================================== */
/* UTILITY FUNCTIONS IMPLEMENTATION                                         */
/* ========================================================================== */

/**
 * @brief Validate PAUSE frame structure
 */
bool flow_control_validate_pause_frame(const pause_frame_t *pause_frame) {
    if (!pause_frame) {
        return false;
    }
    
    /* Check EtherType */
    if (ntohs(pause_frame->ethertype) != FLOW_CONTROL_ETHERTYPE) {
        return false;
    }
    
    /* Check opcode */
    if (ntohs(pause_frame->opcode) != PAUSE_FRAME_OPCODE) {
        return false;
    }
    
    /* Check destination MAC (should be PAUSE multicast address) */
    if (memcmp(pause_frame->dest_mac, PAUSE_DEST_MAC, 6) != 0) {
        return false;
    }
    
    return true;
}

/**
 * @brief Convert pause time from quanta to milliseconds
 */
uint32_t flow_control_quanta_to_ms(uint16_t pause_quanta, uint32_t link_speed_mbps) {
    if (link_speed_mbps == 0) {
        link_speed_mbps = 10; /* Default to 10 Mbps */
    }
    
    /* Each pause quanta = 512 bit times */
    /* Conversion: (pause_quanta * 512 * 1000) / (link_speed_mbps * 1000000) */
    uint32_t bit_times = (uint32_t)pause_quanta * PAUSE_QUANTA_UNIT_BIT_TIMES;
    uint32_t ms = (bit_times) / (link_speed_mbps * 1000);
    
    return ms > 0 ? ms : 1; /* Minimum 1 ms */
}

/**
 * @brief Convert pause time from milliseconds to quanta
 */
uint16_t flow_control_ms_to_quanta(uint32_t pause_ms, uint32_t link_speed_mbps) {
    if (link_speed_mbps == 0) {
        link_speed_mbps = 10; /* Default to 10 Mbps */
    }
    
    /* Convert ms to bit times, then to quanta */
    uint32_t bit_times = pause_ms * link_speed_mbps * 1000;
    uint32_t quanta = bit_times / PAUSE_QUANTA_UNIT_BIT_TIMES;
    
    return (quanta > MAX_PAUSE_QUANTA) ? MAX_PAUSE_QUANTA : (uint16_t)quanta;
}

/**
 * @brief Self-test flow control functionality
 */
int flow_control_self_test(void) {
    LOG_INFO("Running flow control self-test");
    
    /* Test PAUSE frame parsing */
    uint8_t test_pause_frame[64] = {
        /* Destination MAC: 01:80:C2:00:00:01 */
        0x01, 0x80, 0xC2, 0x00, 0x00, 0x01,
        /* Source MAC: 00:11:22:33:44:55 */
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55,
        /* EtherType: 0x8808 */
        0x88, 0x08,
        /* Opcode: 0x0001 */
        0x00, 0x01,
        /* Pause time: 0x0100 */
        0x01, 0x00,
        /* Padding (42 bytes of zeros) */
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00
    };
    
    pause_frame_t parsed_frame;
    int result = flow_control_parse_pause_frame(test_pause_frame, sizeof(test_pause_frame), &parsed_frame);
    
    if (result != 1) {
        LOG_ERROR("PAUSE frame parsing test failed: %d", result);
        return FLOW_CONTROL_ERROR;
    }
    
    if (ntohs(parsed_frame.pause_time) != 0x0100) {
        LOG_ERROR("PAUSE frame pause time parsing failed: expected 0x0100, got 0x%04X", 
                  ntohs(parsed_frame.pause_time));
        return FLOW_CONTROL_ERROR;
    }
    
    /* Test time conversion functions */
    uint16_t test_quanta = 256;
    uint32_t ms = flow_control_quanta_to_ms(test_quanta, 10);
    uint16_t quanta_back = flow_control_ms_to_quanta(ms, 10);
    
    if (abs((int)test_quanta - (int)quanta_back) > 1) { /* Allow 1 quanta tolerance for rounding */
        LOG_ERROR("Time conversion test failed: %d -> %lu ms -> %d quanta", 
                  test_quanta, ms, quanta_back);
        return FLOW_CONTROL_ERROR;
    }
    
    LOG_INFO("Flow control self-test passed");
    return FLOW_CONTROL_SUCCESS;
}

/* ========================================================================== */
/* INTERNAL HELPER FUNCTIONS                                                */
/* ========================================================================== */

/**
 * @brief Check if packet is a PAUSE frame (quick check)
 */
static bool flow_control_is_pause_frame(const uint8_t *packet, uint16_t length) {
    if (!packet || length < 16) {
        return false;
    }
    
    /* Check EtherType (offset 12-13) */
    uint16_t ethertype = (packet[12] << 8) | packet[13];
    if (ethertype != FLOW_CONTROL_ETHERTYPE) {
        return false;
    }
    
    /* Check opcode (offset 14-15) */
    uint16_t opcode = (packet[14] << 8) | packet[15];
    return (opcode == PAUSE_FRAME_OPCODE);
}

/**
 * @brief Get NIC-specific buffer usage percentage
 */
static int flow_control_get_nic_buffer_usage(flow_control_context_t *ctx) {
    switch (ctx->nic_ctx->info->nic_type) {
        case NIC_TYPE_3C515:
            return flow_control_get_3c515_buffer_usage(ctx);
            
        case NIC_TYPE_3C509B:
            return flow_control_get_3c509b_buffer_usage(ctx);
            
        default:
            return FLOW_CONTROL_NOT_SUPPORTED;
    }
}

/**
 * @brief Get 3C515-TX buffer usage (ring buffers)
 */
static int flow_control_get_3c515_buffer_usage(flow_control_context_t *ctx) {
    enhanced_ring_context_t *ring_ctx;
    int used_descriptors;
    
    /* Access enhanced ring context from NIC private data */
    if (!ctx->nic_ctx->private_data) {
        return 0; /* No ring context available */
    }
    
    ring_ctx = (enhanced_ring_context_t *)ctx->nic_ctx->private_data;
    
    /* Calculate RX ring usage */
    used_descriptors = (ring_ctx->rx_dirty - ring_ctx->rx_cur + ring_ctx->ring_size) % ring_ctx->ring_size;
    
    return (used_descriptors * 100) / ring_ctx->ring_size;
}

/**
 * @brief Get 3C509B buffer usage (FIFO estimation)
 */
static int flow_control_get_3c509b_buffer_usage(flow_control_context_t *ctx) {
    /* For 3C509B, we estimate buffer usage based on TX FIFO status
     * This is a simplified approach since we don't have direct FIFO level access */
    
    /* Read TX free space if available */
    uint16_t tx_free = inw(ctx->nic_ctx->io_base + 0x0C); /* TX Free register */
    uint16_t fifo_size = 2048; /* 3C509B has 2KB FIFO */
    
    if (tx_free >= fifo_size) {
        return 0; /* Empty */
    }
    
    uint16_t used = fifo_size - tx_free;
    return (used * 100) / fifo_size;
}

/**
 * @brief Send PAUSE frame (internal implementation)
 */
static int flow_control_send_pause_frame_internal(flow_control_context_t *ctx, uint16_t pause_time) {
    uint8_t pause_frame_buffer[PAUSE_FRAME_MIN_SIZE];
    int frame_size;
    int result;
    
    /* Generate PAUSE frame */
    frame_size = flow_control_generate_pause_frame(ctx, pause_time, 
                                                  pause_frame_buffer, sizeof(pause_frame_buffer));
    if (frame_size < 0) {
        return frame_size;
    }
    
    /* Send PAUSE frame through NIC */
    if (ctx->nic_ctx->info->vtable && ctx->nic_ctx->info->vtable->send_packet) {
        result = ctx->nic_ctx->info->vtable->send_packet(ctx->nic_ctx, 
                                                        pause_frame_buffer, frame_size);
    } else {
        return FLOW_CONTROL_NOT_SUPPORTED;
    }
    
    if (result == 0) {
        ctx->stats.pause_frames_sent++;
        LOG_TRACE("PAUSE frame sent: pause_time=%d quanta", pause_time);
    } else {
        ctx->stats.flow_control_errors++;
        LOG_ERROR("Failed to send PAUSE frame: %d", result);
    }
    
    return result;
}

/**
 * @brief Update flow control statistics with event
 */
static void flow_control_update_statistics(flow_control_context_t *ctx, const char *event) {
    if (!ctx || !event) {
        return;
    }
    
    /* Update timing statistics */
    if (FLOW_CONTROL_IS_ACTIVE(ctx)) {
        uint32_t current_time = get_timestamp_ms();
        uint32_t time_in_state = current_time - ctx->last_state_change_time;
        
        ctx->stats.total_pause_time_ms += time_in_state;
        
        if (time_in_state > ctx->stats.max_pause_duration_ms) {
            ctx->stats.max_pause_duration_ms = time_in_state;
        }
    }
    
    LOG_TRACE("Flow control event: %s", event);
}

/**
 * @brief Validate flow control context
 */
static int flow_control_validate_context(const flow_control_context_t *ctx) {
    if (!ctx) {
        return FLOW_CONTROL_INVALID_PARAM;
    }
    
    if (!ctx->initialized) {
        return FLOW_CONTROL_NOT_INITIALIZED;
    }
    
    if (!ctx->nic_ctx) {
        return FLOW_CONTROL_INVALID_PARAM;
    }
    
    return FLOW_CONTROL_SUCCESS;
}

/**
 * @brief Process flow control state machine (internal)
 */
static int flow_control_state_machine_update(flow_control_context_t *ctx) {
    switch (ctx->state) {
        case FLOW_CONTROL_STATE_DISABLED:
            return flow_control_handle_state_disabled(ctx);
            
        case FLOW_CONTROL_STATE_IDLE:
            return flow_control_handle_state_idle(ctx);
            
        case FLOW_CONTROL_STATE_PAUSE_REQUESTED:
            return flow_control_handle_state_pause_requested(ctx);
            
        case FLOW_CONTROL_STATE_PAUSE_ACTIVE:
            return flow_control_handle_state_pause_active(ctx);
            
        case FLOW_CONTROL_STATE_RESUME_PENDING:
            return flow_control_handle_state_resume_pending(ctx);
            
        case FLOW_CONTROL_STATE_ERROR:
            return flow_control_handle_state_error(ctx);
            
        default:
            LOG_ERROR("Invalid flow control state: %d", ctx->state);
            ctx->state = FLOW_CONTROL_STATE_ERROR;
            return FLOW_CONTROL_INVALID_STATE;
    }
}

/* State machine handlers */
static int flow_control_handle_state_disabled(flow_control_context_t *ctx) {
    /* Stay disabled until explicitly enabled */
    return FLOW_CONTROL_SUCCESS;
}

static int flow_control_handle_state_idle(flow_control_context_t *ctx) {
    /* Check for buffer watermark triggers */
    if (ctx->high_watermark_reached) {
        flow_control_monitor_buffer_levels(ctx);
    }
    return FLOW_CONTROL_SUCCESS;
}

static int flow_control_handle_state_pause_requested(flow_control_context_t *ctx) {
    /* Transition to active pause state */
    flow_control_transition_state(ctx, FLOW_CONTROL_STATE_PAUSE_ACTIVE);
    ctx->stats.transmission_pauses++;
    return FLOW_CONTROL_SUCCESS;
}

static int flow_control_handle_state_pause_active(flow_control_context_t *ctx) {
    /* Check if pause should expire */
    flow_control_update_timer_state(ctx);
    return FLOW_CONTROL_SUCCESS;
}

static int flow_control_handle_state_resume_pending(flow_control_context_t *ctx) {
    /* Resume transmission */
    flow_control_transition_state(ctx, FLOW_CONTROL_STATE_IDLE);
    return FLOW_CONTROL_SUCCESS;
}

static int flow_control_handle_state_error(flow_control_context_t *ctx) {
    /* Error recovery */
    ctx->error_recovery_attempts++;
    
    if (ctx->error_recovery_attempts < MAX_ERROR_RECOVERY_ATTEMPTS) {
        LOG_WARNING("Flow control error recovery attempt %d", ctx->error_recovery_attempts);
        flow_control_reset(ctx);
    } else {
        LOG_ERROR("Flow control error recovery failed, disabling");
        ctx->config.enabled = false;
        flow_control_transition_state(ctx, FLOW_CONTROL_STATE_DISABLED);
    }
    
    return FLOW_CONTROL_SUCCESS;
}