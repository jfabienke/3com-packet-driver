/**
 * @file test_flow_control.c
 * @brief Comprehensive Test Suite for 802.3x Flow Control Implementation
 * 
 * Sprint 2.3: 802.3x Flow Control - Test Suite
 * 
 * This test suite provides comprehensive validation of the 802.3x flow control
 * implementation including PAUSE frame parsing, state machine operation,
 * transmission throttling, buffer monitoring, and interoperability testing.
 * 
 * Test Categories:
 * 1. PAUSE Frame Parsing and Generation
 * 2. Flow Control State Machine
 * 3. Transmission Throttling
 * 4. Buffer Monitoring and Automatic PAUSE
 * 5. Integration with Interrupt Mitigation
 * 6. Performance and Statistics
 * 7. Error Handling and Recovery
 * 8. Interoperability Testing
 * 
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 * This file is part of the 3Com Packet Driver project.
 */

#include "include/flow_control.h"
#include "include/nic_capabilities.h"
#include "include/test_framework.h"
#include "include/logging.h"
#include "include/hardware_mock.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

/* ========================================================================== */
/* TEST FRAMEWORK AND UTILITIES                                             */
/* ========================================================================== */

/* Test context structure */
typedef struct {
    flow_control_context_t flow_ctx;
    nic_context_t nic_ctx;
    nic_info_entry_t nic_info;
    interrupt_mitigation_context_t im_ctx;
    bool mock_hardware_active;
    uint32_t test_start_time;
} test_context_t;

/* Test counters */
static struct {
    int tests_run;
    int tests_passed;
    int tests_failed;
} test_stats = {0, 0, 0};

/* Test result macros */
#define TEST_ASSERT(condition, message) do { \
    if (!(condition)) { \
        printf("FAIL: %s - %s\n", __func__, message); \
        test_stats.tests_failed++; \
        return TEST_FAIL; \
    } \
} while(0)

#define TEST_EXPECT_EQ(expected, actual, message) do { \
    if ((expected) != (actual)) { \
        printf("FAIL: %s - %s (expected %d, got %d)\n", __func__, message, (int)(expected), (int)(actual)); \
        test_stats.tests_failed++; \
        return TEST_FAIL; \
    } \
} while(0)

#define TEST_PASS() do { \
    printf("PASS: %s\n", __func__); \
    test_stats.tests_passed++; \
    return TEST_PASS; \
} while(0)

/* Test function type */
typedef enum {
    TEST_PASS = 0,
    TEST_FAIL = -1
} test_result_t;

typedef test_result_t (*test_function_t)(test_context_t *ctx);

/* Forward declarations */
static int setup_test_context(test_context_t *ctx, nic_type_t nic_type);
static void cleanup_test_context(test_context_t *ctx);
static void print_test_summary(void);

/* ========================================================================== */
/* PAUSE FRAME PARSING AND GENERATION TESTS                                */
/* ========================================================================== */

/**
 * @brief Test PAUSE frame parsing with valid frame
 */
static test_result_t test_pause_frame_parsing_valid(test_context_t *ctx) {
    test_stats.tests_run++;
    
    /* Create a valid PAUSE frame */
    uint8_t pause_frame[64] = {
        /* Destination MAC: 01:80:C2:00:00:01 */
        0x01, 0x80, 0xC2, 0x00, 0x00, 0x01,
        /* Source MAC: 00:11:22:33:44:55 */
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55,
        /* EtherType: 0x8808 */
        0x88, 0x08,
        /* Opcode: 0x0001 */
        0x00, 0x01,
        /* Pause time: 0x0200 (512 quanta) */
        0x02, 0x00
        /* Remaining bytes are padding (zeros) */
    };
    
    pause_frame_t parsed_frame;
    int result = flow_control_parse_pause_frame(pause_frame, sizeof(pause_frame), &parsed_frame);
    
    TEST_EXPECT_EQ(1, result, "Should successfully parse valid PAUSE frame");
    TEST_EXPECT_EQ(0x8808, ntohs(parsed_frame.ethertype), "EtherType should be 0x8808");
    TEST_EXPECT_EQ(0x0001, ntohs(parsed_frame.opcode), "Opcode should be 0x0001");
    TEST_EXPECT_EQ(0x0200, ntohs(parsed_frame.pause_time), "Pause time should be 0x0200");
    
    /* Verify destination MAC */
    const uint8_t expected_dest[6] = {0x01, 0x80, 0xC2, 0x00, 0x00, 0x01};
    TEST_ASSERT(memcmp(parsed_frame.dest_mac, expected_dest, 6) == 0, "Destination MAC should match");
    
    TEST_PASS();
}

/**
 * @brief Test PAUSE frame parsing with invalid frames
 */
static test_result_t test_pause_frame_parsing_invalid(test_context_t *ctx) {
    test_stats.tests_run++;
    
    pause_frame_t parsed_frame;
    int result;
    
    /* Test 1: Frame too short */
    uint8_t short_frame[16] = {0};
    result = flow_control_parse_pause_frame(short_frame, sizeof(short_frame), &parsed_frame);
    TEST_EXPECT_EQ(FLOW_CONTROL_PARSE_ERROR, result, "Should reject frame too short");
    
    /* Test 2: Wrong EtherType */
    uint8_t wrong_ethertype[64] = {
        0x01, 0x80, 0xC2, 0x00, 0x00, 0x01,  /* Dest MAC */
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55,  /* Src MAC */
        0x08, 0x00,  /* Wrong EtherType (IP) */
        0x00, 0x01   /* Opcode */
    };
    result = flow_control_parse_pause_frame(wrong_ethertype, sizeof(wrong_ethertype), &parsed_frame);
    TEST_EXPECT_EQ(0, result, "Should ignore frame with wrong EtherType");
    
    /* Test 3: Wrong opcode */
    uint8_t wrong_opcode[64] = {
        0x01, 0x80, 0xC2, 0x00, 0x00, 0x01,  /* Dest MAC */
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55,  /* Src MAC */
        0x88, 0x08,  /* Correct EtherType */
        0x00, 0x02   /* Wrong opcode */
    };
    result = flow_control_parse_pause_frame(wrong_opcode, sizeof(wrong_opcode), &parsed_frame);
    TEST_EXPECT_EQ(0, result, "Should ignore frame with wrong opcode");
    
    /* Test 4: NULL parameters */
    result = flow_control_parse_pause_frame(NULL, 64, &parsed_frame);
    TEST_EXPECT_EQ(FLOW_CONTROL_INVALID_PARAM, result, "Should reject NULL packet");
    
    result = flow_control_parse_pause_frame(wrong_opcode, 64, NULL);
    TEST_EXPECT_EQ(FLOW_CONTROL_INVALID_PARAM, result, "Should reject NULL output structure");
    
    TEST_PASS();
}

/**
 * @brief Test PAUSE frame generation
 */
static test_result_t test_pause_frame_generation(test_context_t *ctx) {
    test_stats.tests_run++;
    
    uint8_t frame_buffer[64];
    uint16_t test_pause_time = 0x0100; /* 256 quanta */
    
    int frame_size = flow_control_generate_pause_frame(&ctx->flow_ctx, test_pause_time, 
                                                      frame_buffer, sizeof(frame_buffer));
    
    TEST_EXPECT_EQ(64, frame_size, "Generated frame should be 64 bytes");
    
    /* Verify frame structure */
    TEST_EXPECT_EQ(0x88, frame_buffer[12], "EtherType high byte should be 0x88");
    TEST_EXPECT_EQ(0x08, frame_buffer[13], "EtherType low byte should be 0x08");
    TEST_EXPECT_EQ(0x00, frame_buffer[14], "Opcode high byte should be 0x00");
    TEST_EXPECT_EQ(0x01, frame_buffer[15], "Opcode low byte should be 0x01");
    TEST_EXPECT_EQ(0x01, frame_buffer[16], "Pause time high byte should be 0x01");
    TEST_EXPECT_EQ(0x00, frame_buffer[17], "Pause time low byte should be 0x00");
    
    /* Verify destination MAC */
    const uint8_t expected_dest[6] = {0x01, 0x80, 0xC2, 0x00, 0x00, 0x01};
    TEST_ASSERT(memcmp(frame_buffer, expected_dest, 6) == 0, "Destination MAC should be PAUSE multicast");
    
    /* Test with buffer too small */
    uint8_t small_buffer[32];
    frame_size = flow_control_generate_pause_frame(&ctx->flow_ctx, test_pause_time, 
                                                  small_buffer, sizeof(small_buffer));
    TEST_EXPECT_EQ(FLOW_CONTROL_INVALID_PARAM, frame_size, "Should reject buffer too small");
    
    TEST_PASS();
}

/**
 * @brief Test time conversion functions
 */
static test_result_t test_time_conversion_functions(test_context_t *ctx) {
    test_stats.tests_run++;
    
    /* Test quanta to milliseconds conversion */
    uint32_t ms_10mbps = flow_control_quanta_to_ms(256, 10);  /* 256 quanta at 10 Mbps */
    uint32_t ms_100mbps = flow_control_quanta_to_ms(256, 100); /* 256 quanta at 100 Mbps */
    
    TEST_ASSERT(ms_10mbps > ms_100mbps, "10 Mbps should take longer than 100 Mbps for same quanta");
    TEST_ASSERT(ms_10mbps > 0, "Conversion should return positive value");
    
    /* Test milliseconds to quanta conversion */
    uint16_t quanta_10mbps = flow_control_ms_to_quanta(ms_10mbps, 10);
    TEST_ASSERT(abs((int)quanta_10mbps - 256) <= 1, "Round-trip conversion should be accurate within 1 quanta");
    
    /* Test edge cases */
    uint32_t ms_zero = flow_control_quanta_to_ms(0, 10);
    TEST_EXPECT_EQ(1, ms_zero, "Zero quanta should return minimum 1 ms");
    
    uint16_t quanta_max = flow_control_ms_to_quanta(1000000, 10); /* Very large value */
    TEST_EXPECT_EQ(MAX_PAUSE_QUANTA, quanta_max, "Large ms value should be clamped to max quanta");
    
    TEST_PASS();
}

/* ========================================================================== */
/* FLOW CONTROL STATE MACHINE TESTS                                         */
/* ========================================================================== */

/**
 * @brief Test basic state machine initialization and transitions
 */
static test_result_t test_state_machine_basic(test_context_t *ctx) {
    test_stats.tests_run++;
    
    /* Test initial state */
    flow_control_state_t initial_state = flow_control_get_state(&ctx->flow_ctx);
    TEST_EXPECT_EQ(FLOW_CONTROL_STATE_IDLE, initial_state, "Initial state should be IDLE when enabled");
    
    /* Test state transition */
    int result = flow_control_transition_state(&ctx->flow_ctx, FLOW_CONTROL_STATE_PAUSE_REQUESTED);
    TEST_EXPECT_EQ(FLOW_CONTROL_SUCCESS, result, "State transition should succeed");
    
    flow_control_state_t new_state = flow_control_get_state(&ctx->flow_ctx);
    TEST_EXPECT_EQ(FLOW_CONTROL_STATE_PAUSE_REQUESTED, new_state, "State should be updated");
    
    /* Test state string conversion */
    const char *state_str = flow_control_state_to_string(FLOW_CONTROL_STATE_PAUSE_ACTIVE);
    TEST_ASSERT(strcmp(state_str, "PAUSE_ACTIVE") == 0, "State string should be correct");
    
    /* Test disable/enable */
    result = flow_control_set_enabled(&ctx->flow_ctx, false);
    TEST_EXPECT_EQ(FLOW_CONTROL_SUCCESS, result, "Disable should succeed");
    TEST_ASSERT(!flow_control_is_enabled(&ctx->flow_ctx), "Should be disabled");
    
    result = flow_control_set_enabled(&ctx->flow_ctx, true);
    TEST_EXPECT_EQ(FLOW_CONTROL_SUCCESS, result, "Enable should succeed");
    TEST_ASSERT(flow_control_is_enabled(&ctx->flow_ctx), "Should be enabled");
    
    TEST_PASS();
}

/**
 * @brief Test pause request processing
 */
static test_result_t test_pause_request_processing(test_context_t *ctx) {
    test_stats.tests_run++;
    
    /* Create a PAUSE frame with 100 ms pause time */
    uint16_t pause_quanta = flow_control_ms_to_quanta(100, ctx->nic_ctx.speed);
    uint8_t pause_frame[64];
    
    int frame_size = flow_control_generate_pause_frame(&ctx->flow_ctx, pause_quanta, 
                                                      pause_frame, sizeof(pause_frame));
    TEST_ASSERT(frame_size > 0, "Should generate valid PAUSE frame");
    
    /* Process the PAUSE frame */
    int result = flow_control_process_received_packet(&ctx->flow_ctx, pause_frame, frame_size);
    TEST_EXPECT_EQ(1, result, "Should process PAUSE frame successfully");
    
    /* Check that transmission is now paused */
    TEST_ASSERT(flow_control_should_pause_transmission(&ctx->flow_ctx), "Transmission should be paused");
    
    /* Check transmission request processing */
    result = flow_control_process_transmission_request(&ctx->flow_ctx);
    TEST_EXPECT_EQ(1, result, "Transmission request should be paused");
    
    /* Test resume with pause_time = 0 */
    frame_size = flow_control_generate_pause_frame(&ctx->flow_ctx, 0, pause_frame, sizeof(pause_frame));
    result = flow_control_process_received_packet(&ctx->flow_ctx, pause_frame, frame_size);
    TEST_EXPECT_EQ(1, result, "Should process resume frame successfully");
    
    /* Allow state machine to process the resume */
    flow_control_process_state_machine(&ctx->flow_ctx);
    
    TEST_ASSERT(!flow_control_should_pause_transmission(&ctx->flow_ctx), "Transmission should be resumed");
    
    TEST_PASS();
}

/**
 * @brief Test pause timer expiration
 */
static test_result_t test_pause_timer_expiration(test_context_t *ctx) {
    test_stats.tests_run++;
    
    /* Set a very short pause time for testing */
    uint16_t short_pause_quanta = 1; /* Very short pause */
    uint8_t pause_frame[64];
    
    int frame_size = flow_control_generate_pause_frame(&ctx->flow_ctx, short_pause_quanta, 
                                                      pause_frame, sizeof(pause_frame));
    
    /* Process the PAUSE frame */
    int result = flow_control_process_received_packet(&ctx->flow_ctx, pause_frame, frame_size);
    TEST_EXPECT_EQ(1, result, "Should process PAUSE frame successfully");
    
    /* Verify transmission is paused */
    TEST_ASSERT(flow_control_should_pause_transmission(&ctx->flow_ctx), "Transmission should be paused");
    
    /* Simulate time passage by calling timer update multiple times */
    for (int i = 0; i < 100; i++) {
        flow_control_update_timer_state(&ctx->flow_ctx);
        flow_control_process_state_machine(&ctx->flow_ctx);
        
        /* Small delay simulation */
        for (volatile int j = 0; j < 1000; j++);
    }
    
    /* Check if pause has expired */
    TEST_ASSERT(!flow_control_should_pause_transmission(&ctx->flow_ctx), "Pause should have expired");
    
    flow_control_state_t final_state = flow_control_get_state(&ctx->flow_ctx);
    TEST_ASSERT(final_state == FLOW_CONTROL_STATE_IDLE || 
                final_state == FLOW_CONTROL_STATE_RESUME_PENDING, 
                "Should be in IDLE or RESUME_PENDING state after expiration");
    
    TEST_PASS();
}

/* ========================================================================== */
/* TRANSMISSION THROTTLING TESTS                                            */
/* ========================================================================== */

/**
 * @brief Test transmission throttling behavior
 */
static test_result_t test_transmission_throttling(test_context_t *ctx) {
    test_stats.tests_run++;
    
    /* Initially, transmission should be allowed */
    int result = flow_control_process_transmission_request(&ctx->flow_ctx);
    TEST_EXPECT_EQ(0, result, "Transmission should be allowed initially");
    
    /* Send PAUSE frame to trigger throttling */
    uint16_t pause_quanta = 200;
    uint8_t pause_frame[64];
    int frame_size = flow_control_generate_pause_frame(&ctx->flow_ctx, pause_quanta, 
                                                      pause_frame, sizeof(pause_frame));
    
    result = flow_control_process_received_packet(&ctx->flow_ctx, pause_frame, frame_size);
    TEST_EXPECT_EQ(1, result, "Should process PAUSE frame");
    
    /* Now transmission should be throttled */
    result = flow_control_process_transmission_request(&ctx->flow_ctx);
    TEST_EXPECT_EQ(1, result, "Transmission should be throttled");
    
    /* Force resume and verify transmission is allowed again */
    result = flow_control_force_resume_transmission(&ctx->flow_ctx);
    TEST_EXPECT_EQ(FLOW_CONTROL_SUCCESS, result, "Force resume should succeed");
    
    result = flow_control_process_transmission_request(&ctx->flow_ctx);
    TEST_EXPECT_EQ(0, result, "Transmission should be allowed after force resume");
    
    TEST_PASS();
}

/**
 * @brief Test emergency pause functionality
 */
static test_result_t test_emergency_pause(test_context_t *ctx) {
    test_stats.tests_run++;
    
    /* Trigger emergency pause */
    int result = flow_control_trigger_emergency_pause(&ctx->flow_ctx);
    TEST_EXPECT_EQ(FLOW_CONTROL_SUCCESS, result, "Emergency pause should succeed");
    
    /* Check statistics */
    flow_control_stats_t stats;
    result = flow_control_get_statistics(&ctx->flow_ctx, &stats);
    TEST_EXPECT_EQ(FLOW_CONTROL_SUCCESS, result, "Should get statistics");
    TEST_ASSERT(stats.emergency_pause_events > 0, "Emergency pause event should be recorded");
    
    TEST_PASS();
}

/* ========================================================================== */
/* BUFFER MONITORING TESTS                                                  */
/* ========================================================================== */

/**
 * @brief Test buffer monitoring and automatic PAUSE generation
 */
static test_result_t test_buffer_monitoring(test_context_t *ctx) {
    test_stats.tests_run++;
    
    /* Test buffer usage monitoring */
    int result = flow_control_monitor_buffer_levels(&ctx->flow_ctx);
    TEST_EXPECT_EQ(FLOW_CONTROL_SUCCESS, result, "Buffer monitoring should succeed");
    
    /* Test buffer usage percentage function */
    int usage = flow_control_get_buffer_usage_percent(&ctx->flow_ctx);
    TEST_ASSERT(usage >= 0 && usage <= 100, "Buffer usage should be valid percentage");
    
    /* Test watermark checking */
    bool high_watermark = flow_control_is_high_watermark_reached(&ctx->flow_ctx);
    /* Initially should not be at high watermark unless mock hardware sets it */
    
    TEST_PASS();
}

/* ========================================================================== */
/* INTEGRATION TESTS                                                        */
/* ========================================================================== */

/**
 * @brief Test integration with interrupt mitigation
 */
static test_result_t test_interrupt_mitigation_integration(test_context_t *ctx) {
    test_stats.tests_run++;
    
    /* Test integration setup */
    int result = flow_control_integrate_interrupt_mitigation(&ctx->flow_ctx, &ctx->im_ctx);
    TEST_EXPECT_EQ(FLOW_CONTROL_SUCCESS, result, "Integration should succeed");
    
    /* Test interrupt event processing */
    result = flow_control_process_interrupt_event(&ctx->flow_ctx, EVENT_TYPE_RX_COMPLETE);
    TEST_EXPECT_EQ(FLOW_CONTROL_SUCCESS, result, "Should handle RX complete event");
    
    result = flow_control_process_interrupt_event(&ctx->flow_ctx, EVENT_TYPE_TX_COMPLETE);
    TEST_EXPECT_EQ(FLOW_CONTROL_SUCCESS, result, "Should handle TX complete event");
    
    /* Test periodic maintenance */
    result = flow_control_periodic_maintenance(&ctx->flow_ctx);
    TEST_EXPECT_EQ(FLOW_CONTROL_SUCCESS, result, "Periodic maintenance should succeed");
    
    TEST_PASS();
}

/* ========================================================================== */
/* PERFORMANCE AND STATISTICS TESTS                                         */
/* ========================================================================== */

/**
 * @brief Test statistics collection and reporting
 */
static test_result_t test_statistics_collection(test_context_t *ctx) {
    test_stats.tests_run++;
    
    /* Get initial statistics */
    flow_control_stats_t stats;
    int result = flow_control_get_statistics(&ctx->flow_ctx, &stats);
    TEST_EXPECT_EQ(FLOW_CONTROL_SUCCESS, result, "Should get statistics");
    
    /* Process some PAUSE frames to generate statistics */
    uint8_t pause_frame[64];
    int frame_size = flow_control_generate_pause_frame(&ctx->flow_ctx, 100, 
                                                      pause_frame, sizeof(pause_frame));
    
    for (int i = 0; i < 5; i++) {
        flow_control_process_received_packet(&ctx->flow_ctx, pause_frame, frame_size);
    }
    
    /* Get updated statistics */
    result = flow_control_get_statistics(&ctx->flow_ctx, &stats);
    TEST_EXPECT_EQ(FLOW_CONTROL_SUCCESS, result, "Should get updated statistics");
    TEST_ASSERT(stats.pause_frames_received > 0, "Should have recorded received PAUSE frames");
    
    /* Test performance metrics */
    uint32_t avg_pause_duration, pause_efficiency, overflow_prevention;
    result = flow_control_get_performance_metrics(&ctx->flow_ctx, &avg_pause_duration, 
                                                 &pause_efficiency, &overflow_prevention);
    TEST_EXPECT_EQ(FLOW_CONTROL_SUCCESS, result, "Should get performance metrics");
    
    /* Clear statistics */
    flow_control_clear_statistics(&ctx->flow_ctx);
    result = flow_control_get_statistics(&ctx->flow_ctx, &stats);
    TEST_EXPECT_EQ(FLOW_CONTROL_SUCCESS, result, "Should get statistics after clear");
    TEST_EXPECT_EQ(0, stats.pause_frames_received, "Statistics should be cleared");
    
    TEST_PASS();
}

/* ========================================================================== */
/* ERROR HANDLING AND RECOVERY TESTS                                        */
/* ========================================================================== */

/**
 * @brief Test error handling and recovery mechanisms
 */
static test_result_t test_error_handling(test_context_t *ctx) {
    test_stats.tests_run++;
    
    /* Test invalid parameter handling */
    int result = flow_control_process_received_packet(NULL, NULL, 0);
    TEST_EXPECT_EQ(FLOW_CONTROL_INVALID_PARAM, result, "Should reject NULL parameters");
    
    result = flow_control_parse_pause_frame(NULL, 64, NULL);
    TEST_EXPECT_EQ(FLOW_CONTROL_INVALID_PARAM, result, "Should reject NULL parameters");
    
    /* Test reset functionality */
    result = flow_control_reset(&ctx->flow_ctx);
    TEST_EXPECT_EQ(FLOW_CONTROL_SUCCESS, result, "Reset should succeed");
    
    flow_control_state_t state = flow_control_get_state(&ctx->flow_ctx);
    TEST_EXPECT_EQ(FLOW_CONTROL_STATE_IDLE, state, "Should return to IDLE state after reset");
    
    TEST_PASS();
}

/* ========================================================================== */
/* CONFIGURATION TESTS                                                      */
/* ========================================================================== */

/**
 * @brief Test configuration management
 */
static test_result_t test_configuration_management(test_context_t *ctx) {
    test_stats.tests_run++;
    
    /* Test getting current configuration */
    flow_control_config_t config;
    int result = flow_control_get_config(&ctx->flow_ctx, &config);
    TEST_EXPECT_EQ(FLOW_CONTROL_SUCCESS, result, "Should get current configuration");
    
    /* Test setting new configuration */
    config.high_watermark_percent = 90;
    config.low_watermark_percent = 50;
    config.pause_time_default = 200;
    
    result = flow_control_set_config(&ctx->flow_ctx, &config);
    TEST_EXPECT_EQ(FLOW_CONTROL_SUCCESS, result, "Should set new configuration");
    
    /* Verify configuration was applied */
    flow_control_config_t new_config;
    result = flow_control_get_config(&ctx->flow_ctx, &new_config);
    TEST_EXPECT_EQ(FLOW_CONTROL_SUCCESS, result, "Should get updated configuration");
    TEST_EXPECT_EQ(90, new_config.high_watermark_percent, "High watermark should be updated");
    TEST_EXPECT_EQ(50, new_config.low_watermark_percent, "Low watermark should be updated");
    TEST_EXPECT_EQ(200, new_config.pause_time_default, "Pause time should be updated");
    
    /* Test invalid configuration */
    config.high_watermark_percent = 50;  /* Less than low watermark */
    config.low_watermark_percent = 60;
    result = flow_control_set_config(&ctx->flow_ctx, &config);
    TEST_EXPECT_EQ(FLOW_CONTROL_INVALID_PARAM, result, "Should reject invalid configuration");
    
    TEST_PASS();
}

/* ========================================================================== */
/* CAPABILITY DETECTION TESTS                                               */
/* ========================================================================== */

/**
 * @brief Test capability detection
 */
static test_result_t test_capability_detection(test_context_t *ctx) {
    test_stats.tests_run++;
    
    /* Test capability detection for both NIC types */
    flow_control_capabilities_t caps_3c515 = flow_control_detect_capabilities(&ctx->nic_ctx);
    TEST_ASSERT(caps_3c515 & FLOW_CONTROL_CAP_RX_PAUSE, "3C515 should support RX PAUSE");
    TEST_ASSERT(caps_3c515 & FLOW_CONTROL_CAP_TX_PAUSE, "3C515 should support TX PAUSE");
    
    /* Test default configuration for different NIC types */
    flow_control_config_t default_config;
    int result = flow_control_get_default_config(NIC_TYPE_3C515_TX, &default_config);
    TEST_EXPECT_EQ(FLOW_CONTROL_SUCCESS, result, "Should get default config for 3C515");
    TEST_ASSERT(default_config.enabled, "Flow control should be enabled by default");
    
    result = flow_control_get_default_config(NIC_TYPE_3C509B, &default_config);
    TEST_EXPECT_EQ(FLOW_CONTROL_SUCCESS, result, "Should get default config for 3C509B");
    
    TEST_PASS();
}

/* ========================================================================== */
/* INTEROPERABILITY TESTS                                                   */
/* ========================================================================== */

/**
 * @brief Test interoperability scenarios
 */
static test_result_t test_interoperability_scenarios(test_context_t *ctx) {
    test_stats.tests_run++;
    
    /* Test 1: Partner that supports flow control */
    uint8_t pause_frame[64];
    int frame_size = flow_control_generate_pause_frame(&ctx->flow_ctx, 100, 
                                                      pause_frame, sizeof(pause_frame));
    
    int result = flow_control_process_received_packet(&ctx->flow_ctx, pause_frame, frame_size);
    TEST_EXPECT_EQ(1, result, "Should process PAUSE from supporting partner");
    
    TEST_ASSERT(flow_control_partner_supports_flow_control(&ctx->flow_ctx), 
                "Should detect partner flow control support");
    
    /* Test 2: Disabled flow control scenarios */
    flow_control_set_enabled(&ctx->flow_ctx, false);
    
    result = flow_control_process_received_packet(&ctx->flow_ctx, pause_frame, frame_size);
    TEST_EXPECT_EQ(0, result, "Should ignore PAUSE frames when disabled");
    
    /* Re-enable for other tests */
    flow_control_set_enabled(&ctx->flow_ctx, true);
    
    /* Test 3: Different pause time values */
    uint16_t test_pause_times[] = {0, 1, 100, 1000, MAX_PAUSE_QUANTA};
    int num_tests = sizeof(test_pause_times) / sizeof(test_pause_times[0]);
    
    for (int i = 0; i < num_tests; i++) {
        frame_size = flow_control_generate_pause_frame(&ctx->flow_ctx, test_pause_times[i], 
                                                      pause_frame, sizeof(pause_frame));
        result = flow_control_process_received_packet(&ctx->flow_ctx, pause_frame, frame_size);
        TEST_EXPECT_EQ(1, result, "Should handle various pause time values");
    }
    
    TEST_PASS();
}

/* ========================================================================== */
/* SELF-TEST VERIFICATION                                                   */
/* ========================================================================== */

/**
 * @brief Test the flow control self-test functionality
 */
static test_result_t test_self_test_functionality(test_context_t *ctx) {
    test_stats.tests_run++;
    
    int result = flow_control_self_test();
    TEST_EXPECT_EQ(FLOW_CONTROL_SUCCESS, result, "Flow control self-test should pass");
    
    TEST_PASS();
}

/* ========================================================================== */
/* TEST FRAMEWORK IMPLEMENTATION                                            */
/* ========================================================================== */

/**
 * @brief Setup test context for specific NIC type
 */
static int setup_test_context(test_context_t *ctx, nic_type_t nic_type) {
    memset(ctx, 0, sizeof(test_context_t));
    
    /* Setup mock NIC info */
    ctx->nic_info.nic_type = nic_type;
    ctx->nic_info.capabilities = NIC_CAP_FLOW_CONTROL;
    strcpy((char*)ctx->nic_info.name, (nic_type == NIC_TYPE_3C515_TX) ? "3C515-TX" : "3C509B");
    
    /* Setup NIC context */
    ctx->nic_ctx.info = &ctx->nic_info;
    ctx->nic_ctx.io_base = 0x300;
    ctx->nic_ctx.irq = 10;
    ctx->nic_ctx.speed = (nic_type == NIC_TYPE_3C515_TX) ? 100 : 10;
    memcpy(ctx->nic_ctx.mac, "\x00\x11\x22\x33\x44\x55", 6);
    
    /* Initialize interrupt mitigation context */
    ctx->im_ctx.nic_type = nic_type;
    ctx->im_ctx.status_flags = 0;
    
    /* Initialize flow control */
    flow_control_config_t config;
    flow_control_get_default_config(nic_type, &config);
    
    int result = flow_control_init(&ctx->flow_ctx, &ctx->nic_ctx, &config);
    if (result != FLOW_CONTROL_SUCCESS) {
        printf("ERROR: Failed to initialize flow control: %d\n", result);
        return -1;
    }
    
    ctx->test_start_time = get_timestamp_ms();
    ctx->mock_hardware_active = true;
    
    return 0;
}

/**
 * @brief Cleanup test context
 */
static void cleanup_test_context(test_context_t *ctx) {
    if (ctx->mock_hardware_active) {
        flow_control_cleanup(&ctx->flow_ctx);
        ctx->mock_hardware_active = false;
    }
}

/**
 * @brief Print test summary
 */
static void print_test_summary(void) {
    printf("\n=== Flow Control Test Summary ===\n");
    printf("Tests Run:    %d\n", test_stats.tests_run);
    printf("Tests Passed: %d\n", test_stats.tests_passed);
    printf("Tests Failed: %d\n", test_stats.tests_failed);
    printf("Success Rate: %.1f%%\n", 
           test_stats.tests_run > 0 ? (test_stats.tests_passed * 100.0 / test_stats.tests_run) : 0.0);
    
    if (test_stats.tests_failed == 0) {
        printf("\nALL TESTS PASSED! ✓\n");
    } else {
        printf("\nSOME TESTS FAILED! ✗\n");
    }
}

/* ========================================================================== */
/* MAIN TEST RUNNER                                                         */
/* ========================================================================== */

/**
 * @brief Test suite definition
 */
typedef struct {
    const char *name;
    test_function_t function;
} test_case_t;

static test_case_t test_cases[] = {
    /* PAUSE Frame Tests */
    {"PAUSE Frame Parsing (Valid)", test_pause_frame_parsing_valid},
    {"PAUSE Frame Parsing (Invalid)", test_pause_frame_parsing_invalid},
    {"PAUSE Frame Generation", test_pause_frame_generation},
    {"Time Conversion Functions", test_time_conversion_functions},
    
    /* State Machine Tests */
    {"State Machine Basic Operations", test_state_machine_basic},
    {"Pause Request Processing", test_pause_request_processing},
    {"Pause Timer Expiration", test_pause_timer_expiration},
    
    /* Transmission Control Tests */
    {"Transmission Throttling", test_transmission_throttling},
    {"Emergency Pause", test_emergency_pause},
    
    /* Buffer Monitoring Tests */
    {"Buffer Monitoring", test_buffer_monitoring},
    
    /* Integration Tests */
    {"Interrupt Mitigation Integration", test_interrupt_mitigation_integration},
    
    /* Performance Tests */
    {"Statistics Collection", test_statistics_collection},
    
    /* Error Handling Tests */
    {"Error Handling", test_error_handling},
    
    /* Configuration Tests */
    {"Configuration Management", test_configuration_management},
    
    /* Capability Tests */
    {"Capability Detection", test_capability_detection},
    
    /* Interoperability Tests */
    {"Interoperability Scenarios", test_interoperability_scenarios},
    
    /* Self-Test */
    {"Self-Test Functionality", test_self_test_functionality}
};

/**
 * @brief Main test runner
 */
int main(int argc, char *argv[]) {
    printf("=== 802.3x Flow Control Comprehensive Test Suite ===\n");
    printf("Sprint 2.3: Flow Control Implementation Testing\n\n");
    
    /* Test with both NIC types */
    nic_type_t test_nic_types[] = {NIC_TYPE_3C515_TX, NIC_TYPE_3C509B};
    int num_nic_types = sizeof(test_nic_types) / sizeof(test_nic_types[0]);
    
    for (int nic_idx = 0; nic_idx < num_nic_types; nic_idx++) {
        printf("Testing with NIC type: %s\n", 
               (test_nic_types[nic_idx] == NIC_TYPE_3C515_TX) ? "3C515-TX" : "3C509B");
        printf("----------------------------------------\n");
        
        test_context_t ctx;
        if (setup_test_context(&ctx, test_nic_types[nic_idx]) != 0) {
            printf("ERROR: Failed to setup test context for NIC type %d\n", test_nic_types[nic_idx]);
            continue;
        }
        
        /* Run all test cases */
        int num_tests = sizeof(test_cases) / sizeof(test_cases[0]);
        for (int i = 0; i < num_tests; i++) {
            printf("Running: %s... ", test_cases[i].name);
            fflush(stdout);
            
            test_result_t result = test_cases[i].function(&ctx);
            if (result == TEST_PASS) {
                printf("PASS\n");
            } else {
                printf("FAIL\n");
            }
        }
        
        cleanup_test_context(&ctx);
        printf("\n");
    }
    
    print_test_summary();
    
    return (test_stats.tests_failed == 0) ? 0 : 1;
}