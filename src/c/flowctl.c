/**
 * @file flow_control.c
 * @brief Software 802.3x PAUSE handling (lightweight wrappers)
 *
 * Implements minimal, DOS-safe software flow control used by packet_ops.c:
 * - Detect PAUSE frames (Ethertype 0x8808, opcode 0x0001)
 * - Convert quanta to milliseconds based on link speed
 * - Throttle transmissions until pause expires
 * - High/low watermarks using reported buffer usage
 *
 * Notes:
 * - Runs in non-ISR context only (RX bottom-half / TX enqueue)
 * - Avoids DOS/BIOS calls; uses stats_get_timestamp() for ms timestamps
 */

#include <string.h>
#include "common.h"
#include "hardware.h"
#include "stats.h"
#include "logging.h"
#include "flowctl.h"

/* PAUSE destination multicast MAC */
static const uint8_t k_pause_dest[6] = {0x01, 0x80, 0xC2, 0x00, 0x00, 0x01};

typedef struct {
    bool     initialized;
    bool     enabled;                /* Global enable (runtime policy) */
    uint32_t pause_until_ms;         /* Timestamp when pause ends */
    uint16_t last_pause_quanta;      /* Last requested quanta */
    uint16_t buffer_usage_percent;   /* Latest buffer usage report */
    bool     high_water_active;      /* High-watermark driven pause */
} fc_state_t;

static fc_state_t g_fc_state[MAX_NICS];
static bool g_fc_initialized = false;

/* Convert quanta to milliseconds (integer, conservative rounding) */
static uint32_t fc_quanta_to_ms(uint16_t quanta, uint32_t link_speed_mbps) {
    if (link_speed_mbps == 0) link_speed_mbps = 10; /* Safe default */
    /* ms ≈ (quanta * 512 bit-times) / (Mbps * 1e3) */
    /* To avoid overflow: (quanta * 512) / (Mbps * 1000) */
    uint32_t num = (uint32_t)quanta * 512UL;
    uint32_t den = link_speed_mbps * 1000UL;
    uint32_t ms = (den > 0) ? (num + den - 1) / den : 0; /* round up */
    /* Safety cap */
    if (ms > MAX_PAUSE_DURATION_MS) ms = MAX_PAUSE_DURATION_MS;
    return ms;
}

int flow_control_init(void) {
    if (!g_fc_initialized) {
        int i;
        memset(g_fc_state, 0, sizeof(g_fc_state));
        for (i = 0; i < MAX_NICS; i++) {
            g_fc_state[i].enabled = true;
            g_fc_state[i].initialized = true;
        }
        g_fc_initialized = true;
        LOG_DEBUG("Flow control (software PAUSE) initialized for %d NIC slots", MAX_NICS);
    }
    return 0;
}

void flow_control_update_buffer_status(int nic_index, uint16_t usage_percent) {
    if (nic_index < 0 || nic_index >= MAX_NICS) return;
    fc_state_t *st = &g_fc_state[nic_index];
    if (!st->initialized) return;

    st->buffer_usage_percent = usage_percent;
    /* Hysteresis: activate ≥85%, deactivate <60% */
    if (!st->high_water_active && usage_percent >= FLOW_CONTROL_HIGH_WATERMARK) {
        st->high_water_active = true;
    } else if (st->high_water_active && usage_percent < FLOW_CONTROL_LOW_WATERMARK) {
        st->high_water_active = false;
    }
}

bool flow_control_should_pause_transmission(int nic_index) {
    if (nic_index < 0 || nic_index >= MAX_NICS) return false;
    fc_state_t *st = &g_fc_state[nic_index];
    if (!st->initialized || !st->enabled) return false;

    uint32_t now = stats_get_timestamp();
    bool pause_timer_active = (now < st->pause_until_ms);
    return pause_timer_active || st->high_water_active;
}

uint32_t flow_control_get_pause_duration(int nic_index) {
    if (nic_index < 0 || nic_index >= MAX_NICS) return 0;
    fc_state_t *st = &g_fc_state[nic_index];
    if (!st->initialized) return 0;
    uint32_t now = stats_get_timestamp();
    return (now < st->pause_until_ms) ? (st->pause_until_ms - now) : 0;
}

void flow_control_wait_for_resume(int nic_index, uint32_t pause_ms) {
    /* Simple bounded busy-wait using stats_get_timestamp; no DOS/BIOS calls */
    uint32_t start = stats_get_timestamp();
    uint32_t deadline = start + pause_ms;
    while (stats_get_timestamp() < deadline) {
        /* Tight spin; keep short to avoid blocking long periods */
        /* Break early if flow_control_should_pause_transmission() clears */
        if (!flow_control_should_pause_transmission(nic_index)) break;
    }
}

static bool fc_is_pause_dest(const uint8_t *mac) {
    int i;
    for (i = 0; i < 6; i++) {
        if (mac[i] != k_pause_dest[i]) return false;
    }
    return true;
}

int flow_control_process_received_packet(int nic_index,
                                         const uint8_t *packet,
                                         uint16_t length) {
    /* Minimal parser: Ethernet header (14 bytes) + MAC Control payload */
    if (nic_index < 0 || nic_index >= MAX_NICS || !packet || length < 18) {
        return 0; /* Not a PAUSE frame */
    }

    fc_state_t *st = &g_fc_state[nic_index];
    if (!st->initialized || !st->enabled) return 0;

    /* EtherType at bytes 12-13 (big endian) */
    uint16_t ethertype = ((uint16_t)packet[12] << 8) | packet[13];
    if (ethertype != FLOW_CONTROL_ETHERTYPE) return 0;

    /* Destination MAC must be 01:80:C2:00:00:01 */
    if (!fc_is_pause_dest(packet)) return 0;

    /* Opcode at bytes 14-15; Pause time at 16-17 */
    if (length < 18) return 0;
    uint16_t opcode = ((uint16_t)packet[14] << 8) | packet[15];
    if (opcode != PAUSE_FRAME_OPCODE) return 0; /* Ignore other MAC control */

    uint16_t pause_quanta = ((uint16_t)packet[16] << 8) | packet[17];

    /* Calculate milliseconds from quanta based on NIC speed */
    nic_info_t *nic = hardware_get_nic(nic_index);
    uint32_t link_speed = (nic && nic->speed > 0) ? (uint32_t)nic->speed : 10UL; /* Mbps */
    uint32_t pause_ms = fc_quanta_to_ms(pause_quanta, link_speed);

    uint32_t now = stats_get_timestamp();
    st->last_pause_quanta = pause_quanta;
    st->pause_until_ms = now + pause_ms;

    LOG_DEBUG("Flow control: PAUSE quanta=%u (≈%lums) on NIC %d", pause_quanta, pause_ms, nic_index);
    return 1; /* PAUSE handled */
}
/* Place entire module in cold code segment (discardable after init). */
#pragma code_seg ("COLD_TEXT", "CODE")
