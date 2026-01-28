; linkasm.asm - ASM/C Naming Bridge (NEAR to FAR trampolines)
;
; 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
;
; This file provides assembly symbols that are referenced by other
; ASM modules without underscore. Since Watcom C adds TRAILING underscore
; to all symbols (symbol_), this file bridges the naming gap using trampolines.
;
; CRITICAL: tsrldr.asm uses NEAR calls (call cpu_detect_init)
;           C functions under -ml (large model) are FAR (return with retf)
;           Each trampoline uses "call far symbol_" then "ret" to bridge
;
; IMPORTANT: Watcom C uses TRAILING underscore (symbol_), NOT prefix underscore (_symbol)
;
; Last Updated: 2026-01-28 03:30:00 UTC
;
; Phase 5 Fix (Round 8):
;   - CRITICAL: Changed all extern/global from _symbol to symbol_ for Watcom C
;   - Packet functions: NOW TRAMPOLINED to pktops.c (packet_isr_receive_ etc)
;   - Hardware functions: NOW PROVIDED by hwpkt.asm, hwcfg.asm (added to build)
;   - Cache functions: NOW PROVIDED by cacheops.asm (added to build)
;   - DMA bounce buffer: Now calls dma_get_rx_bounce_buffer_ (real in dmabnd.c)
;   - Stage stubs: REMOVED - C owns them in linkstubs.c (avoid duplicates)
;   - init_3c515: NOW PROVIDED by hwinit.asm (added to build)
;   - packet_api_entry: NOW JUMPS to packet_driver_isr (pktapi.asm)
;   - PCI shim handler: Returns 0xFFFF (not handled) to chain to real BIOS
;
; DATA OWNERSHIP:
;   - indos_segment, indos_offset, criterr_segment, criterr_offset -> tsrcom.asm
;   - pcmcia_event_flag -> pcmmgr.c (use pcmcia_event_flag_)
;   - g_promisc_buffer_tail -> linkstubs.c (uint32_t, use g_promisc_buffer_tail_)
;   - THIS FILE DEFINES NO DATA - only trampolines and stubs

[BITS 16]
[CPU 8086]

; =============================================================================
; CODE SEGMENT - Trampoline functions bridging ASM near calls to C far calls
; =============================================================================
segment _TEXT class=CODE public use16

; -----------------------------------------------------------------------------
; INIT FUNCTIONS - Called via NEAR call from tsrldr.asm
; tsrldr.asm does: call main_init (near)
; C function under -ml is FAR, returns with retf
; Trampoline: call far symbol_, then ret (near return to ASM caller)
;
; WATCOM NAMING: C function int foo() -> foo_ (TRAILING underscore)
; -----------------------------------------------------------------------------

extern main_init_           ; FAR C function in loader/init_stubs.c
extern cpu_detect_init_     ; FAR C function in loader/cpu_detect.c
extern nic_detect_init_     ; FAR C function in loader/init_stubs.c
extern patch_init_and_apply_ ; FAR C function in loader/patch_apply.c

global main_init
global cpu_detect_init
global nic_detect_init
global patch_init_and_apply

main_init:
    call far main_init_     ; Far call to C (returns via retf)
    ret                     ; Near return to ASM caller

cpu_detect_init:
    call far cpu_detect_init_
    ret

nic_detect_init:
    call far nic_detect_init_
    ret

patch_init_and_apply:
    call far patch_init_and_apply_
    ret

; -----------------------------------------------------------------------------
; RUNTIME FUNCTIONS - Called from pktapi.asm, nicirq.asm
; These are called at runtime after driver is loaded, must be resident
;
; WATCOM NAMING: C function int foo() -> foo_ (TRAILING underscore)
; -----------------------------------------------------------------------------

extern dma_policy_set_runtime_      ; FAR C in dmapol.c
extern dma_policy_set_validated_    ; FAR C in dmapol.c
extern dma_policy_save_             ; FAR C in dmapol.c
extern interrupt_mitigation_apply_all_ ; FAR C in irqmit.c
extern hardware_set_media_mode_     ; FAR C in rtcfg.c
extern pcmcia_get_snapshot_         ; FAR C in pcmsnap.c

global dma_policy_set_runtime
global dma_policy_set_validated
global dma_policy_save
global interrupt_mitigation_apply_all
global hardware_set_media_mode
global pcmcia_get_snapshot

dma_policy_set_runtime:
    call far dma_policy_set_runtime_
    ret

dma_policy_set_validated:
    call far dma_policy_set_validated_
    ret

dma_policy_save:
    call far dma_policy_save_
    ret

interrupt_mitigation_apply_all:
    call far interrupt_mitigation_apply_all_
    ret

hardware_set_media_mode:
    call far hardware_set_media_mode_
    ret

pcmcia_get_snapshot:
    call far pcmcia_get_snapshot_
    ret

; -----------------------------------------------------------------------------
; PACKET FUNCTIONS - NOW PROVIDED by pktops.c (added to build)
; pktops.c provides: packet_isr_receive_, packet_receive_process_
; pktops.asm uses extern to call these C functions
;
; free_tx_buffer: Stub until tx buffer management is implemented
;
; WATCOM NAMING: C function int foo() -> foo_ (TRAILING underscore)
; -----------------------------------------------------------------------------

extern packet_isr_receive_      ; FAR C in pktops.c
extern packet_receive_process_  ; FAR C in pktops.c

global packet_isr_receive
global packet_receive_process
global free_tx_buffer

packet_isr_receive:
    call far packet_isr_receive_
    ret

packet_receive_process:
    call far packet_receive_process_
    ret

free_tx_buffer:
    ; STUB - Asynchronous TX buffer management not implemented
    ;
    ; LIMITATION: nicirq.asm calls free_tx_buffer on TX_COMPLETE interrupt
    ; to release the buffer used for the completed transmission. Without
    ; this implementation, bus master DMA TX will leak buffers.
    ;
    ; CURRENT STATE: C code (api.c, hardware.c) uses synchronous TX that
    ; allocates, sends, and immediately frees. The ISR path is for async
    ; bus master DMA which isn't fully implemented.
    ;
    ; WORKAROUND: Use PIO mode (synchronous) for TX until async is added.
    ; TODO: Implement tx_pending tracking and buffer_free_any() call here.
    ret

; -----------------------------------------------------------------------------
; HARDWARE FUNCTIONS - NOW PROVIDED by hwpkt.asm, hwcfg.asm, hwcoord.asm
; These ASM modules are now added to the build and provide:
;   hwpkt.asm: hardware_read_packet, hardware_send_packet_asm, io_read/write_*
;   hwcfg.asm: hardware_configure_3c509b, configure_3c509b_device, etc.
;   hwcoord.asm: hw_flags_table, hw_instance_table, etc.
; NO STUBS NEEDED HERE - symbols come from those modules
; -----------------------------------------------------------------------------

; packet_api_entry - Alias to real packet driver ISR in pktapi.asm
; tsrcom.asm installs INT 60h vector pointing here
extern packet_driver_isr    ; NEAR in pktapi.asm

global packet_api_entry
packet_api_entry:
    jmp packet_driver_isr   ; Jump to real packet driver ISR

; init_3c515 - NOW PROVIDED by hwinit.asm (added to build)
; hwinit.asm provides init_3c515 for 3C515 hardware initialization
; Called by nicirq.asm for recovery on fatal 3C515 errors
; NO STUB NEEDED - symbol comes from hwinit.asm

; -----------------------------------------------------------------------------
; DMA BUFFER FUNCTIONS - Alias for ASM callers without underscore
; CRITICAL: dma_get_rx_bounce_buffer_ is defined in dmabnd.c:453
;           C code links directly to dmabnd.c, no ASM stub needed.
;           We only provide non-underscored alias for ASM callers.
;
; WATCOM NAMING: C function int foo() -> foo_ (TRAILING underscore)
; -----------------------------------------------------------------------------

extern dma_get_rx_bounce_buffer_    ; FAR C in dmabnd.c - REAL implementation

; NOTE: dma_get_rx_bounce_buffer_ is NOT defined here - it comes from dmabnd.c
;       C code calling dma_get_rx_bounce_buffer_ links directly to dmabnd.obj

; Non-underscored version for ASM callers
global dma_get_rx_bounce_buffer

dma_get_rx_bounce_buffer:
    ; ASM callers use this name without underscore
    ; Trampoline to the real C function in dmabnd.c
    call far dma_get_rx_bounce_buffer_
    ret

; PCI shim handler - Called by pciisr.asm for PCI BIOS interception
; Expected interface: FAR call, far pointer to param struct on stack
; Returns: AX=0xFFFF (not handled, chain to BIOS), or handled status
;
; NOTE: pci_shim.c provides pci_shim_handler as an __interrupt handler
; installed via _dos_setvect. This pci_shim_handler_c_ is for pciisr.asm
; which uses a different calling convention. Until a proper adapter is
; written, we return "not handled" to safely chain to the real PCI BIOS.
;
; WATCOM NAMING: Provide both trailing underscore version (C callers)
;                and non-underscored version (ASM callers)

global pci_shim_handler_c_
global pci_shim_handler_c

pci_shim_handler_c_:
    ; Return 0xFFFF = not handled, pciisr.asm will chain to real BIOS
    mov ax, 0FFFFh
    retf

pci_shim_handler_c:
    ; Non-underscored alias for ASM callers
    mov ax, 0FFFFh
    ret

; -----------------------------------------------------------------------------
; DEFERRED WORK QUEUE FUNCTIONS - Called from tsrcom.asm
;
; WATCOM NAMING: C function int foo() -> foo_ (TRAILING underscore)
; -----------------------------------------------------------------------------

extern periodic_vector_monitoring_  ; FAR C in unwind.c
extern deferred_work_queue_add_     ; FAR C in dos_idle.c
extern deferred_work_queue_process_ ; FAR C in dos_idle.c

global periodic_vector_monitoring
global deferred_work_queue_add
global deferred_work_queue_process

periodic_vector_monitoring:
    call far periodic_vector_monitoring_
    ret

deferred_work_queue_add:
    call far deferred_work_queue_add_
    ret

deferred_work_queue_process:
    call far deferred_work_queue_process_
    ret

; Promiscuous mode buffer function - stub until promisc.c provides it
; WATCOM NAMING: Provide trailing underscore version for C callers
global promisc_add_buffer_packet_asm_
global promisc_add_buffer_packet_asm

promisc_add_buffer_packet_asm_:
    ; Stub - real implementation would add packet to buffer
    xor ax, ax
    retf

promisc_add_buffer_packet_asm:
    call far promisc_add_buffer_packet_asm_
    ret

; -----------------------------------------------------------------------------
; CACHE FUNCTIONS - C-compatible wrappers for cacheops.asm
; cacheops.asm exports non-underscored names (cache_wbinvd)
; C code expects Watcom-mangled names (cache_wbinvd_)
; These trampolines bridge the naming gap
;
; WATCOM NAMING: C expects cache_wbinvd_ (TRAILING underscore)
; cacheops.asm exports cache_wbinvd (no underscore)
; So we alias cache_wbinvd_ -> cache_wbinvd
; -----------------------------------------------------------------------------

extern cache_wbinvd
extern cache_wbinvd_safe
extern cache_clflush_line
extern cache_clflush_safe
extern memory_fence

global cache_wbinvd_
global cache_wbinvd_safe_
global cache_clflush_line_
global cache_clflush_safe_
global memory_fence_

cache_wbinvd_:
    call cache_wbinvd
    retf

cache_wbinvd_safe_:
    call cache_wbinvd_safe
    retf

cache_clflush_line_:
    call cache_clflush_line
    retf

cache_clflush_safe_:
    call cache_clflush_safe
    retf

memory_fence_:
    call memory_fence
    retf

; =============================================================================
; STAGE FUNCTIONS - Trampolines from non-underscored to underscored C names
; =============================================================================
; init_main.c uses #pragma aux "*" to call these with unmangled names.
; But linkstubs.c exports them with trailing underscore (Watcom default).
; These trampolines bridge the gap.
; =============================================================================

extern stage_entry_validation_
extern stage_cpu_detect_
extern stage_platform_probe_
extern stage_logging_init_
extern stage_config_parse_
extern stage_chipset_detect_
extern stage_vds_dma_refine_
extern stage_memory_init_
extern stage_packet_ops_init_
extern stage_hardware_detect_
extern stage_dma_buffer_init_
extern stage_tsr_relocate_
extern stage_api_install_
extern stage_irq_enable_
extern stage_api_activate_

global stage_entry_validation
global stage_cpu_detect
global stage_platform_probe
global stage_logging_init
global stage_config_parse
global stage_chipset_detect
global stage_vds_dma_refine
global stage_memory_init
global stage_packet_ops_init
global stage_hardware_detect
global stage_dma_buffer_init
global stage_tsr_relocate
global stage_api_install
global stage_irq_enable
global stage_api_activate

stage_entry_validation:
    jmp far stage_entry_validation_

stage_cpu_detect:
    jmp far stage_cpu_detect_

stage_platform_probe:
    jmp far stage_platform_probe_

stage_logging_init:
    jmp far stage_logging_init_

stage_config_parse:
    jmp far stage_config_parse_

stage_chipset_detect:
    jmp far stage_chipset_detect_

stage_vds_dma_refine:
    jmp far stage_vds_dma_refine_

stage_memory_init:
    jmp far stage_memory_init_

stage_packet_ops_init:
    jmp far stage_packet_ops_init_

stage_hardware_detect:
    jmp far stage_hardware_detect_

stage_dma_buffer_init:
    jmp far stage_dma_buffer_init_

stage_tsr_relocate:
    jmp far stage_tsr_relocate_

stage_api_install:
    jmp far stage_api_install_

stage_irq_enable:
    jmp far stage_irq_enable_

stage_api_activate:
    jmp far stage_api_activate_

; =============================================================================
; DATA SEGMENT - ONLY EXTERN DECLARATIONS, NO DEFINITIONS
; All data is owned by C files or tsrcom.asm
; =============================================================================
segment _DATA class=DATA public use16

; These are owned by tsrcom.asm - declare extern for reference
extern indos_segment
extern indos_offset
extern criterr_segment
extern criterr_offset

; These are owned by C files - ASM callers should use symbol_ directly:
; extern pcmcia_event_flag_          ; pcmmgr.c (volatile uint8_t)
; extern g_promisc_buffer_tail_      ; linkstubs.c (uint32_t)
; extern isr_active_                 ; linkstubs.c
; extern nic_io_base_                ; linkstubs.c

; NO DATA DEFINITIONS HERE
; Data is defined in C files (linkstubs.c, pcmmgr.c) or tsrcom.asm

; End of linkasm.asm
