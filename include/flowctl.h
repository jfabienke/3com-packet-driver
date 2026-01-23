/**
 * @file flow_control.h
 * @brief 802.3x Flow Control Implementation for 3Com Packet Driver
 * 
 * Sprint 2.3: 802.3x Flow Control Implementation
 * 
 * This module implements 802.3x flow control (PAUSE frame) support for
 * improved network utilization and congestion management. The implementation
 * provides software-based flow control for ISA-generation NICs (3C515-TX and
 * 3C509B) that lack hardware PAUSE frame support.
 * 
 * Key Features:
 * - PAUSE frame detection and parsing (Type 0x8808, Opcode 0x0001)
 * - Transmission throttling based on PAUSE timer values
 * - Flow control state machine with automatic resume
 * - Integration with existing interrupt mitigation and buffer management
 * - Comprehensive statistics collection and monitoring
 * - Fallback mechanisms for switches without flow control support
 * 
 * Technical Implementation:
 * - Software-based PAUSE frame processing for ISA NICs
 * - Timer-based transmission throttling mechanism
 * - Integration with enhanced ring buffer management (16 descriptors)
 * - Coordination with interrupt batching system (Sprint 1.3)
 * - Support for both full-duplex and half-duplex scenarios
 * 
 * Hardware Compatibility:
 * - 3C515-TX: Software flow control with DMA integration
 * - 3C509B: Software flow control with PIO integration
 * - Future-ready for hardware PAUSE frame support
 * 
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 * This file is part of the 3Com Packet Driver project.
 */

#ifndef _FLOW_CONTROL_H_
#define _FLOW_CONTROL_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"
#include "niccap.h"
#include "irqmit.h"

/* ========================================================================== */
/* 802.3x PAUSE FRAME CONSTANTS AND STRUCTURES                              */
/* ========================================================================== */

/* 802.3x MAC Control Frame constants */
#define FLOW_CONTROL_ETHERTYPE      0x8808     /* MAC Control frame type */
#define PAUSE_FRAME_OPCODE          0x0001     /* PAUSE frame opcode */
#define PRIORITY_PAUSE_OPCODE       0x0101     /* Priority PAUSE frame opcode */

/* PAUSE frame destination MAC address (multicast, not forwarded by bridges) */
#define PAUSE_FRAME_DEST_MAC        {0x01, 0x80, 0xC2, 0x00, 0x00, 0x01}

/* PAUSE frame timing constants */
#define PAUSE_QUANTA_UNIT_BIT_TIMES 512        /* Bit times per pause quanta */
#define MAX_PAUSE_QUANTA            0xFFFF     /* Maximum pause time */
#define DEFAULT_PAUSE_TIME          0x0100     /* Default pause time (256 quanta) */

/* Frame size constants */
#define PAUSE_FRAME_MIN_SIZE        64         /* Minimum Ethernet frame size */
#define PAUSE_FRAME_PAYLOAD_SIZE    42         /* PAUSE frame payload (opcode + params + padding) */
#define PAUSE_FRAME_PADDING_SIZE    42         /* Padding bytes (all zeros) */

/* Flow control timing and thresholds */
#define FLOW_CONTROL_CHECK_INTERVAL_MS    10   /* Flow control check interval */
#define PAUSE_TIMER_RESOLUTION_MS         1    /* Timer resolution */
#define MAX_PAUSE_DURATION_MS             350  /* Maximum pause duration (safety limit) */
#define FLOW_CONTROL_TIMEOUT_MS           5000 /* Flow control state timeout */

/* Buffer threshold constants for flow control activation */
#define FLOW_CONTROL_HIGH_WATERMARK       85   /* Activate flow control at 85% buffer usage */
#define FLOW_CONTROL_LOW_WATERMARK        60   /* Deactivate flow control at 60% buffer usage */
#define FLOW_CONTROL_EMERGENCY_THRESHOLD  95   /* Emergency threshold for immediate PAUSE */

/**
 * @brief 802.3x PAUSE frame structure
 * 
 * Standard PAUSE frame format according to IEEE 802.3x specification.
 * Total frame size is 64 bytes including Ethernet header and CRC.
 */
typedef struct __attribute__((packed)) {
    /* Ethernet header (14 bytes) */
    uint8_t dest_mac[6];        /* Destination MAC: 01:80:C2:00:00:01 */
    uint8_t src_mac[6];         /* Source MAC address */
    uint16_t ethertype;         /* EtherType: 0x8808 (MAC Control) */
    
    /* MAC Control payload (46 bytes) */
    uint16_t opcode;            /* Control opcode: 0x0001 (PAUSE) */
    uint16_t pause_time;        /* Pause time in quanta (0-65535) */
    uint8_t padding[42];        /* Padding bytes (all zeros) */
    
    /* CRC is added by hardware/software layer */
} pause_frame_t;

/**
 * @brief Flow control state enumeration
 */
typedef enum {
    FLOW_CONTROL_STATE_DISABLED = 0,    /* Flow control disabled */
    FLOW_CONTROL_STATE_IDLE,            /* No flow control active */
    FLOW_CONTROL_STATE_PAUSE_REQUESTED, /* PAUSE frame received, throttling TX */
    FLOW_CONTROL_STATE_PAUSE_ACTIVE,    /* Actively pausing transmission */
    FLOW_CONTROL_STATE_RESUME_PENDING,  /* Waiting to resume transmission */
    FLOW_CONTROL_STATE_ERROR            /* Error state requiring reset */
} flow_control_state_t;

/**
 * @brief Flow control capability flags
 */
typedef enum {
    FLOW_CONTROL_CAP_NONE           = 0x0000,  /* No flow control support */
    FLOW_CONTROL_CAP_RX_PAUSE       = 0x0001,  /* Can process received PAUSE frames */
    FLOW_CONTROL_CAP_TX_PAUSE       = 0x0002,  /* Can send PAUSE frames */
    FLOW_CONTROL_CAP_SYMMETRIC      = 0x0003,  /* Full symmetric flow control */
    FLOW_CONTROL_CAP_ASYMMETRIC     = 0x0004,  /* Asymmetric flow control */
    FLOW_CONTROL_CAP_AUTO_NEGOTIATE = 0x0008,  /* Auto-negotiation support */
    FLOW_CONTROL_CAP_PRIORITY_PAUSE = 0x0010,  /* Priority-based PAUSE support */
    FLOW_CONTROL_CAP_HW_DETECTION   = 0x0020,  /* Hardware PAUSE frame detection */
    FLOW_CONTROL_CAP_HW_GENERATION  = 0x0040   /* Hardware PAUSE frame generation */
} flow_control_capabilities_t;

/**
 * @brief Flow control configuration structure
 */
typedef struct {
    bool enabled;                           /* Flow control enabled flag */
    bool rx_pause_enabled;                  /* Process received PAUSE frames */
    bool tx_pause_enabled;                  /* Send PAUSE frames */
    bool auto_negotiate;                    /* Auto-negotiate flow control */
    uint16_t pause_time_default;            /* Default pause time to send */
    uint16_t high_watermark_percent;        /* High watermark for PAUSE generation */
    uint16_t low_watermark_percent;         /* Low watermark for PAUSE resume */
    uint32_t max_pause_duration_ms;         /* Maximum pause duration (safety) */
    flow_control_capabilities_t capabilities; /* Supported capabilities */
} flow_control_config_t;

/**
 * @brief Flow control statistics structure
 */
typedef struct {
    /* PAUSE frame statistics */
    uint32_t pause_frames_received;         /* PAUSE frames received */
    uint32_t pause_frames_sent;             /* PAUSE frames sent */
    uint32_t invalid_pause_frames;          /* Invalid PAUSE frames detected */
    uint32_t pause_frames_ignored;          /* PAUSE frames ignored (disabled) */
    
    /* Flow control events */
    uint32_t flow_control_activations;      /* Times flow control activated */
    uint32_t flow_control_deactivations;    /* Times flow control deactivated */
    uint32_t transmission_pauses;           /* Times transmission was paused */
    uint32_t transmission_resumes;          /* Times transmission resumed */
    
    /* Timing statistics */
    uint32_t total_pause_time_ms;           /* Total time spent paused */
    uint32_t max_pause_duration_ms;         /* Maximum single pause duration */
    uint32_t avg_pause_duration_ms;         /* Average pause duration */
    uint32_t pause_timeout_events;          /* Pause timeout events */
    
    /* Buffer management statistics */
    uint32_t buffer_watermark_triggers;     /* Buffer watermark triggers */
    uint32_t emergency_pause_events;        /* Emergency PAUSE events */
    uint32_t buffer_overflow_prevented;     /* Buffer overflows prevented */
    
    /* Error statistics */
    uint32_t flow_control_errors;           /* Flow control processing errors */
    uint32_t state_machine_errors;          /* State machine errors */
    uint32_t timer_errors;                  /* Timer management errors */
} flow_control_stats_t;

/**
 * @brief Flow control context structure
 */
typedef struct {
    /* Configuration */
    flow_control_config_t config;           /* Flow control configuration */
    nic_context_t *nic_ctx;                 /* Associated NIC context */
    
    /* State management */
    flow_control_state_t state;             /* Current flow control state */
    uint32_t pause_start_time;              /* When current pause started */
    uint32_t pause_duration_remaining;      /* Remaining pause time in ms */
    uint16_t last_pause_time_received;      /* Last PAUSE time received */
    
    /* Buffer monitoring */
    uint32_t last_buffer_check_time;        /* Last buffer level check */
    uint16_t current_buffer_usage_percent;  /* Current buffer usage percentage */
    bool high_watermark_reached;            /* High watermark status */
    
    /* Partner flow control support */
    bool partner_supports_flow_control;     /* Partner supports flow control */
    uint32_t partner_last_pause_time;       /* Partner's last pause request */
    
    /* Statistics */
    flow_control_stats_t stats;             /* Flow control statistics */
    
    /* Integration with other systems */
    interrupt_mitigation_context_t *im_ctx; /* Interrupt mitigation context */
    void *private_data;                     /* NIC-specific private data */
    
    /* Internal state */
    uint32_t last_state_change_time;        /* Last state change timestamp */
    uint8_t error_recovery_attempts;        /* Error recovery attempt counter */
    bool initialized;                       /* Context initialization flag */
} flow_control_context_t;

/* ========================================================================== */
/* FLOW CONTROL API FUNCTIONS                                               */
/* ========================================================================== */

/**
 * @brief Initialize flow control subsystem
 * @param ctx Flow control context to initialize
 * @param nic_ctx Associated NIC context
 * @param config Initial configuration (NULL for defaults)
 * @return 0 on success, negative error code on failure
 */
int flow_control_init(flow_control_context_t *ctx, 
                      nic_context_t *nic_ctx,
                      const flow_control_config_t *config);

/**
 * @brief Cleanup flow control resources
 * @param ctx Flow control context
 */
void flow_control_cleanup(flow_control_context_t *ctx);

/**
 * @brief Reset flow control state
 * @param ctx Flow control context
 * @return 0 on success, negative error code on failure
 */
int flow_control_reset(flow_control_context_t *ctx);

/**
 * @brief Enable or disable flow control
 * @param ctx Flow control context
 * @param enabled true to enable, false to disable
 * @return 0 on success, negative error code on failure
 */
int flow_control_set_enabled(flow_control_context_t *ctx, bool enabled);

/**
 * @brief Check if flow control is enabled
 * @param ctx Flow control context
 * @return true if enabled, false otherwise
 */
bool flow_control_is_enabled(const flow_control_context_t *ctx);

/* ========================================================================== */
/* PAUSE FRAME PROCESSING                                                   */
/* ========================================================================== */

/**
 * @brief Process received packet for PAUSE frame detection
 * @param ctx Flow control context
 * @param packet Received packet data
 * @param length Packet length
 * @return 1 if PAUSE frame processed, 0 if not PAUSE frame, negative on error
 */
int flow_control_process_received_packet(flow_control_context_t *ctx,
                                        const uint8_t *packet,
                                        uint16_t length);

/**
 * @brief Parse PAUSE frame from packet data
 * @param packet Packet data
 * @param length Packet length
 * @param pause_frame Output structure for parsed PAUSE frame
 * @return 1 if valid PAUSE frame, 0 if not PAUSE frame, negative on error
 */
int flow_control_parse_pause_frame(const uint8_t *packet,
                                  uint16_t length,
                                  pause_frame_t *pause_frame);

/**
 * @brief Generate PAUSE frame
 * @param ctx Flow control context
 * @param pause_time Pause time in quanta (0 to resume)
 * @param frame_buffer Output buffer for PAUSE frame (minimum 64 bytes)
 * @param buffer_size Size of output buffer
 * @return Frame size on success, negative error code on failure
 */
int flow_control_generate_pause_frame(flow_control_context_t *ctx,
                                     uint16_t pause_time,
                                     uint8_t *frame_buffer,
                                     uint16_t buffer_size);

/**
 * @brief Send PAUSE frame to request transmission pause
 * @param ctx Flow control context
 * @param pause_time Pause time in quanta (0 to resume)
 * @return 0 on success, negative error code on failure
 */
int flow_control_send_pause_frame(flow_control_context_t *ctx, 
                                 uint16_t pause_time);

/* ========================================================================== */
/* TRANSMISSION CONTROL                                                     */
/* ========================================================================== */

/**
 * @brief Check if transmission should be paused
 * @param ctx Flow control context
 * @return true if transmission should be paused, false otherwise
 */
bool flow_control_should_pause_transmission(const flow_control_context_t *ctx);

/**
 * @brief Process transmission request (called before each packet transmission)
 * @param ctx Flow control context
 * @return 0 if transmission allowed, 1 if paused, negative on error
 */
int flow_control_process_transmission_request(flow_control_context_t *ctx);

/**
 * @brief Update flow control state based on timer expiration
 * @param ctx Flow control context
 * @return 0 on success, negative error code on failure
 */
int flow_control_update_timer_state(flow_control_context_t *ctx);

/**
 * @brief Force resume transmission (emergency override)
 * @param ctx Flow control context
 * @return 0 on success, negative error code on failure
 */
int flow_control_force_resume_transmission(flow_control_context_t *ctx);

/* ========================================================================== */
/* BUFFER MONITORING AND AUTOMATIC PAUSE GENERATION                        */
/* ========================================================================== */

/**
 * @brief Monitor buffer levels and generate PAUSE frames if needed
 * @param ctx Flow control context
 * @return 0 on success, negative error code on failure
 */
int flow_control_monitor_buffer_levels(flow_control_context_t *ctx);

/**
 * @brief Get current buffer usage percentage
 * @param ctx Flow control context
 * @return Buffer usage percentage (0-100), negative on error
 */
int flow_control_get_buffer_usage_percent(flow_control_context_t *ctx);

/**
 * @brief Check if high watermark threshold is reached
 * @param ctx Flow control context
 * @return true if high watermark reached, false otherwise
 */
bool flow_control_is_high_watermark_reached(const flow_control_context_t *ctx);

/**
 * @brief Trigger emergency PAUSE frame generation
 * @param ctx Flow control context
 * @return 0 on success, negative error code on failure
 */
int flow_control_trigger_emergency_pause(flow_control_context_t *ctx);

/* ========================================================================== */
/* STATE MACHINE MANAGEMENT                                                 */
/* ========================================================================== */

/**
 * @brief Get current flow control state
 * @param ctx Flow control context
 * @return Current flow control state
 */
flow_control_state_t flow_control_get_state(const flow_control_context_t *ctx);

/**
 * @brief Convert flow control state to string
 * @param state Flow control state
 * @return String representation of state
 */
const char* flow_control_state_to_string(flow_control_state_t state);

/**
 * @brief Process flow control state machine
 * @param ctx Flow control context
 * @return 0 on success, negative error code on failure
 */
int flow_control_process_state_machine(flow_control_context_t *ctx);

/**
 * @brief Handle state transition
 * @param ctx Flow control context
 * @param new_state New state to transition to
 * @return 0 on success, negative error code on failure
 */
int flow_control_transition_state(flow_control_context_t *ctx,
                                 flow_control_state_t new_state);

/* ========================================================================== */
/* STATISTICS AND MONITORING                                                */
/* ========================================================================== */

/**
 * @brief Get flow control statistics
 * @param ctx Flow control context
 * @param stats Output buffer for statistics
 * @return 0 on success, negative error code on failure
 */
int flow_control_get_statistics(const flow_control_context_t *ctx,
                               flow_control_stats_t *stats);

/**
 * @brief Clear flow control statistics
 * @param ctx Flow control context
 */
void flow_control_clear_statistics(flow_control_context_t *ctx);

/**
 * @brief Get flow control performance metrics
 * @param ctx Flow control context
 * @param avg_pause_duration_ms Output: average pause duration
 * @param pause_efficiency_percent Output: pause efficiency percentage
 * @param buffer_overflow_prevention_rate Output: overflow prevention rate
 * @return 0 on success, negative error code on failure
 */
int flow_control_get_performance_metrics(const flow_control_context_t *ctx,
                                        uint32_t *avg_pause_duration_ms,
                                        uint32_t *pause_efficiency_percent,
                                        uint32_t *buffer_overflow_prevention_rate);

/* ========================================================================== */
/* CONFIGURATION MANAGEMENT                                                 */
/* ========================================================================== */

/**
 * @brief Get current flow control configuration
 * @param ctx Flow control context
 * @param config Output buffer for configuration
 * @return 0 on success, negative error code on failure
 */
int flow_control_get_config(const flow_control_context_t *ctx,
                           flow_control_config_t *config);

/**
 * @brief Update flow control configuration
 * @param ctx Flow control context
 * @param config New configuration
 * @return 0 on success, negative error code on failure
 */
int flow_control_set_config(flow_control_context_t *ctx,
                           const flow_control_config_t *config);

/**
 * @brief Get default flow control configuration
 * @param nic_type NIC type for capability-specific defaults
 * @param config Output buffer for default configuration
 * @return 0 on success, negative error code on failure
 */
int flow_control_get_default_config(nic_type_t nic_type,
                                   flow_control_config_t *config);

/* ========================================================================== */
/* CAPABILITY DETECTION AND NEGOTIATION                                     */
/* ========================================================================== */

/**
 * @brief Detect flow control capabilities for NIC
 * @param nic_ctx NIC context
 * @return Flow control capabilities bitmask
 */
flow_control_capabilities_t flow_control_detect_capabilities(const nic_context_t *nic_ctx);

/**
 * @brief Check if partner supports flow control
 * @param ctx Flow control context
 * @return true if partner supports flow control, false otherwise
 */
bool flow_control_partner_supports_flow_control(const flow_control_context_t *ctx);

/**
 * @brief Negotiate flow control with partner
 * @param ctx Flow control context
 * @return 0 on success, negative error code on failure
 */
int flow_control_negotiate_with_partner(flow_control_context_t *ctx);

/* ========================================================================== */
/* INTEGRATION FUNCTIONS                                                    */
/* ========================================================================== */

/**
 * @brief Integration with interrupt mitigation system
 * @param ctx Flow control context
 * @param im_ctx Interrupt mitigation context
 * @return 0 on success, negative error code on failure
 */
int flow_control_integrate_interrupt_mitigation(flow_control_context_t *ctx,
                                               interrupt_mitigation_context_t *im_ctx);

/**
 * @brief Process flow control during interrupt handling
 * @param ctx Flow control context
 * @param event_type Type of interrupt event
 * @return 0 on success, negative error code on failure
 */
int flow_control_process_interrupt_event(flow_control_context_t *ctx,
                                        interrupt_event_type_t event_type);

/**
 * @brief Periodic flow control maintenance (called from timer/scheduler)
 * @param ctx Flow control context
 * @return 0 on success, negative error code on failure
 */
int flow_control_periodic_maintenance(flow_control_context_t *ctx);

/* ========================================================================== */
/* UTILITY FUNCTIONS                                                        */
/* ========================================================================== */

/**
 * @brief Validate PAUSE frame structure
 * @param pause_frame PAUSE frame to validate
 * @return true if valid, false otherwise
 */
bool flow_control_validate_pause_frame(const pause_frame_t *pause_frame);

/**
 * @brief Convert pause time from quanta to milliseconds
 * @param pause_quanta Pause time in quanta
 * @param link_speed_mbps Link speed in Mbps
 * @return Pause time in milliseconds
 */
uint32_t flow_control_quanta_to_ms(uint16_t pause_quanta, uint32_t link_speed_mbps);

/**
 * @brief Convert pause time from milliseconds to quanta
 * @param pause_ms Pause time in milliseconds
 * @param link_speed_mbps Link speed in Mbps
 * @return Pause time in quanta
 */
uint16_t flow_control_ms_to_quanta(uint32_t pause_ms, uint32_t link_speed_mbps);

/**
 * @brief Self-test flow control functionality
 * @return 0 on success, negative error code on failure
 */
int flow_control_self_test(void);

/* ========================================================================== */
/* ERROR CODES                                                              */
/* ========================================================================== */

#define FLOW_CONTROL_SUCCESS            0
#define FLOW_CONTROL_ERROR              -1
#define FLOW_CONTROL_INVALID_PARAM      -2
#define FLOW_CONTROL_NOT_INITIALIZED    -3
#define FLOW_CONTROL_NOT_SUPPORTED      -4
#define FLOW_CONTROL_BUFFER_TOO_SMALL   -5
#define FLOW_CONTROL_INVALID_STATE      -6
#define FLOW_CONTROL_TIMEOUT            -7
#define FLOW_CONTROL_PARSE_ERROR        -8
#define FLOW_CONTROL_GENERATION_ERROR   -9
#define FLOW_CONTROL_TRANSMISSION_ERROR -10

/* ========================================================================== */
/* CONVENIENCE MACROS                                                       */
/* ========================================================================== */

/* Check if flow control is active */
#define FLOW_CONTROL_IS_ACTIVE(ctx) \
    ((ctx)->state == FLOW_CONTROL_STATE_PAUSE_ACTIVE || \
     (ctx)->state == FLOW_CONTROL_STATE_PAUSE_REQUESTED)

/* Check if transmission should be blocked */
#define FLOW_CONTROL_BLOCKS_TRANSMISSION(ctx) \
    (flow_control_is_enabled(ctx) && FLOW_CONTROL_IS_ACTIVE(ctx))

/* Get time since last state change */
#define FLOW_CONTROL_TIME_IN_STATE(ctx) \
    (get_timestamp_ms() - (ctx)->last_state_change_time)

/* Check if pause has timed out */
#define FLOW_CONTROL_PAUSE_TIMED_OUT(ctx) \
    (FLOW_CONTROL_TIME_IN_STATE(ctx) > (ctx)->config.max_pause_duration_ms)

/* Default configuration initializer */
#define FLOW_CONTROL_CONFIG_DEFAULT { \
    .enabled = true, \
    .rx_pause_enabled = true, \
    .tx_pause_enabled = true, \
    .auto_negotiate = true, \
    .pause_time_default = DEFAULT_PAUSE_TIME, \
    .high_watermark_percent = FLOW_CONTROL_HIGH_WATERMARK, \
    .low_watermark_percent = FLOW_CONTROL_LOW_WATERMARK, \
    .max_pause_duration_ms = MAX_PAUSE_DURATION_MS, \
    .capabilities = FLOW_CONTROL_CAP_RX_PAUSE | FLOW_CONTROL_CAP_TX_PAUSE \
}

#ifdef __cplusplus
}
#endif

#endif /* _FLOW_CONTROL_H_ */

/* -------------------------------------------------------------------------- */
/* Compatibility wrappers used by packet_ops.c (software PAUSE implementation) */
/* These lightweight APIs provide NIC-index based flow control suitable for    */
/* DOS real-mode drivers without per-NIC heap allocations.                     */
/* -------------------------------------------------------------------------- */

/* Initialize software flow control (global/per-NIC state) */
int flow_control_init(void);

/* RX path: detect and process PAUSE frame; returns 1 if handled */
int flow_control_process_received_packet(int nic_index,
                                         const uint8_t *packet,
                                         uint16_t length);

/* TX path: check if transmission should be paused for NIC */
bool flow_control_should_pause_transmission(int nic_index);

/* TX path: remaining pause time in milliseconds (0 if none) */
uint32_t flow_control_get_pause_duration(int nic_index);

/* Busy-wait resume helper (bounded, DOS-safe) */
void flow_control_wait_for_resume(int nic_index, uint32_t pause_ms);

/* Update buffer usage percentage to drive high/low watermarks */
void flow_control_update_buffer_status(int nic_index, uint16_t usage_percent);
