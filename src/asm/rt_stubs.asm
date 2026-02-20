; rt_stubs.asm - Runtime stub functions (ASM replacement for rt_stubs.c)
;
; 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
;
; Provides 183 linker stub functions and 76 global data variables.
; Stubs are never called at runtime - JIT ASM modules override them.
; Replaces rt_stubs.c for ~4800 bytes ROOT code savings.
;
; Last Updated: 2026-02-02 11:15:00 CET
;
; Watcom naming conventions:
;   Functions: TRAILING underscore (func -> func_)
;   Data: PREFIX underscore (var -> _var)

[BITS 16]
[CPU 8086]

; =============================================================================
; STUB MACROS - reduce repetition for common patterns
; =============================================================================

; Return 0 (int16 in AX)
%macro STUB_RET0 1
    global %1
    %1: xor ax, ax
        retf
%endmacro

; Return -1 (int16 in AX)
%macro STUB_RETM1 1
    global %1
    %1: mov ax, -1
        retf
%endmacro

; Return 1 (int16 in AX)
%macro STUB_RET1 1
    global %1
    %1: mov ax, 1
        retf
%endmacro

; Return 10 (int16 in AX)
%macro STUB_RET10 1
    global %1
    %1: mov ax, 10
        retf
%endmacro

; Void no-op (far return)
%macro STUB_VOID 1
    global %1
    %1: retf
%endmacro

; Return NULL far pointer (DX:AX = 0:0) or uint32_t 0
%macro STUB_NULL 1
    global %1
    %1: xor ax, ax
        xor dx, dx
        retf
%endmacro

; =============================================================================
; DATA SEGMENT - Initialized non-zero values (DGROUP)
; =============================================================================
segment _DATA class=DATA

; String constant for hw_checksum_mode_to_string
global _str_unknown
_str_unknown:   db "Unknown", 0

; g_copy_break_threshold moved to rtcfg.asm

; logging: log file handle (int, init -1 = no file)
global _log_file
_log_file:  dw -1

; =============================================================================
; BSS SEGMENT - DGROUP items (near-referenced by resident ASM modules)
; =============================================================================
segment _BSS class=BSS

; --- hardware_rt (near-referenced by mod_api_rt.asm, mod_routing_rt.asm) ---
global _g_nic_infos
_g_nic_infos:           resb 2616   ; nic_info_t[8], 327 bytes each

global _g_num_nics
_g_num_nics:            resw 1      ; int

global _g_hardware_initialized
_g_hardware_initialized: resb 1     ; bool

; --- xms_core_rt (near-referenced by mod_xms_rt.asm) ---
global _g_xms_entry
_g_xms_entry:           resd 1      ; void (far *)(void)

global _g_xms_unavail_reason
_g_xms_unavail_reason:  resb 64     ; char[64]

; --- DGROUP association (only _DATA and near _BSS) ---
group DGROUP _DATA _BSS

; =============================================================================
; FAR BSS SEGMENT - Init-only data (written by *_init.c, not used at runtime)
; Cold C modules use -zt=0 so they access these via far pointers.
; ASM JIT modules have their own copies in hot sections.
; =============================================================================
segment rt_FAR_BSS class=FAR_DATA

; --- api_rt (only referenced by api_init.c, unwind.c) ---
global _handles
_handles:               resb 512    ; pd_handle_t[16]

global _extended_handles
_extended_handles:      resb 848    ; extended_packet_handle_t[16]

global _next_handle
_next_handle:           resw 1      ; int

global _api_initialized
_api_initialized:       resw 1      ; int

global _extended_api_initialized
_extended_api_initialized: resw 1   ; int

global _api_ready
_api_ready:             resw 1      ; int (volatile)

global _load_balancing_enabled
_load_balancing_enabled: resw 1     ; int

global _qos_enabled
_qos_enabled:           resw 1      ; int

global _virtual_interrupts_enabled
_virtual_interrupts_enabled: resw 1 ; int

global _global_bandwidth_limit
_global_bandwidth_limit: resd 1     ; uint32_t

global _global_lb_config
_global_lb_config:      resb 13     ; pd_load_balance_params_t

global _default_qos_params
_default_qos_params:    resb 15     ; pd_qos_params_t

global _nic_weights
_nic_weights:           resb 32     ; uint32_t[8]

global _nic_utilization
_nic_utilization:       resb 32     ; uint32_t[8]

global _nic_error_counts
_nic_error_counts:      resb 32     ; uint32_t[8]

global _last_nic_used
_last_nic_used:         resd 1      ; uint32_t

; --- dmabnd_rt (only referenced by dmabnd_init.c, dmasafe.c, dmatest.c) ---
global _g_tx_bounce_pool
_g_tx_bounce_pool:      resb 24     ; bounce_pool_t

global _g_rx_bounce_pool
_g_rx_bounce_pool:      resb 24     ; bounce_pool_t

global _g_bounce_pools_initialized
_g_bounce_pools_initialized: resb 1 ; bool

global _g_boundary_stats
_g_boundary_stats:      resb 44     ; dma_boundary_stats_t

global _g_v86_mode_detected
_g_v86_mode_detected:   resb 1      ; bool

global _g_dpmi_available
_g_dpmi_available:      resb 1      ; bool

global _g_memory_manager_detected
_g_memory_manager_detected: resb 1  ; bool

; --- dmamap_rt (only referenced by dmamap_init.c) ---
global _g_dmamap_stats
_g_dmamap_stats:        resb 32     ; dma_mapping_stats_t

global _g_fast_path_enabled
_g_fast_path_enabled:   resb 1      ; bool

global _g_cache_hits
_g_cache_hits:          resd 1      ; uint32_t

global _g_cache_attempts
_g_cache_attempts:      resd 1      ; uint32_t

; --- pci_shim_rt (only referenced by pci_shim_init.c) ---
global _shim_state
_shim_state:            resb 16     ; struct pci_shim_state

; --- pcimux_rt (only referenced by pcimux_init.c) ---
global _mplex_state
_mplex_state:           resb 10     ; struct mplex_state_t

; --- hwchksm_rt (only referenced by hwchksm_init.c) ---
global _checksum_system_initialized
_checksum_system_initialized: resb 1 ; bool

global _global_checksum_mode
_global_checksum_mode:  resb 1      ; checksum_mode_t

global _global_checksum_stats
_global_checksum_stats: resb 56     ; checksum_stats_t

global _checksum_optimization_flags
_checksum_optimization_flags: resw 1 ; uint16_t

; --- irqmit_rt (only referenced by irqmit_init.c) ---
global _g_mitigation_contexts
_g_mitigation_contexts: resb 840    ; interrupt_mitigation_context_t[8]

global _g_mitigation_initialized
_g_mitigation_initialized: resb 1   ; bool

; g_mitigation_batch, g_mitigation_timeout moved to rtcfg.asm

; --- rxbatch_rt (only referenced by rxbatch_init.c) ---
global _g_rx_state
_g_rx_state:            resb 2944   ; rx_batch_state_t[8]

global _g_rx_batch_initialized
_g_rx_batch_initialized: resb 1     ; bool

; --- txlazy_rt (only referenced by txlazy_init.c) ---
global _g_lazy_tx_state
_g_lazy_tx_state:       resb 408    ; tx_lazy_state_t[8], 51 bytes each

global _g_tx_lazy_initialized
_g_tx_lazy_initialized: resb 1      ; uint8_t

; --- xms_core_rt (only referenced by xms_core_init.c, routing.c) ---
; Note: _g_xms_entry and _g_xms_unavail_reason stay in DGROUP _BSS (near ASM refs)
global _g_xms_available
_g_xms_available:       resw 1      ; int

global _g_xms_version
_g_xms_version:         resw 1      ; uint16_t

global _g_xms_free_kb
_g_xms_free_kb:         resd 1      ; uint32_t

global _g_xms_largest_block_kb
_g_xms_largest_block_kb: resd 1     ; uint32_t

global _g_promisc_xms
_g_promisc_xms:         resb 14     ; xms_block_t

global _g_routing_xms
_g_routing_xms:         resb 14     ; xms_block_t

global _g_xms_initialized
_g_xms_initialized:     resw 1      ; int

; --- logging_rt (referenced by logging_init.c, diag.c, mod_pktbuf.asm) ---
global _logging_enabled
_logging_enabled:       resw 1      ; int

global _log_level
_log_level:             resw 1      ; int

global _log_buffer
_log_buffer:            resb 256    ; char[256]

global _ring_buffer
_ring_buffer:           resd 1      ; char * (far ptr)

global _ring_buffer_size
_ring_buffer_size:      resw 1      ; int

global _ring_write_pos
_ring_write_pos:        resw 1      ; int

global _ring_read_pos
_ring_read_pos:         resw 1      ; int

global _ring_entries
_ring_entries:          resw 1      ; int

global _ring_wrapped
_ring_wrapped:          resw 1      ; int

global _ring_enabled
_ring_enabled:          resw 1      ; int

global _category_filter
_category_filter:       resw 1      ; int

global _log_entries_written
_log_entries_written:   resd 1      ; unsigned long

global _log_entries_dropped
_log_entries_dropped:   resd 1      ; unsigned long

global _log_buffer_overruns
_log_buffer_overruns:   resd 1      ; unsigned long

global _log_to_console
_log_to_console:        resw 1      ; int

global _log_to_file
_log_to_file:           resw 1      ; int

global _log_to_network
_log_to_network:        resw 1      ; int

global _log_filename
_log_filename:          resb 1      ; char[1]

; _log_file is in _DATA (initialized to -1)

global _network_log_host
_network_log_host:      resb 1      ; char[1]

global _network_log_port
_network_log_port:      resw 1      ; int

global _network_log_protocol
_network_log_protocol:  resw 1      ; int

; =============================================================================
; CODE SEGMENT - 183 stub functions
; =============================================================================
segment rt_stubs_TEXT class=CODE

; =============================================================================
; SECTION: hardware_rt stubs
; =============================================================================

; hardware_get_nic_count: return g_num_nics
global hardware_get_nic_count_
hardware_get_nic_count_:
    mov ax, seg _g_num_nics
    mov es, ax
    mov ax, [es:_g_num_nics]
    retf

; hardware_get_nic: bounds check index, return &g_nic_infos[index*327]
global hardware_get_nic_
hardware_get_nic_:
    ; index in AX (Watcom register convention)
    test ax, ax
    js .null
    cmp ax, 8               ; MAX_NICS
    jge .null
    mov cx, 327             ; sizeof(nic_info_t)
    mul cx                  ; AX = index * 327
    add ax, _g_nic_infos    ; add base offset
    mov dx, seg _g_nic_infos
    retf
.null:
    xor ax, ax
    xor dx, dx
    retf

; hardware_get_primary_nic: if g_num_nics > 0 return &g_nic_infos[0]
global hardware_get_primary_nic_
hardware_get_primary_nic_:
    mov ax, seg _g_num_nics
    mov es, ax
    cmp word [es:_g_num_nics], 0
    jle .null
    mov ax, _g_nic_infos
    mov dx, seg _g_nic_infos
    retf
.null:
    xor ax, ax
    xor dx, dx
    retf

STUB_NULL  hardware_find_nic_by_type_
STUB_NULL  hardware_find_nic_by_mac_
STUB_RETM1 hardware_send_packet_
STUB_RETM1 hardware_receive_packet_
STUB_RET0  hardware_enable_interrupts_
STUB_RET0  hardware_disable_interrupts_
STUB_RET0  hardware_clear_interrupts_
STUB_RET0  hardware_get_link_status_
STUB_RET10 hardware_get_link_speed_
STUB_RET0  hardware_is_link_up_
STUB_VOID  hardware_get_stats_
STUB_VOID  hardware_clear_stats_
STUB_RET0  hardware_set_promiscuous_mode_
STUB_RET0  hardware_set_multicast_filter_
STUB_RET0  hardware_self_test_nic_
STUB_VOID  hardware_print_nic_info_
STUB_RET0  hardware_is_nic_present_
STUB_RET0  hardware_is_nic_active_

; =============================================================================
; SECTION: 3c509b_rt stubs - NOW IMPLEMENTED IN mod_3c509b_rt.asm
; =============================================================================
; The following stubs are now replaced by real implementations:
; _3c509b_send_packet_, _3c509b_receive_packet_, _3c509b_handle_interrupt_,
; _3c509b_check_interrupt_, _3c509b_enable_interrupts_, _3c509b_disable_interrupts_,
; _3c509b_get_link_status_, _3c509b_get_link_speed_, etc.

; Only keep stubs for functions not yet implemented in the ASM module:
STUB_RETM1 send_packet_direct_pio_
STUB_RETM1 send_packet_direct_pio_with_header_

; =============================================================================
; SECTION: 3c515_rt stubs - NOW IMPLEMENTED IN mod_3c515_rt.asm
; =============================================================================
; The following stubs are now replaced by real implementations:
; _3c515_send_packet_, _3c515_receive_packet_, _3c515_handle_interrupt_,
; _3c515_check_interrupt_, _3c515_enable_interrupts_, _3c515_disable_interrupts_,
; _3c515_get_link_status_, _3c515_get_link_speed_, _3c515_dma_prepare_buffers_,
; _3c515_dma_complete_buffers_, _3c515_process_single_event_, _3c515_handle_interrupt_batched_

; =============================================================================
; SECTION: api_rt stubs
; =============================================================================

%ifndef MOD_RT_API_IMPLEMENTED
STUB_RETM1 pd_access_type_
STUB_RET0  pd_get_driver_info_
STUB_RETM1 pd_handle_access_type_
STUB_RETM1 pd_release_handle_
STUB_RETM1 pd_send_packet_
STUB_RETM1 pd_terminate_
STUB_RETM1 pd_get_address_
STUB_RET0  pd_reset_interface_
STUB_RET0  pd_get_parameters_
STUB_RET0  pd_set_rcv_mode_
STUB_RET0  pd_get_rcv_mode_
STUB_RET0  pd_get_statistics_
STUB_RETM1 pd_set_address_
STUB_RET0  pd_validate_handle_
STUB_RET0  api_process_received_packet_
STUB_RET0  api_init_extended_handles_
STUB_RET0  api_cleanup_extended_handles_
STUB_RETM1 api_get_extended_handle_
STUB_RETM1 api_upgrade_handle_
STUB_RET0  pd_set_handle_priority_
STUB_RET0  pd_get_routing_info_
STUB_RET0  pd_set_load_balance_
STUB_RET0  pd_get_nic_status_
STUB_RET0  pd_set_qos_params_
STUB_RET0  pd_get_flow_stats_
STUB_RET0  pd_set_nic_preference_
STUB_RET0  pd_get_handle_info_
STUB_RET0  pd_set_bandwidth_limit_
STUB_RET0  pd_get_error_info_
STUB_RET0  api_select_optimal_nic_
STUB_RET1  api_check_bandwidth_limit_
STUB_RET0  api_handle_nic_failure_
STUB_RET0  api_coordinate_recovery_with_routing_
STUB_RET0  api_update_nic_utilization_
%endif

; =============================================================================
; SECTION: dmabnd_rt stubs
; =============================================================================

%ifndef MOD_RT_DMABND_IMPLEMENTED
; dma_check_buffer_safety: memset result to 0 (37 bytes), return 0
; Params: buffer=DX:AX, len=BX, result=stack (far ptr)
global dma_check_buffer_safety_
dma_check_buffer_safety_:
    push bp
    mov bp, sp
    push di
    ; result far ptr at [bp+6] (offset), [bp+8] (segment)
    mov di, [bp+6]
    mov ax, [bp+8]
    or ax, di
    jz .done
    mov es, [bp+8]
    xor ax, ax
    mov cx, 37              ; sizeof(dma_check_result_t)
    cld
    rep stosb
.done:
    xor ax, ax              ; return 0
    pop di
    pop bp
    retf

STUB_NULL  dma_get_tx_bounce_buffer_
STUB_VOID  dma_release_tx_bounce_buffer_
STUB_NULL  dma_get_rx_bounce_buffer_
STUB_VOID  dma_release_rx_bounce_buffer_

; dma_get_boundary_stats: memset stats to 0 (44 bytes)
; Params: stats=DX:AX (far ptr)
global dma_get_boundary_stats_
dma_get_boundary_stats_:
    mov bx, ax
    or ax, dx
    jz .done
    push di
    mov es, dx
    mov di, bx
    xor ax, ax
    mov cx, 44              ; sizeof(dma_boundary_stats_t)
    cld
    rep stosb
    pop di
.done:
    retf

STUB_RET1  is_safe_for_direct_dma_
%endif

; =============================================================================
; SECTION: dmamap_rt stubs
; =============================================================================

%ifndef MOD_RT_DMAMAP_IMPLEMENTED
STUB_NULL  dma_map_tx_
STUB_NULL  dma_map_tx_flags_
STUB_VOID  dma_unmap_tx_
STUB_NULL  dma_map_rx_
STUB_NULL  dma_map_rx_flags_
STUB_VOID  dma_unmap_rx_
STUB_NULL  dma_map_buffer_
STUB_NULL  dma_map_buffer_flags_
STUB_VOID  dma_unmap_buffer_
STUB_NULL  dma_mapping_get_address_
STUB_NULL  dma_mapping_get_phys_addr_       ; uint32_t return 0 = DX:AX=0
STUB_RET0  dma_mapping_get_length_          ; size_t (16-bit)
STUB_RET0  dma_mapping_uses_bounce_
STUB_RET0  dma_mapping_is_coherent_
STUB_RET0  dma_mapping_uses_vds_
STUB_RET0  dma_mapping_sync_for_device_
STUB_RET0  dma_mapping_sync_for_cpu_
STUB_RET0  dma_mapping_is_fast_path_enabled_
STUB_NULL  dma_mapping_get_cache_hit_rate_  ; uint32_t return 0
%endif

; =============================================================================
; SECTION: pci_shim_rt stubs
; =============================================================================

%ifndef MOD_RT_PCISHIM_IMPLEMENTED
; pci_shim_get_stats: write 0 to two uint32_t out-pointers
; Params: calls=DX:AX, fallbacks=CX:BX
global pci_shim_get_stats_
pci_shim_get_stats_:
    ; Save fallbacks ptr
    push cx
    push bx
    ; Write 0 to *calls if non-NULL
    mov bx, ax              ; BX = calls offset
    or ax, dx               ; test NULL (destroys AX)
    jz .no_calls
    mov es, dx
    mov word [es:bx], 0
    mov word [es:bx+2], 0
.no_calls:
    ; Write 0 to *fallbacks if non-NULL
    pop bx                  ; BX = fallbacks offset
    pop ax                  ; AX = fallbacks segment (was CX)
    mov cx, ax
    or ax, bx               ; test NULL
    jz .done
    mov es, cx
    mov word [es:bx], 0
    mov word [es:bx+2], 0
.done:
    retf
%endif

; =============================================================================
; SECTION: pcimux_rt stubs
; =============================================================================

%ifndef MOD_RT_PCIMUX_IMPLEMENTED
STUB_RET0  multiplex_is_shim_enabled_
STUB_VOID  multiplex_set_shim_enabled_

; multiplex_get_stats: write 0 to uint32_t out-pointer
; Params: calls=DX:AX (far ptr)
global multiplex_get_stats_
multiplex_get_stats_:
    mov bx, ax
    or ax, dx
    jz .done
    mov es, dx
    mov word [es:bx], 0
    mov word [es:bx+2], 0
.done:
    retf
%endif

; =============================================================================
; SECTION: hwchksm_rt stubs
; =============================================================================

%ifndef MOD_RT_HWCHKSM_IMPLEMENTED
STUB_RET0  hw_checksum_tx_calculate_
STUB_RET0  hw_checksum_rx_validate_
STUB_RET0  sw_checksum_internet_
STUB_RET0  hw_checksum_is_supported_       ; returns bool (false)
STUB_RET0  hw_checksum_get_optimal_mode_

; hw_checksum_get_stats: memset stats to 0 (56 bytes), return 0
; Params: stats=DX:AX (far ptr)
global hw_checksum_get_stats_
hw_checksum_get_stats_:
    mov bx, ax
    or ax, dx
    jz .done
    push di
    mov es, dx
    mov di, bx
    xor ax, ax
    mov cx, 56              ; sizeof(checksum_stats_t)
    cld
    rep stosb
    pop di
.done:
    xor ax, ax              ; return 0
    retf

STUB_RET0  hw_checksum_clear_stats_
STUB_VOID  hw_checksum_print_stats_
STUB_RET0  hw_checksum_calculate_ip_
STUB_RET0  hw_checksum_validate_ip_

; hw_checksum_mode_to_string: return far ptr to "Unknown"
global hw_checksum_mode_to_string_
hw_checksum_mode_to_string_:
    mov ax, _str_unknown
    mov dx, seg _str_unknown
    retf
%endif

; =============================================================================
; SECTION: irqmit_rt stubs
; =============================================================================

%ifndef MOD_RT_IRQMIT_IMPLEMENTED
STUB_RET0  is_interrupt_mitigation_enabled_ ; returns bool (false)

; get_mitigation_context: bounds check, return &g_mitigation_contexts[index*105]
; Params: nic_index=AX (uint8_t)
global get_mitigation_context_
get_mitigation_context_:
    cmp ax, 8               ; MAX_NICS
    jae .null
    mov cx, 105             ; sizeof(interrupt_mitigation_context_t)
    mul cx                  ; AX = index * 105
    add ax, _g_mitigation_contexts
    mov dx, seg _g_mitigation_contexts
    retf
.null:
    xor ax, ax
    xor dx, dx
    retf

STUB_VOID  interrupt_mitigation_apply_runtime_
%endif

; =============================================================================
; SECTION: rxbatch_rt stubs
; =============================================================================

%ifndef MOD_RT_RXBATCH_IMPLEMENTED
; rx_alloc_64k_safe: write 0 to phys_out, return NULL far ptr
; Params: len=AX, phys_out=DX:BX (far ptr)
global rx_alloc_64k_safe_
rx_alloc_64k_safe_:
    ; phys_out: segment=DX, offset=BX
    mov ax, bx
    or ax, dx
    jz .null
    mov es, dx
    mov word [es:bx], 0
    mov word [es:bx+2], 0
.null:
    xor ax, ax
    xor dx, dx
    retf

STUB_RET0  rx_batch_refill_
STUB_RET0  rx_batch_process_
STUB_VOID  rx_batch_get_stats_
%endif

; =============================================================================
; SECTION: txlazy_rt stubs
; =============================================================================

%ifndef MOD_RT_TXLAZY_IMPLEMENTED
STUB_RET1  tx_lazy_should_interrupt_
STUB_RET0  tx_lazy_post_boomerang_
STUB_RET0  tx_lazy_post_vortex_
STUB_RET0  tx_lazy_reclaim_batch_
%endif

; =============================================================================
; SECTION: xms_core_rt stubs
; =============================================================================

%ifndef MOD_RT_XMS_IMPLEMENTED
STUB_RETM1 xms_lock_
STUB_RETM1 xms_unlock_
STUB_RETM1 xms_copy_

; xms_query_free: write 0 to two uint32_t out-pointers, return -1
; Params: free_kb=DX:AX, largest_kb=CX:BX
global xms_query_free_
xms_query_free_:
    ; Save largest_kb ptr
    push cx
    push bx
    ; Write 0 to *free_kb if non-NULL
    mov bx, ax
    or ax, dx
    jz .no_free
    mov es, dx
    mov word [es:bx], 0
    mov word [es:bx+2], 0
.no_free:
    ; Write 0 to *largest_kb if non-NULL
    pop bx
    pop ax
    mov cx, ax
    or ax, bx
    jz .done
    mov es, cx
    mov word [es:bx], 0
    mov word [es:bx+2], 0
.done:
    mov ax, -1              ; return -1
    retf

STUB_RETM1 xms_enable_a20_
STUB_RETM1 xms_disable_a20_
STUB_RET0  xms_query_a20_
STUB_RET0  xms_promisc_available_
STUB_RET0  xms_routing_available_

; xms_unavailable_reason: return far ptr to g_xms_unavail_reason
global xms_unavailable_reason_
xms_unavailable_reason_:
    mov ax, _g_xms_unavail_reason
    mov dx, seg _g_xms_unavail_reason
    retf
%endif

; =============================================================================
; SECTION: pktops_rt stubs
; =============================================================================

%ifndef MOD_RT_PKTOPS_IMPLEMENTED
STUB_RET0  packet_get_ethertype_
STUB_RET0  packet_queue_tx_completion_
STUB_RETM1 packet_test_internal_loopback_
%endif

; =============================================================================
; SECTION: logging_rt stubs
; =============================================================================

STUB_VOID  log_debug_
STUB_VOID  log_info_
STUB_VOID  log_warning_
STUB_VOID  log_error_
STUB_VOID  log_critical_
STUB_VOID  log_at_level_
STUB_VOID  log_debug_category_
STUB_VOID  log_warning_category_
STUB_VOID  log_error_category_
STUB_RET0  log_read_ring_buffer_

; logging_get_stats: write 0 to three unsigned long out-pointers
; Params: written=DX:AX, dropped=CX:BX, overruns=stack
global logging_get_stats_
logging_get_stats_:
    push bp
    mov bp, sp
    ; Save dropped ptr
    push cx
    push bx
    ; Write 0 to *written if non-NULL
    mov bx, ax
    or ax, dx
    jz .no_written
    mov es, dx
    mov word [es:bx], 0
    mov word [es:bx+2], 0
.no_written:
    ; Write 0 to *dropped if non-NULL
    pop bx
    pop ax
    mov cx, ax
    or ax, bx
    jz .no_dropped
    mov es, cx
    mov word [es:bx], 0
    mov word [es:bx+2], 0
.no_dropped:
    ; Write 0 to *overruns if non-NULL (stack param at [bp+6])
    mov bx, [bp+6]
    mov ax, [bp+8]
    or ax, bx
    jz .done
    mov es, [bp+8]
    mov word [es:bx], 0
    mov word [es:bx+2], 0
.done:
    pop bp
    retf

STUB_RET0  logging_ring_buffer_enabled_
STUB_RET0  logging_is_enabled_
STUB_RET0  logging_get_level_

; logging_get_config: write 0 to three int out-pointers
; Params: level=DX:AX, categories=CX:BX, outputs=stack
global logging_get_config_
logging_get_config_:
    push bp
    mov bp, sp
    ; Save categories ptr
    push cx
    push bx
    ; Write 0 to *level if non-NULL
    mov bx, ax
    or ax, dx
    jz .no_level
    mov es, dx
    mov word [es:bx], 0
.no_level:
    ; Write 0 to *categories if non-NULL
    pop bx
    pop ax
    mov cx, ax
    or ax, bx
    jz .no_cat
    mov es, cx
    mov word [es:bx], 0
.no_cat:
    ; Write 0 to *outputs if non-NULL (stack param at [bp+6])
    mov bx, [bp+6]
    mov ax, [bp+8]
    or ax, bx
    jz .done
    mov es, [bp+8]
    mov word [es:bx], 0
.done:
    pop bp
    retf

; =============================================================================
; SECTION: pktops_rt packet operation stubs
; =============================================================================

%ifndef MOD_RT_PKTOPS_IMPLEMENTED
STUB_RETM1 packet_send_enhanced_
STUB_RETM1 packet_receive_from_nic_
STUB_RET0  packet_receive_process_
STUB_VOID  packet_process_deferred_work_
STUB_RET0  packet_isr_receive_
STUB_RETM1 packet_build_ethernet_frame_
STUB_RETM1 packet_parse_ethernet_header_
STUB_RETM1 packet_send_with_retry_
%endif

; =============================================================================
; SECTION: ISR handler stubs (interrupt convention)
; =============================================================================

%ifndef MOD_RT_PCIMUX_IMPLEMENTED
global multiplex_handler_
multiplex_handler_:
    iret
%endif

%ifndef MOD_RT_PCISHIM_IMPLEMENTED
global pci_shim_handler_
pci_shim_handler_:
    iret
%endif
