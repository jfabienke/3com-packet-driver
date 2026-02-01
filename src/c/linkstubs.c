/**
 * @file linkstubs.c
 * @brief Stub implementations for undefined symbols
 *
 * 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
 *
 * This file provides minimal stub implementations to resolve undefined
 * symbols during linking. These stubs allow the overlay system to
 * be tested before all modules are fully implemented.
 *
 * IMPORTANT: This file intentionally does NOT include most project headers
 * to avoid signature conflicts. Stubs are defined with simple types.
 *
 * PHASE 5 CLEANUP (2026-01-27):
 * - Removed functions now provided via linkasm.asm trampolines
 * - Removed data owned by tsrcom.asm (indos/criterr)
 * - Removed data owned by other C modules (pcmcia_event_flag, api_ready)
 * - Removed 3c509b_* stubs (real implementations in 3c509b.c)
 *
 * Last Updated: 2026-01-28 13:28:44 CET
 *
 * Round 5: Added packet_received wrapper to forward to packet_receive_process
 */

#include <stddef.h>

/* Basic types for 16-bit DOS */
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned long uint32_t;
typedef int bool;
typedef int media_type_t;  /* Stub type for media_type enumeration */

/* Forward declaration for init context (opaque pointer in stubs) */
struct init_context;

/* ============================================================================
 * Global Variables - Must be defined to resolve external references
 * NOTE: Remove leading underscores - Watcom C adds underscore prefix automatically.
 * So 'api_ready' in C becomes '_api_ready' in the object file.
 *
 * OWNERSHIP NOTES:
 *   - api_ready: Owned by api.c
 *   - indos_segment/offset, criterr_segment/offset: Owned by tsrcom.asm
 *   - pcmcia_event_flag: Owned by pcmmgr.c
 *   - g_config: Owned by config.c
 * ============================================================================ */

/* CPU/Platform state */
int cpu_type = 0;
int g_platform = 0;
int global_force_pio_mode = 0;
int g_clflush_available = 0;
int g_cache_line_size = 32;
uint8_t g_local_mac[6] = {0};

/* DMA state */
int g_dma_policy = 0;

/* NIC state - referenced by ASM (without underscore becomes _nic_io_base) */
uint16_t nic_io_base = 0;
uint8_t nic_irq = 0;
int isr_active = 0;

/* Buffer pools - referenced by ASM */
void *bounce_pool = NULL;
void *vds_pool = NULL;

/* ISR state */
uint8_t saved_int_mask = 0;
int mask_method = 0;

/* Promiscuous mode - must be uint32_t to match promisc.asm usage */
uint32_t g_promisc_buffer_tail = 0;

/* CPU optimization - REMOVED: now defined in ASM modules
 * current_cpu_opt: pktops.asm (sets during packet_ops_init)
 * current_iobase, current_irq, current_instance: hwcoord.asm
 */

/* Module headers for patching */
void *packet_api_module_header = NULL;
void *nic_irq_module_header = NULL;
void *hardware_module_header = NULL;
void *extension_snapshots = NULL;

/* Patch sites */
void *PATCH_3c515_transfer = NULL;
void *PATCH_cache_flush_pre = NULL;
void *PATCH_dma_boundary_check = NULL;

/* Hardware flags table - REMOVED: now defined in hwcoord.asm */

/* Deferred work queue */
/* deferred_work_queue_count is a FUNCTION called by tsrwrap.asm */
int deferred_work_queue_count(void) { return 0; }

/* ============================================================================
 * Stage Functions - Init Pipeline
 * Declared as 'far' to match init_context.h signatures
 * These are C callable stubs, ASM stubs are in linkasm.asm
 * ============================================================================ */

int far stage_entry_validation(struct init_context far *ctx) {
    (void)ctx;
    return 0;
}

int far stage_cpu_detect(struct init_context far *ctx) {
    (void)ctx;
    return 0;
}

int far stage_platform_probe(struct init_context far *ctx) {
    (void)ctx;
    return 0;
}

int far stage_logging_init(struct init_context far *ctx) {
    (void)ctx;
    return 0;
}

int far stage_config_parse(struct init_context far *ctx, int argc, char far * far *argv) {
    (void)ctx;
    (void)argc;
    (void)argv;
    return 0;
}

int far stage_chipset_detect(struct init_context far *ctx) {
    (void)ctx;
    return 0;
}

int far stage_vds_dma_refine(struct init_context far *ctx) {
    (void)ctx;
    return 0;
}

int far stage_memory_init(struct init_context far *ctx) {
    (void)ctx;
    return 0;
}

int far stage_packet_ops_init(struct init_context far *ctx) {
    (void)ctx;
    return 0;
}

int far stage_hardware_detect(struct init_context far *ctx) {
    (void)ctx;
    return 0;
}

int far stage_dma_buffer_init(struct init_context far *ctx) {
    (void)ctx;
    return 0;
}

int far stage_tsr_relocate(struct init_context far *ctx) {
    (void)ctx;
    return 0;
}

int far stage_api_install(struct init_context far *ctx) {
    (void)ctx;
    return 0;
}

int far stage_irq_enable(struct init_context far *ctx) {
    (void)ctx;
    return 0;
}

int far stage_api_activate(struct init_context far *ctx) {
    (void)ctx;
    return 0;
}

/* ============================================================================
 * 3C515 NIC Driver Function Stubs
 * 3C509B functions removed - real implementations in 3c509b.c
 * REMOVED (2026-01-28): _3c515_init, _3c515_cleanup, _3c515_reset,
 *   _3c515_self_test - now in 3c515_init.c
 * REMOVED (2026-01-28): _3c515_enable_interrupts, _3c515_disable_interrupts
 *   - now in 3c515_rt.c
 * ============================================================================ */

/* ============================================================================
 * ASM CPU Detection Functions - Stubs matching cpudet.h
 * These are fallbacks if cpudet.asm is not linked
 * ============================================================================ */

int asm_detect_cpu_type(void) { return 3; }
uint32_t asm_get_cpu_flags(void) { return 0; }
uint8_t asm_get_cpu_family(void) { return 3; }
uint8_t asm_get_cpu_model(void) { return 0; }
uint8_t asm_get_cpu_stepping(void) { return 0; }
uint8_t asm_get_cpu_vendor(void) { return 0; }
char far *asm_get_cpu_vendor_string(void) {
    static char vendor[] = "GenuineIntel";
    return (char far *)vendor;
}
int asm_get_cpu_speed(void) { return 100; }
int asm_get_speed_confidence(void) { return 50; }
int asm_has_cyrix_extensions(void) { return 0; }
int asm_has_invariant_tsc(void) { return 0; }
uint8_t asm_is_hypervisor(void) { return 0; }
int asm_is_v86_mode(void) { return 0; }

/* ============================================================================
 * Remaining stub functions - all with simple/generic signatures
 * The linker just needs symbol resolution, actual signatures don't matter
 * for stubs that are never called during initial testing
 *
 * REMOVED (now trampolines in linkasm.asm):
 *   - main_init, cpu_detect_init, nic_detect_init, patch_init_and_apply
 *   - dma_policy_save, dma_policy_set_runtime, dma_policy_set_validated
 *   - interrupt_mitigation_apply_all, hardware_set_media_mode
 *   - pcmcia_get_snapshot, packet_isr_receive, packet_receive_process
 *   - free_tx_buffer, hardware_read_packet, hardware_configure_3c509b
 *   - init_3c515, packet_api_entry, periodic_vector_monitoring
 *   - deferred_work_queue_add, deferred_work_queue_process
 * ============================================================================ */

/* DMA functions */
int dma_init(void) { return 0; }
int dma_send_scatter_gather(void) { return -1; }
uint32_t dma_get_physical_addr(void) { return 0; }
uint32_t dma_virt_to_phys(void) { return 0; }
int dma_validate_buffer_constraints(void) { return 0; }
void dma_stall_engines_asm(void) {}
void dma_unstall_engines_asm(void) {}
int dma_start_transfer_asm(void) { return 0; }
int dma_get_engine_status_asm(void) { return 0; }
int dma_analyze_packet_fragmentation(void) { return 0; }

/* Renamed trampolined functions - impl versions called by linkasm.asm */
void *dma_get_rx_bounce_buffer_impl(void) { return NULL; }
void pci_shim_handler_c_impl(void) {}
void promisc_add_buffer_packet_asm_impl(void) {}

/* Cache functions - SOME NOW PROVIDED by cacheops.asm via linkasm.asm trampolines:
 * cache_clflush_line, cache_clflush_safe, cache_wbinvd, cache_wbinvd_safe, memory_fence
 * These remaining stubs are for higher-level cache management not in cacheops.asm */
int cache_coherency_init(void) { return 0; }
void cache_coherency_shutdown(void) {}
void cache_flush_486(void) {}
void cache_flush_if_needed(void) {}
/* cache_clflush_line - NOW in cacheops.asm */
/* cache_clflush_safe - NOW in cacheops.asm */
/* cache_wbinvd - NOW in cacheops.asm */
/* cache_wbinvd_safe - NOW in cacheops.asm */
void cache_sync_for_cpu(void) {}
void cache_sync_for_device(void) {}
void cache_management_flush_buffer(void) {}
void cache_management_invalidate_buffer(void) {}
int is_cache_management_initialized(void) { return 0; }
const char *get_cache_tier_description(void) { return "Unknown"; }

/* Hardware functions - NOT trampolined ones
 * REMOVED (2026-01-28): hardware_get_link_status - now in hardware_rt.c */
int hardware_check_rx_ready(void) { return 0; }
int hardware_dma_read(void) { return -1; }
int hardware_dma_write(void) { return -1; }
int hardware_pio_read(void) { return -1; }
int hardware_pio_write(void) { return -1; }
int hardware_set_loopback_mode(void) { return 0; }

/* Hardware functions - needed for split driver builds (Phase 6) */
uint32_t hardware_get_last_error_time(uint8_t nic_index) { (void)nic_index; return 0; }
const char *hardware_nic_type_to_string(int nic_type) { (void)nic_type; return "Unknown"; }
void hardware_set_pnp_detection_results(void *results, int count) { (void)results; (void)count; }
int hardware_check_tx_complete(void *nic) { (void)nic; return 1; }
void *error_context_create(int nic_index) { (void)nic_index; return NULL; }
const char *hardware_nic_status_to_string(uint16_t status) { (void)status; return "Unknown"; }
int hardware_attach_pcmcia_nic(void *nic) { (void)nic; return 0; }
int hardware_detach_nic_by_index(int index) { (void)index; return 0; }

/* Buffer functions */
int buffer_alloc_init(void) { return 0; }
void buffer_alloc_cleanup(void) {}
void *buffer_alloc_rx(void) { return NULL; }
void *buffer_alloc_small(void) { return NULL; }
void *buffer_alloc_dma_safe(void) { return NULL; }
int nic_buffer_pool_manager_init(void) { return 0; }
void nic_buffer_pool_manager_cleanup(void) {}
void *nic_buffer_pool_create(void) { return NULL; }
void nic_buffer_pool_destroy(void) {}
void *nic_buffer_alloc(void) { return NULL; }
void nic_buffer_free(void) {}
void *nic_buffer_alloc_ethernet_frame(void) { return NULL; }
int nic_buffer_is_initialized(void) { return 0; }
void nic_buffer_get_stats(void) {}
void nic_buffer_print_all_stats(void) {}
int nic_rx_copybreak_init(void) { return 0; }
void *nic_rx_copybreak_alloc(void) { return NULL; }
void nic_rx_copybreak_free(void) {}
void *packet_buffer_alloc(void) { return NULL; }
void packet_buffer_free(void) {}

/* Platform functions */
int platform_init(void) { return 0; }
int platform_detect(void) { return 0; }
int platform_get_dma_policy(void) { return 0; }
const char *platform_get_policy_desc(void) { return "Unknown"; }
void platform_set_global_policy(void) {}
int platform_allow_busmaster_dma(void) { return 1; }
int detect_emm386_manager(void) { return 0; }
int detect_qemm_manager(void) { return 0; }
int detect_vcpi_services(void) { return 0; }
int detect_windows_enhanced_mode(void) { return 0; }
int is_eisa_system(void) { return 0; }
int is_mca_system(void) { return 0; }
int is_isa_bus(void) { return 1; }
int get_ps2_model(void) { return 0; }

/* NIC Detection functions - nic_detect_init is now a trampoline */
int nic_detect_eisa_3c592(void) { return 0; }
int nic_detect_eisa_3c597(void) { return 0; }
int nic_detect_mca_3c523(void) { return 0; }
int nic_detect_mca_3c529(void) { return 0; }
int nic_detect_vlb(void) { return 0; }
int detect_nic_type(void) { return 0; }
const char *nic_type_to_string(void) { return "Unknown"; }
int nic_has_capability(void) { return 0; }
void nic_irq_set_binding(void) {}

/* Media functions */
int media_control_init(void) { return 0; }
void media_control_cleanup(void) {}
int auto_detect_media(void) { return 0; }
int check_media_link_status(void) { return 0; }
int get_default_media_for_nic(void) { return 0; }
int is_media_supported_by_nic(void) { return 0; }
int select_media_transceiver(void) { return 0; }
int test_link_beat(void) { return 0; }
const char *media_type_to_string(media_type_t media) { (void)media; return "Unknown"; }
int get_link_speed(void) { return 10; }

/* Packet functions - NOW PROVIDED by pktops.c (pktops_c.obj):
 *   packet_ops_init, packet_bottom_half_init, packet_process_deferred_work,
 *   packet_process_received, packet_isr_receive, packet_receive_process
 * REMOVED (2026-01-28): packet_get_ethertype, packet_queue_tx_completion
 *   - now in pktops_rt.c
 * These remaining stubs are for functions not yet in pktops.c: */
void packet_deliver_to_handler(void) {}
uint32_t packet_get_timestamp(void) { return 0; }

/* packet_received - Legacy wrapper for callers that don't have nic_index
 * NOTE: rxbatch.c now calls packet_receive_process directly with nic_index.
 * This wrapper is kept for any other callers that use the old interface.
 * Hard-codes nic_index=0 (primary NIC). */
extern int packet_receive_process(uint8_t *raw_data, uint16_t length, uint8_t nic_index);

int packet_received(void far *buf, uint16_t len) {
    /* Forward to packet_receive_process with nic_index 0 (primary NIC) */
    return packet_receive_process((uint8_t far *)buf, len, 0);
}

/* Error handling functions */
int error_handling_init(void) { return 0; }
void error_handling_cleanup(void) {}
void error_handling_reset_stats(void) {}
const char *error_severity_to_string(void) { return "Unknown"; }
int configure_error_thresholds(void) { return 0; }
int get_system_health_status(void) { return 100; }
int read_error_log_entries(void) { return 0; }
void print_error_statistics(void) {}
void print_global_error_summary(void) {}
void print_recovery_statistics(void) {}
int handle_adapter_error(void) { return -1; }
int attempt_adapter_recovery(void) { return -1; }
const char *adapter_failure_to_string(void) { return "Unknown"; }
int protected_hardware_operation(void) { return 0; }

/* Utility functions */
void delay_ms(uint16_t ms) { (void)ms; }
uint32_t get_system_timestamp_ms(void) { return 0; }
uint32_t get_available_memory(void) { return 640 * 1024; }
uint32_t get_free_conventional_memory(void) { return 400 * 1024; }
uint32_t get_free_umb_memory(void) { return 0; }
uint32_t get_free_xms_memory(void) { return 0; }
/* memory_fence - NOW in cacheops.asm */
void memory_set(void *ptr, uint8_t value, uint32_t size) { (void)ptr; (void)value; (void)size; }
void cpu_opt_memzero(void) {}

/* I/O Port functions */
uint8_t inportb(uint16_t port) { (void)port; return 0; }
uint16_t inportw(uint16_t port) { (void)port; return 0; }
uint32_t inportd(uint16_t port) { (void)port; return 0; }
void outportb(uint16_t port, uint8_t val) { (void)port; (void)val; }
void outportw(uint16_t port, uint16_t val) { (void)port; (void)val; }
void outportd(uint16_t port, uint32_t val) { (void)port; (void)val; }
uint32_t inl(uint16_t port) { (void)port; return 0; }
void outl(uint16_t port, uint32_t value) { (void)port; (void)value; }
uint32_t inpd(uint16_t port) { (void)port; return 0; }
void outpd(uint16_t port, uint32_t val) { (void)port; (void)val; }

/* Miscellaneous functions - removed trampolined ones */
int init_driver(void) { return 0; }
int init_complete_safety_detection(void) { return 0; }
void driver_entry(void) {}
void tsr_uninstall(void) {}
int pnp_init_system(void) { return 0; }
int pnp_detect_nics(void) { return 0; }
int pnp_filter_by_type(void) { return 0; }
void patch_transfer_method(void) {}
uint32_t calculate_hw_signature(void) { return 0; }
void copybreak_set_threshold(void) {}
int patch_batch_init(void) { return 0; }
int routing_engine_init(void) { return 0; }
void ethernet_build_header(void) {}
int arp_get_table_size(void) { return 0; }
int telemetry_init(void) { return 0; }
void telemetry_record_dma_test_results(void) {}
int test_bus_master_dma_quick(void) { return 0; }
int vds_in_v86_mode(void) { return 0; }
int vds_lock_region_sg(void) { return 0; }
int vds_unlock_region_sg(void) { return 0; }
int validate_all_device_caps(void) { return 0; }
int perform_complete_coherency_analysis(void) { return 0; }
int needs_bounce_for_alignment(void) { return 0; }
void balance_buffer_resources(void) {}
void monitor_nic_buffer_usage(void) {}
void update_nic_stats(void) {}
int irq_handler_init(void) { return 0; }
void irq_handler_uninstall(void) {}
int tx_lazy_global_init(void) { return 0; }
void handle_rx_complete(void) {}
void handle_rx_error(void) {}
void handle_tx_complete(void) {}
void handle_tx_error(void) {}
int hw_checksum_process_outbound_packet(void) { return 0; }
int hw_checksum_verify_inbound_packet(void) { return 0; }
int direct_pio_init_cpu_detection(void) { return 0; }
int direct_pio_get_optimization_level(void) { return 0; }
void direct_pio_get_cpu_support_info(void) {}
int direct_pio_header_and_payload(void) { return -1; }
int send_packet_direct_pio_asm(void) { return -1; }
int send_packet_direct_pio_enhanced(void) { return -1; }
int should_offer_performance_guidance(void) { return 0; }
void display_performance_opportunity_analysis(void) {}
void transfer_dma(void) {}
void transfer_pio(void) {}
void log_cpu_database_info(void) {}
int check_cpuid_available(void) { return 0; }
int amd_k5_has_pge_bug(void) { return 0; }
int cyrix_needs_cpuid_enable(void) { return 0; }
void serialize_after_smc(void) {}
void pci_shim_set_enabled(void) {}
int pci_shim_can_uninstall(void) { return 0; }
void pci_shim_do_uninstall(void) {}
int pcmcia_isr_install(void) { return 0; }
void pcmcia_isr_uninstall(void) {}
int pcmcia_cis_parse_3com(void) { return 0; }
uint16_t _dos_getversion(void) { return 0x0600; }
int asm_get_cache_info(void) { return 0; }

/* main.asm C function stubs (Phase 5 Round 8) */
int nic_irq_init(void) { return 0; }
void *hardware_get_detected_nics(void) { return NULL; }
int install_hardware_irq(int irq) { (void)irq; return 0; }
void restore_all_hardware_irqs(void) {}
int packet_api_dispatcher(void) { return 0; }
void log_vector_ownership_warning(void) {}
void log_hardware_irq_restore_warning(void) {}
/* REMOVED (2026-01-28): dos_idle_background_processing - now in dos_idle.c */
int defensive_init(void) { return 0; }
void defensive_shutdown(void) {}
int safe_restore_vector(int vec) { (void)vec; return 0; }
int check_vector_ownership(int vec) { (void)vec; return 1; }
void initialize_memory_optimization(void) {}

/* ASM data symbol stubs - deferred_work_queue_count, g_promisc_buffer_tail,
 * nic_irq, isr_active are already defined above */

/* delay functions - delay_1us is provided by hwdet.asm, only delay_1ms here */
void delay_1ms(void) {}

/* CPU detection ASM symbols - REMOVED (2026-01-28):
 * cpuid_available, cpu_features, cache_line_size, cpu_is_486_plus,
 * is_v86_mode, sse2_available - all now defined in cpudet.asm
 * NOTE: g_cache_line_size (with g_ prefix) is kept as it's a separate variable */

/* 3C509B PIO transmit */
int el3_3c509b_pio_transmit(void *nic, const void *data, int len) {
    (void)nic; (void)data; (void)len; return -1;
}

/* VDS availability */
int vds_is_available(void) { return 0; }

/* Buffer management */
int calculate_buffer_usage_percentage(void) { return 0; }

/* Memory allocation */
void *memory_allocate(int size) { (void)size; return NULL; }

/* Routing */
int routing_process_packet(void *pkt, int len) { (void)pkt; (void)len; return 0; }

/* Packet queue */
int packet_queue_init(void) { return 0; }
void packet_queue_cleanup(void) {}
int packet_queue_is_full(void) { return 0; }
int packet_queue_enqueue(void *pkt) { (void)pkt; return 0; }
int packet_queue_is_empty(void) { return 1; }
void *packet_queue_dequeue(void) { return NULL; }
void *packet_queue_peek(void) { return NULL; }
void packet_set_data(void *pkt, const void *data, int len) { (void)pkt; (void)data; (void)len; }
int packet_send_immediate(void *pkt) { (void)pkt; return -1; }

/* ASM packet copy */
void asm_packet_copy_fast(void *dest, const void *src, uint16_t size) { (void)dest; (void)src; (void)size; }

/* EEPROM */
int read_mac_from_eeprom_3c509b(void *nic, uint8_t *mac) { (void)nic; (void)mac; return -1; }

/* XMS pool and deferred queue - REMOVED (2026-01-28):
 * g_xms_pool and its typedef - now defined in pktops_init.c
 * g_deferred_queue and its typedef - now defined in pktops_init.c */

/* Deferred work queue functions (called from tsrwrap.asm) */
void periodic_vector_monitoring(void) {}
int deferred_work_queue_add(void *work) { (void)work; return 0; }
void deferred_work_queue_process(void) {}

/* NOTE: pcmcia_event_flag is owned by pcmmgr.c, do NOT define here */
