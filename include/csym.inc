; csym.inc - C Symbol Naming Bridge for NASM
;
; 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
;
; This file provides %xdefine macros to map C function/variable names
; to their Watcom C mangled equivalents.
;
; USAGE: Add "%include "csym.inc"" at the top of ASM files that
;        reference C symbols via extern declarations.
;
; The macros rewrite both the extern declaration and all uses:
;   extern log_warning  -->  extern log_warning_      (functions)
;   mov ax, [cpu_type]  -->  mov ax, [_cpu_type]      (data)
;
; IMPORTANT: Watcom C 16-bit uses DIFFERENT conventions for data vs functions:
;   - FUNCTIONS: TRAILING underscore (func() -> func_)
;   - DATA: PREFIX underscore (int var -> _var)
;
; Last Updated: 2026-01-28 03:45:00 UTC
; Phase 5: Fixed - data uses PREFIX underscore, functions use TRAILING underscore

; =============================================================================
; C DATA SYMBOLS (variables defined in C files)
; Watcom C 16-bit: int foo;  -->  _foo (PREFIX underscore for data)
; NOTE: This is DIFFERENT from function naming (trailing underscore)
; =============================================================================

; From linkstubs.c
%xdefine deferred_work_queue_count _deferred_work_queue_count
%xdefine g_promisc_buffer_tail _g_promisc_buffer_tail
%xdefine isr_active _isr_active
%xdefine nic_io_base _nic_io_base
%xdefine nic_irq _nic_irq

; From pcmmgr.c
%xdefine pcmcia_event_flag _pcmcia_event_flag

; From api.c
%xdefine api_ready _api_ready

; From config.c
%xdefine g_config _g_config

; =============================================================================
; C FUNCTION SYMBOLS (functions defined in C files)
; Watcom C: int foo();  -->  foo_
; =============================================================================

; --- Logging (logging.c) ---
%xdefine log_error log_error_
%xdefine log_warning log_warning_
%xdefine log_info log_info_
%xdefine log_debug log_debug_
%xdefine log_trace log_trace_
%xdefine log_hardware_error log_hardware_error_
%xdefine log_hardware_irq_restore_warning log_hardware_irq_restore_warning_
%xdefine log_vector_ownership_warning log_vector_ownership_warning_

; --- Deferred work queue (dos_idle.c) ---
%xdefine deferred_work_queue_add deferred_work_queue_add_
%xdefine deferred_work_queue_process deferred_work_queue_process_
%xdefine dos_idle_background_processing dos_idle_background_processing_

; --- Packet operations (pktops.c) ---
%xdefine packet_receive_process packet_receive_process_
%xdefine packet_isr_receive packet_isr_receive_

; --- EEPROM (eeprom.c) ---
%xdefine read_3c509b_eeprom read_3c509b_eeprom_
%xdefine read_3c515_eeprom read_3c515_eeprom_
%xdefine read_mac_from_eeprom_3c509b read_mac_from_eeprom_3c509b_

; --- Hardware detection ---
; NOTE: hardware_detect_all, detect_3c509b_device, detect_3c515_device are
; ASM-to-ASM symbols (provided by hwdet.asm). Do NOT add them here.

; --- Platform detection ---
; NOTE: is_eisa_system, is_mca_system, get_ps2_model are ASM-to-ASM symbols
; (provided by hwbus.asm). Do NOT add them here.

; --- DMA (dmabnd.c, dmamap.c) ---
%xdefine dma_get_rx_bounce_buffer dma_get_rx_bounce_buffer_
%xdefine isa_virt_to_phys isa_virt_to_phys_
%xdefine check_isa_dma_boundary check_isa_dma_boundary_

; --- Utility functions ---
; NOTE: delay_1us is provided by hwdet.asm (ASM-to-ASM), do NOT add it here
; delay_1ms is a C stub, so it CAN be here
%xdefine delay_1ms delay_1ms_

; --- Init functions (C functions, trailing underscore) ---
%xdefine main_init main_init_
%xdefine cpu_detect_init cpu_detect_init_
%xdefine nic_detect_init nic_detect_init_
%xdefine patch_init_and_apply patch_init_and_apply_

; --- Runtime C functions ---
%xdefine dma_policy_set_runtime dma_policy_set_runtime_
%xdefine dma_policy_set_validated dma_policy_set_validated_
%xdefine dma_policy_save dma_policy_save_
%xdefine interrupt_mitigation_apply_all interrupt_mitigation_apply_all_
%xdefine hardware_set_media_mode hardware_set_media_mode_
%xdefine pcmcia_get_snapshot pcmcia_get_snapshot_
%xdefine periodic_vector_monitoring periodic_vector_monitoring_

; --- Stage functions (init_main.c calls these) ---
%xdefine stage_entry_validation stage_entry_validation_
%xdefine stage_cpu_detect stage_cpu_detect_
%xdefine stage_platform_probe stage_platform_probe_
%xdefine stage_logging_init stage_logging_init_
%xdefine stage_config_parse stage_config_parse_
%xdefine stage_chipset_detect stage_chipset_detect_
%xdefine stage_vds_dma_refine stage_vds_dma_refine_
%xdefine stage_memory_init stage_memory_init_
%xdefine stage_packet_ops_init stage_packet_ops_init_
%xdefine stage_hardware_detect stage_hardware_detect_
%xdefine stage_dma_buffer_init stage_dma_buffer_init_
%xdefine stage_tsr_relocate stage_tsr_relocate_
%xdefine stage_api_install stage_api_install_
%xdefine stage_irq_enable stage_irq_enable_
%xdefine stage_api_activate stage_api_activate_

; --- XMS (xms_core.c) - DATA symbols use prefix underscore ---
%xdefine g_xms_pool _g_xms_pool
%xdefine g_deferred_queue _g_deferred_queue

; --- Memory management ---
%xdefine memory_allocate memory_allocate_

; --- Routing ---
%xdefine routing_process_packet routing_process_packet_

; --- Packet queue ---
%xdefine packet_queue_init packet_queue_init_
%xdefine packet_queue_cleanup packet_queue_cleanup_
%xdefine packet_queue_is_full packet_queue_is_full_
%xdefine packet_queue_enqueue packet_queue_enqueue_
%xdefine packet_queue_is_empty packet_queue_is_empty_
%xdefine packet_queue_dequeue packet_queue_dequeue_
%xdefine packet_queue_peek packet_queue_peek_
%xdefine packet_set_data packet_set_data_
%xdefine packet_send_immediate packet_send_immediate_

; --- VDS ---
%xdefine vds_is_available vds_is_available_

; --- Buffer management ---
%xdefine calculate_buffer_usage_percentage calculate_buffer_usage_percentage_

; --- Promiscuous mode (promisc.c) ---
%xdefine promisc_capture_packet promisc_capture_packet_

; --- SMC patching (smcpat.c) - DATA symbols use prefix underscore ---
%xdefine g_patch_manager _g_patch_manager

; --- Safety stubs (linkstubs.c, nicsafe.c) - DATA symbols use prefix underscore ---
%xdefine vds_pool _vds_pool
%xdefine bounce_pool _bounce_pool
%xdefine cpu_type _cpu_type
%xdefine saved_int_mask _saved_int_mask
%xdefine mask_method _mask_method

; --- main.asm C function references ---
%xdefine nic_irq_init nic_irq_init_
%xdefine hardware_get_detected_nics hardware_get_detected_nics_
%xdefine install_hardware_irq install_hardware_irq_
%xdefine restore_all_hardware_irqs restore_all_hardware_irqs_
%xdefine packet_api_dispatcher packet_api_dispatcher_
%xdefine log_vector_ownership_warning log_vector_ownership_warning_
%xdefine log_hardware_irq_restore_warning log_hardware_irq_restore_warning_
%xdefine dos_idle_background_processing dos_idle_background_processing_
%xdefine defensive_init defensive_init_
%xdefine defensive_shutdown defensive_shutdown_
%xdefine safe_restore_vector safe_restore_vector_
%xdefine check_vector_ownership check_vector_ownership_
%xdefine initialize_memory_optimization initialize_memory_optimization_

; --- tsrwrap.asm C function references ---
%xdefine deferred_work_queue_count deferred_work_queue_count_

; --- ASM packet copy (pktops.asm exports, C calls) ---
%xdefine asm_packet_copy_fast asm_packet_copy_fast_

; --- CPU detection (cpudet.asm provides, C may reference) - DATA symbols use prefix underscore ---
%xdefine cpuid_available _cpuid_available
%xdefine cpu_features _cpu_features
%xdefine cache_line_size _cache_line_size
%xdefine cpu_is_486_plus _cpu_is_486_plus
%xdefine is_v86_mode _is_v86_mode
%xdefine sse2_available _sse2_available

; --- 3C509B driver ---
%xdefine el3_3c509b_pio_transmit el3_3c509b_pio_transmit_

; =============================================================================
; NOTE: ASM-to-ASM symbols do NOT need trailing underscore mapping
; =============================================================================
; The following are defined in ASM files and referenced by other ASM files.
; They should NOT be included here:
;
; From tsrcom.asm: indos_segment, indos_offset, criterr_segment, criterr_offset
; From pktapi.asm: packet_driver_isr
; From hwcoord.asm: hw_instances, hw_io_bases, hw_irq_lines, hw_types, etc.
;                   current_instance, current_iobase, current_irq (both names exported)
; From hwpkt.asm: hardware_read_packet, io_read_byte, io_write_byte, etc.
; From hwcfg.asm: configure_3c509b_device, reset_3c509b_device, etc.
; From hwinit.asm: init_3c515
; From cacheops.asm: cache_wbinvd, memory_fence, etc.
; From pktops.asm: packet_copy_fast, current_cpu_opt (both names exported)
; =============================================================================

; End of csym.inc
