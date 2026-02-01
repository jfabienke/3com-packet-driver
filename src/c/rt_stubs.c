/**
 * @file rt_stubs.c
 * @brief Consolidated runtime stubs replacing 15 individual *_rt.c files
 *
 * Phase 6: Remove C _rt modules. This file provides global variable
 * definitions and minimal stub functions so that the 15 *_init.c files
 * (which have extern references to these symbols) continue to link.
 * At runtime, JIT ASM modules do the real work.
 *
 * Replaces: hardware_rt.c, 3c509b_rt.c, 3c515_rt.c, api_rt.c,
 *   dmabnd_rt.c, dmamap_rt.c, pci_shim_rt.c, pcimux_rt.c, hwchksm_rt.c,
 *   irqmit_rt.c, rxbatch_rt.c, txlazy_rt.c, xms_core_rt.c, pktops_rt.c,
 *   logging_rt.c
 *
 * Last Updated: 2026-02-01 14:15:00 CET
 */

/* Keep includes minimal - only what we need for type definitions */
#include <stddef.h>
#include <string.h>

/* Portability header for bool, uint types */
#include "portabl.h"

/* Type definitions from project headers */
#include "hardware.h"    /* nic_info_t, nic_ops_t, MAX_NICS */
#include "api.h"         /* extended_packet_handle_t, pd_load_balance_params_t, pd_qos_params_t */
#include "dmabnd.h"      /* bounce_pool_t, dma_boundary_stats_t, dma_check_result_t */
#include "dmamap.h"      /* dma_mapping_t, dma_mapping_stats_t */
#include "irqmit.h"      /* interrupt_mitigation_context_t */
#include "hwchksm.h"     /* checksum_mode_t, checksum_stats_t */
#include "xms_alloc.h"   /* xms_block_t */
#include "dos_io.h"      /* dos_file_t */
#include "logging.h"     /* log level constants */
#include "pci_bios.h"    /* PCI types */
#include "pci_shim.h"    /* pci_shim types */
#include "common.h"      /* common types */
#include "pktops.h"      /* eth_header_t, packet function signatures */

/* ============================================================================
 * Types defined locally in the old _rt.c files (not in headers)
 * These must match the duplicate definitions in the _init.c files
 * ============================================================================ */

/* From api_rt.c - pd_handle_t (also defined in api.c) */
#ifndef PD_MAX_HANDLES
#define PD_MAX_HANDLES 16
#endif
#ifndef PD_MAX_EXTENDED_HANDLES
#define PD_MAX_EXTENDED_HANDLES 16
#endif

typedef struct {
    uint16_t handle;
    uint16_t packet_type;
    uint8_t class;
    uint8_t number;
    uint8_t type;
    uint8_t flags;
    void far *receiver;
    uint32_t packets_received;
    uint32_t packets_dropped;
    uint32_t packets_sent;
    uint32_t bytes_received;
    uint32_t bytes_sent;
} pd_handle_t;

/* From rxbatch_rt.c */
typedef struct {
    uint32_t next;
    uint32_t status;
    uint32_t buf_addr;
    uint32_t buf_len;
} rx_desc_t;

#ifndef RX_RING_SIZE
#define RX_RING_SIZE 32
#endif

typedef struct {
    rx_desc_t *ring;
    uint32_t ring_phys;
    uint16_t head;
    uint16_t tail;
    uint16_t available;
    uint16_t io_base;
    uint8_t nic_index;
    bool enabled;
    void far *buffer_virt[RX_RING_SIZE];
    uint32_t buffer_phys[RX_RING_SIZE];
    uint16_t buffer_size[RX_RING_SIZE];
    uint32_t total_packets;
    uint32_t copy_break_count;
    uint32_t bulk_refills;
    uint32_t doorbell_writes;
    uint32_t empty_events;
    uint32_t boundary_avoided;
    uint32_t boundary_retry_exhausted;
    uint16_t last_refill_count;
} rx_batch_state_t;

/* From txlazy_rt.c */
typedef struct {
    uint32_t next_addr;
    uint32_t frame_start_hdr;
    uint32_t buf_addr;
    uint32_t buf_length;
} boomerang_tx_desc_t;

typedef struct {
    boomerang_tx_desc_t *ring;
    uint32_t ring_phys;
    uint16_t head;
    uint16_t tail;
    uint16_t io_base;
    uint8_t nic_index;
    uint8_t enabled;
    uint16_t tx_since_irq;
    uint16_t tx_inflight;
    uint16_t last_irq_desc;
    uint8_t force_next_irq;
    uint32_t total_packets;
    uint32_t total_interrupts;
    uint32_t empty_queue_irqs;
    uint32_t threshold_irqs;
    uint32_t high_water_irqs;
    uint32_t interrupts_saved;
    uint32_t ring_full_events;
} tx_lazy_state_t;

/* From pci_shim_rt.c */
struct pci_shim_state {
    void (__interrupt __far *original_int1a)();
    bool installed;
    uint8_t mechanism;
    uint16_t broken_functions;
    uint32_t shim_calls;
    uint32_t fallback_calls;
};

/* From pcimux_rt.c */
struct mplex_state_t {
    void (__interrupt __far *old_int2f)();
    bool installed;
    bool shim_enabled;
    uint32_t mplex_calls;
};

/* Log buffer size */
#ifndef LOG_BUFFER_SIZE
#define LOG_BUFFER_SIZE 256
#endif

/* ============================================================================
 * SECTION: hardware_rt globals and stubs
 * ============================================================================ */
nic_info_t g_nic_infos[MAX_NICS];
int g_num_nics = 0;
bool g_hardware_initialized = false;

int hardware_get_nic_count(void) { return g_num_nics; }
nic_info_t *hardware_get_nic(int index) {
    if (index < 0 || index >= MAX_NICS) return NULL;
    return &g_nic_infos[index];
}
nic_info_t *hardware_get_primary_nic(void) {
    return g_num_nics > 0 ? &g_nic_infos[0] : NULL;
}
nic_info_t *hardware_find_nic_by_type(nic_type_t type) { (void)type; return NULL; }
nic_info_t *hardware_find_nic_by_mac(const uint8_t *mac) { (void)mac; return NULL; }
int hardware_send_packet(nic_info_t *nic, const uint8_t *packet, size_t length) {
    (void)nic; (void)packet; (void)length; return -1;
}
int hardware_receive_packet(nic_info_t *nic, uint8_t *buffer, size_t *length) {
    (void)nic; (void)buffer; (void)length; return -1;
}
int hardware_enable_interrupts(nic_info_t *nic) { (void)nic; return 0; }
int hardware_disable_interrupts(nic_info_t *nic) { (void)nic; return 0; }
int hardware_clear_interrupts(nic_info_t *nic) { (void)nic; return 0; }
int hardware_get_link_status(nic_info_t *nic) { (void)nic; return 0; }
int hardware_get_link_speed(nic_info_t *nic) { (void)nic; return 10; }
int hardware_is_link_up(nic_info_t *nic) { (void)nic; return 0; }
void hardware_get_stats(nic_info_t *nic, void *stats) { (void)nic; (void)stats; }
void hardware_clear_stats(nic_info_t *nic) { (void)nic; }
int hardware_set_promiscuous_mode(nic_info_t *nic, bool enable) { (void)nic; (void)enable; return 0; }
int hardware_set_multicast_filter(nic_info_t *nic, const uint8_t *mc_list, int count) {
    (void)nic; (void)mc_list; (void)count; return 0;
}
int hardware_self_test_nic(nic_info_t *nic) { (void)nic; return 0; }
void hardware_print_nic_info(const nic_info_t *nic) { (void)nic; }
bool hardware_is_nic_present(int index) { (void)index; return false; }
bool hardware_is_nic_active(int index) { (void)index; return false; }

/* ============================================================================
 * SECTION: 3c509b_rt globals and stubs
 * ============================================================================ */
uint16_t _3c509b_read_reg(nic_info_t *nic, uint16_t reg) { (void)nic; (void)reg; return 0; }
void _3c509b_write_reg(nic_info_t *nic, uint16_t reg, uint16_t value) { (void)nic; (void)reg; (void)value; }
void _3c509b_select_window(nic_info_t *nic, uint8_t window) { (void)nic; (void)window; }
int _3c509b_wait_for_cmd_busy(nic_info_t *nic, uint32_t timeout_ms) { (void)nic; (void)timeout_ms; return 0; }
void _3c509b_write_command(nic_info_t *nic, uint16_t command) { (void)nic; (void)command; }
int _3c509b_send_packet(nic_info_t *nic, const uint8_t *packet, size_t length) {
    (void)nic; (void)packet; (void)length; return -1;
}
int _3c509b_receive_packet(nic_info_t *nic, uint8_t *buffer, size_t *length) {
    (void)nic; (void)buffer; (void)length; return -1;
}
int _3c509b_receive_packet_buffered(nic_info_t *nic, uint8_t *buffer, size_t *length) {
    (void)nic; (void)buffer; (void)length; return -1;
}
int _3c509b_check_interrupt(nic_info_t *nic) { (void)nic; return 0; }
void _3c509b_handle_interrupt(nic_info_t *nic) { (void)nic; }
int _3c509b_process_single_event(nic_info_t *nic) { (void)nic; return 0; }
int _3c509b_check_interrupt_batched(nic_info_t *nic) { (void)nic; return 0; }
void _3c509b_handle_interrupt_batched(nic_info_t *nic) { (void)nic; }
int _3c509b_enable_interrupts(nic_info_t *nic) { (void)nic; return 0; }
int _3c509b_disable_interrupts(nic_info_t *nic) { (void)nic; return 0; }
int _3c509b_get_link_status(nic_info_t *nic) { (void)nic; return 0; }
int _3c509b_get_link_speed(nic_info_t *nic) { (void)nic; return 10; }
int _3c509b_set_promiscuous(nic_info_t *nic, bool enable) { (void)nic; (void)enable; return 0; }
int _3c509b_set_multicast(nic_info_t *nic, const uint8_t *mc_list, int count) {
    (void)nic; (void)mc_list; (void)count; return 0;
}
int send_packet_direct_pio(nic_info_t *nic, const uint8_t *packet, size_t length) {
    (void)nic; (void)packet; (void)length; return -1;
}
int send_packet_direct_pio_with_header(nic_info_t *nic, const uint8_t *hdr, size_t hdr_len, const uint8_t *payload, size_t pay_len) {
    (void)nic; (void)hdr; (void)hdr_len; (void)payload; (void)pay_len; return -1;
}
int _3c509b_send_packet_direct_pio(nic_info_t *nic, const uint8_t *packet, size_t length) {
    (void)nic; (void)packet; (void)length; return -1;
}
int _3c509b_pio_prepare_rx_buffer(nic_info_t *nic, uint8_t *buffer, size_t size) {
    (void)nic; (void)buffer; (void)size; return -1;
}
int _3c509b_pio_complete_rx_buffer(nic_info_t *nic, uint8_t *buffer, size_t *actual_size) {
    (void)nic; (void)buffer; (void)actual_size; return -1;
}
int _3c509b_pio_prepare_tx_buffer(nic_info_t *nic, const uint8_t *data, size_t size) {
    (void)nic; (void)data; (void)size; return -1;
}
int _3c509b_receive_packet_cache_safe(nic_info_t *nic, uint8_t *buffer, size_t *length) {
    (void)nic; (void)buffer; (void)length; return -1;
}

/* ============================================================================
 * SECTION: 3c515_rt globals and stubs
 * ============================================================================ */
int _3c515_dma_prepare_buffers(nic_info_t *nic) { (void)nic; return 0; }
void _3c515_dma_complete_buffers(nic_info_t *nic) { (void)nic; }
int _3c515_send_packet(nic_info_t *nic, const uint8_t *packet, size_t len) {
    (void)nic; (void)packet; (void)len; return -1;
}
int _3c515_receive_packet(nic_info_t *nic, uint8_t *buffer, size_t *len) {
    (void)nic; (void)buffer; (void)len; return -1;
}
void _3c515_handle_interrupt(nic_info_t *nic) { (void)nic; }
int _3c515_check_interrupt(nic_info_t *nic) { (void)nic; return 0; }
int _3c515_process_single_event(nic_info_t *nic) { (void)nic; return 0; }
void _3c515_handle_interrupt_batched(nic_info_t *nic) { (void)nic; }
int _3c515_enable_interrupts(nic_info_t *nic) { (void)nic; return 0; }
int _3c515_disable_interrupts(nic_info_t *nic) { (void)nic; return 0; }
int _3c515_get_link_status(nic_info_t *nic) { (void)nic; return 0; }
int _3c515_get_link_speed(nic_info_t *nic) { (void)nic; return 10; }

/* ============================================================================
 * SECTION: api_rt globals and stubs
 * ============================================================================ */
pd_handle_t handles[PD_MAX_HANDLES];
extended_packet_handle_t extended_handles[PD_MAX_EXTENDED_HANDLES];
int next_handle = 0;
int api_initialized = 0;
int extended_api_initialized = 0;
volatile int api_ready = 0;
int load_balancing_enabled = 0;
int qos_enabled = 0;
int virtual_interrupts_enabled = 0;
uint32_t global_bandwidth_limit = 0;
pd_load_balance_params_t global_lb_config;
pd_qos_params_t default_qos_params;
uint32_t nic_weights[MAX_NICS];
uint32_t nic_utilization[MAX_NICS];
uint32_t nic_error_counts[MAX_NICS];
uint32_t last_nic_used = 0;

int pd_access_type(uint8_t function, uint16_t handle, void *params) {
    (void)function; (void)handle; (void)params; return -1;
}
int pd_get_driver_info(void *info_ptr) { (void)info_ptr; return 0; }
int pd_handle_access_type(void *params) { (void)params; return -1; }
int pd_release_handle(uint16_t handle) { (void)handle; return -1; }
int pd_send_packet(uint16_t handle, void *params) {
    (void)handle; (void)params; return -1;
}
int pd_terminate(uint16_t handle) { (void)handle; return -1; }
int pd_get_address(uint16_t handle, void *params) {
    (void)handle; (void)params; return -1;
}
int pd_reset_interface(uint16_t handle) { (void)handle; return 0; }
int pd_get_parameters(uint16_t handle, void *params) { (void)handle; (void)params; return 0; }
int pd_set_rcv_mode(uint16_t handle, void *params) { (void)handle; (void)params; return 0; }
int pd_get_rcv_mode(uint16_t handle, void *params) { (void)handle; (void)params; return 0; }
int pd_get_statistics(uint16_t handle, void *params) { (void)handle; (void)params; return 0; }
int pd_set_address(uint16_t handle, void *params) { (void)handle; (void)params; return -1; }
int pd_validate_handle(uint16_t handle) { (void)handle; return 0; }
int api_process_received_packet(const uint8_t *packet, size_t length, int nic_id) {
    (void)packet; (void)length; (void)nic_id; return 0;
}
int api_init_extended_handles(void) { return 0; }
int api_cleanup_extended_handles(void) { return 0; }
int api_get_extended_handle(uint16_t handle, extended_packet_handle_t **ext_handle) {
    (void)handle; (void)ext_handle; return -1;
}
int api_upgrade_handle(uint16_t handle) { (void)handle; return -1; }
int pd_set_handle_priority(uint16_t handle, void *params) { (void)handle; (void)params; return 0; }
int pd_get_routing_info(uint16_t handle, void *params) { (void)handle; (void)params; return 0; }
int pd_set_load_balance(uint16_t handle, void *params) { (void)handle; (void)params; return 0; }
int pd_get_nic_status(uint16_t handle, void *params) { (void)handle; (void)params; return 0; }
int pd_set_qos_params(uint16_t handle, void *params) { (void)handle; (void)params; return 0; }
int pd_get_flow_stats(uint16_t handle, void *params) { (void)handle; (void)params; return 0; }
int pd_set_nic_preference(uint16_t handle, void *params) { (void)handle; (void)params; return 0; }
int pd_get_handle_info(uint16_t handle, void *params) { (void)handle; (void)params; return 0; }
int pd_set_bandwidth_limit(uint16_t handle, void *params) { (void)handle; (void)params; return 0; }
int pd_get_error_info(uint16_t handle, void *params) { (void)handle; (void)params; return 0; }
int api_select_optimal_nic(uint16_t handle, const uint8_t *packet, uint8_t *selected_nic) {
    (void)handle; (void)packet; (void)selected_nic; return 0;
}
int api_check_bandwidth_limit(uint16_t handle, uint32_t packet_size) { (void)handle; (void)packet_size; return 1; }
int api_handle_nic_failure(uint8_t failed_nic) { (void)failed_nic; return 0; }
int api_coordinate_recovery_with_routing(uint8_t failed_nic) { (void)failed_nic; return 0; }
int api_update_nic_utilization(uint8_t nic_index, uint32_t packet_size) { (void)nic_index; (void)packet_size; return 0; }

/* ============================================================================
 * SECTION: dmabnd_rt globals and stubs
 * ============================================================================ */
bounce_pool_t g_tx_bounce_pool;
bounce_pool_t g_rx_bounce_pool;
bool g_bounce_pools_initialized = false;
dma_boundary_stats_t g_boundary_stats;
bool g_v86_mode_detected = false;
bool g_dpmi_available = false;
bool g_memory_manager_detected = false;

int dma_check_buffer_safety(void *buffer, size_t len, dma_check_result_t *result) {
    (void)buffer; (void)len; if (result) memset(result, 0, sizeof(*result)); return 0;
}
void *dma_get_tx_bounce_buffer(size_t size) { (void)size; return NULL; }
void dma_release_tx_bounce_buffer(void *buf) { (void)buf; }
void *dma_get_rx_bounce_buffer(size_t size) { (void)size; return NULL; }
void dma_release_rx_bounce_buffer(void *buf) { (void)buf; }
void dma_get_boundary_stats(dma_boundary_stats_t *stats) {
    if (stats) memset(stats, 0, sizeof(*stats));
}
int is_safe_for_direct_dma(void *buf, size_t len) { (void)buf; (void)len; return 1; }

/* ============================================================================
 * SECTION: dmamap_rt globals and stubs
 * ============================================================================ */
dma_mapping_stats_t g_dmamap_stats;
bool g_fast_path_enabled = false;
uint32_t g_cache_hits = 0;
uint32_t g_cache_attempts = 0;

dma_mapping_t *dma_map_tx(void *buf, size_t len) { (void)buf; (void)len; return NULL; }
dma_mapping_t *dma_map_tx_flags(void *buf, size_t len, uint32_t flags) {
    (void)buf; (void)len; (void)flags; return NULL;
}
void dma_unmap_tx(dma_mapping_t *m) { (void)m; }
dma_mapping_t *dma_map_rx(void *buf, size_t len) { (void)buf; (void)len; return NULL; }
dma_mapping_t *dma_map_rx_flags(void *buf, size_t len, uint32_t flags) {
    (void)buf; (void)len; (void)flags; return NULL;
}
void dma_unmap_rx(dma_mapping_t *m) { (void)m; }
dma_mapping_t *dma_map_buffer(void *buf, size_t len, dma_sync_direction_t dir) {
    (void)buf; (void)len; (void)dir; return NULL;
}
dma_mapping_t *dma_map_buffer_flags(void *buf, size_t len, dma_sync_direction_t dir, uint32_t flags) {
    (void)buf; (void)len; (void)dir; (void)flags; return NULL;
}
void dma_unmap_buffer(dma_mapping_t *m) { (void)m; }
void *dma_mapping_get_address(const dma_mapping_t *m) { (void)m; return NULL; }
uint32_t dma_mapping_get_phys_addr(const dma_mapping_t *m) { (void)m; return 0; }
size_t dma_mapping_get_length(const dma_mapping_t *m) { (void)m; return 0; }
int dma_mapping_uses_bounce(const dma_mapping_t *m) { (void)m; return 0; }
int dma_mapping_is_coherent(const dma_mapping_t *m) { (void)m; return 0; }
int dma_mapping_uses_vds(const dma_mapping_t *m) { (void)m; return 0; }
int dma_mapping_sync_for_device(dma_mapping_t *m) { (void)m; return 0; }
int dma_mapping_sync_for_cpu(dma_mapping_t *m) { (void)m; return 0; }
int dma_mapping_is_fast_path_enabled(void) { return 0; }
uint32_t dma_mapping_get_cache_hit_rate(void) { return 0; }

/* ============================================================================
 * SECTION: pci_shim_rt globals and stubs
 * ============================================================================ */
struct pci_shim_state shim_state;

void pci_shim_get_stats(uint32_t *calls, uint32_t *fallbacks) {
    if (calls) *calls = 0;
    if (fallbacks) *fallbacks = 0;
}

/* ============================================================================
 * SECTION: pcimux_rt globals and stubs
 * ============================================================================ */
struct mplex_state_t mplex_state;

int multiplex_is_shim_enabled(void) { return 0; }
void multiplex_set_shim_enabled(bool enable) { (void)enable; }
void multiplex_get_stats(uint32_t *calls) { if (calls) *calls = 0; }

/* ============================================================================
 * SECTION: hwchksm_rt globals and stubs
 * ============================================================================ */
bool checksum_system_initialized = false;
checksum_mode_t global_checksum_mode = 0;
checksum_stats_t global_checksum_stats;
uint16_t checksum_optimization_flags = 0;

int hw_checksum_tx_calculate(struct nic_context *ctx, uint8_t *packet, uint16_t length, uint32_t protocols) {
    (void)ctx; (void)packet; (void)length; (void)protocols; return 0;
}
int hw_checksum_rx_validate(struct nic_context *ctx, const uint8_t *packet, uint16_t length, uint32_t *result_mask) {
    (void)ctx; (void)packet; (void)length; (void)result_mask; return 0;
}
uint16_t sw_checksum_internet(const uint8_t *data, uint16_t length, uint32_t initial) {
    (void)data; (void)length; (void)initial; return 0;
}
bool hw_checksum_is_supported(struct nic_context *ctx, checksum_protocol_t protocol) {
    (void)ctx; (void)protocol; return false;
}
checksum_mode_t hw_checksum_get_optimal_mode(struct nic_context *ctx, checksum_protocol_t protocol) {
    (void)ctx; (void)protocol; return 0;
}
int hw_checksum_get_stats(checksum_stats_t *stats) {
    if (stats) memset(stats, 0, sizeof(*stats)); return 0;
}
int hw_checksum_clear_stats(void) { return 0; }
void hw_checksum_print_stats(void) {}
int hw_checksum_calculate_ip(uint8_t *ip_header, uint16_t header_length) {
    (void)ip_header; (void)header_length; return 0;
}
checksum_result_t hw_checksum_validate_ip(const uint8_t *ip_header, uint16_t header_length) {
    (void)ip_header; (void)header_length; return 0;
}
const char *hw_checksum_mode_to_string(checksum_mode_t mode) {
    (void)mode; return "Unknown";
}

/* ============================================================================
 * SECTION: irqmit_rt globals and stubs
 * ============================================================================ */
interrupt_mitigation_context_t g_mitigation_contexts[MAX_NICS];
bool g_mitigation_initialized = false;
uint8_t g_mitigation_batch = 0;
uint8_t g_mitigation_timeout = 0;

bool is_interrupt_mitigation_enabled(interrupt_mitigation_context_t *ctx) { (void)ctx; return false; }
interrupt_mitigation_context_t *get_mitigation_context(uint8_t nic_index) {
    if (nic_index >= MAX_NICS) return NULL;
    return &g_mitigation_contexts[nic_index];
}
void interrupt_mitigation_apply_runtime(interrupt_mitigation_context_t *ctx) { (void)ctx; }

/* ============================================================================
 * SECTION: rxbatch_rt globals and stubs
 * ============================================================================ */
rx_batch_state_t g_rx_state[MAX_NICS];
bool g_rx_batch_initialized = false;
uint16_t g_copy_break_threshold = 256;

void far *rx_alloc_64k_safe(uint16_t len, uint32_t *phys_out) {
    (void)len; if (phys_out) *phys_out = 0; return NULL;
}
int rx_batch_refill(uint8_t nic_index) { (void)nic_index; return 0; }
int rx_batch_process(uint8_t nic_index) { (void)nic_index; return 0; }
void rx_batch_get_stats(uint8_t nic_index, void *stats) { (void)nic_index; (void)stats; }

/* ============================================================================
 * SECTION: txlazy_rt globals and stubs
 * ============================================================================ */
tx_lazy_state_t g_lazy_tx_state[MAX_NICS];
uint8_t g_tx_lazy_initialized = 0;

int tx_lazy_should_interrupt(uint8_t nic_index) { (void)nic_index; return 1; }
int tx_lazy_post_boomerang(uint8_t nic_index, void *desc) { (void)nic_index; (void)desc; return 0; }
int tx_lazy_post_vortex(uint8_t nic_index, void *desc) { (void)nic_index; (void)desc; return 0; }
int tx_lazy_reclaim_batch(uint8_t nic_index) { (void)nic_index; return 0; }

/* ============================================================================
 * SECTION: xms_core_rt globals and stubs
 * ============================================================================ */
int g_xms_available = 0;
uint16_t g_xms_version = 0;
uint32_t g_xms_free_kb = 0;
uint32_t g_xms_largest_block_kb = 0;
void (far *g_xms_entry)(void) = NULL;
xms_block_t g_promisc_xms;
xms_block_t g_routing_xms;
int g_xms_initialized = 0;
char g_xms_unavail_reason[64] = {0};

int xms_lock(xms_block_t *block) { (void)block; return -1; }
int xms_unlock(xms_block_t *block) { (void)block; return -1; }
int xms_copy(xms_block_t *block, uint32_t offset, void far *conv_buf, uint32_t size, int to_xms) {
    (void)block; (void)offset; (void)conv_buf; (void)size; (void)to_xms; return -1;
}
int xms_query_free(uint32_t *free_kb, uint32_t *largest_kb) {
    if (free_kb) *free_kb = 0;
    if (largest_kb) *largest_kb = 0;
    return -1;
}
int xms_enable_a20(void) { return -1; }
int xms_disable_a20(void) { return -1; }
int xms_query_a20(void) { return 0; }
int xms_promisc_available(void) { return 0; }
int xms_routing_available(void) { return 0; }
const char *xms_unavailable_reason(void) { return g_xms_unavail_reason; }

/* ============================================================================
 * SECTION: pktops_rt globals and stubs
 * (pktops_rt.c was the largest - many functions)
 * ============================================================================ */
/* Note: Many pktops functions are already in pktops.c/pktops.asm.
 * Only stubs for functions unique to pktops_rt.c */
uint16_t packet_get_ethertype(const uint8_t *packet) {
    (void)packet; return 0;
}
int packet_queue_tx_completion(uint8_t nic_index, int status) {
    (void)nic_index; (void)status; return 0;
}
int packet_test_internal_loopback(int nic_index, const uint8_t *test_pattern, uint16_t pattern_size) {
    (void)nic_index; (void)test_pattern; (void)pattern_size; return -1;
}

/* ============================================================================
 * SECTION: logging_rt globals and stubs
 * ============================================================================ */
int logging_enabled = 0;
int log_level = 0;
char log_buffer[LOG_BUFFER_SIZE] = {0};
char *ring_buffer = NULL;
int ring_buffer_size = 0;
int ring_write_pos = 0;
int ring_read_pos = 0;
int ring_entries = 0;
int ring_wrapped = 0;
int ring_enabled = 0;
int category_filter = 0;
unsigned long log_entries_written = 0;
unsigned long log_entries_dropped = 0;
unsigned long log_buffer_overruns = 0;
int log_to_console = 0;
int log_to_file = 0;
int log_to_network = 0;
char log_filename[1] = {0};
dos_file_t log_file = -1;
char network_log_host[1] = {0};
int network_log_port = 0;
int network_log_protocol = 0;

void log_debug(const char *fmt, ...) { (void)fmt; }
void log_info(const char *fmt, ...) { (void)fmt; }
void log_warning(const char *fmt, ...) { (void)fmt; }
void log_error(const char *fmt, ...) { (void)fmt; }
void log_critical(const char *fmt, ...) { (void)fmt; }
void log_at_level(int level, const char *format, ...) { (void)level; (void)format; }
void log_debug_category(int category, const char *format, ...) { (void)category; (void)format; }
void log_warning_category(int category, const char *format, ...) { (void)category; (void)format; }
void log_error_category(int category, const char *format, ...) { (void)category; (void)format; }
int log_read_ring_buffer(char *output, int max_len) { (void)output; (void)max_len; return 0; }
void logging_get_stats(unsigned long *written, unsigned long *dropped, unsigned long *overruns) {
    if (written) *written = 0;
    if (dropped) *dropped = 0;
    if (overruns) *overruns = 0;
}
int logging_ring_buffer_enabled(void) { return 0; }
int logging_is_enabled(void) { return 0; }
int logging_get_level(void) { return 0; }
void logging_get_config(int *level, int *categories, int *outputs) {
    if (level) *level = 0;
    if (categories) *categories = 0;
    if (outputs) *outputs = 0;
}

/* ============================================================================
 * SECTION: pktops_rt packet operation stubs
 * These were previously in pktops.c (pktops_c.obj) which is not linked.
 * ============================================================================ */
int packet_send_enhanced(uint8_t interface_num, const uint8_t *packet_data,
                        uint16_t length, const uint8_t *dest_addr, uint16_t handle) {
    (void)interface_num; (void)packet_data; (void)length; (void)dest_addr; (void)handle;
    return -1;
}
int packet_receive_from_nic(int nic_index, uint8_t *buffer, size_t *length) {
    (void)nic_index; (void)buffer; (void)length; return -1;
}
int packet_receive_process(uint8_t *raw_data, uint16_t length, uint8_t nic_index) {
    (void)raw_data; (void)length; (void)nic_index; return 0;
}
void packet_process_deferred_work(void) {}
int packet_isr_receive(uint8_t *packet_data, uint16_t packet_size, uint8_t nic_index) {
    (void)packet_data; (void)packet_size; (void)nic_index; return 0;
}
int packet_build_ethernet_frame(uint8_t *frame_buffer, uint16_t frame_size,
                               const uint8_t *dest_mac, const uint8_t *src_mac,
                               uint16_t ethertype, const uint8_t *payload,
                               uint16_t payload_len) {
    (void)frame_buffer; (void)frame_size; (void)dest_mac; (void)src_mac;
    (void)ethertype; (void)payload; (void)payload_len; return -1;
}
int packet_parse_ethernet_header(const uint8_t *frame_data, uint16_t frame_len,
                                eth_header_t *header) {
    (void)frame_data; (void)frame_len; (void)header; return -1;
}
int packet_send_with_retry(const uint8_t *packet_data, uint16_t length,
                          const uint8_t *dest_addr, uint16_t handle,
                          int max_retries) {
    (void)packet_data; (void)length; (void)dest_addr; (void)handle; (void)max_retries;
    return -1;
}

/* ============================================================================
 * SECTION: ISR handler stubs (interrupt calling convention)
 * multiplex_handler: INT 2Fh handler from pcimux_rt.c
 * pci_shim_handler: INT 1Ah handler from pci_shim_rt.c
 * ============================================================================ */
void __interrupt __far multiplex_handler(void) {}
void __interrupt __far pci_shim_handler(void) {}
