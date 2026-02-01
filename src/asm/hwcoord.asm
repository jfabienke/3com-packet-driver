; @file hwcoord.asm
; @brief Coordinator module - Shared data structures for hardware modules
; @note Renamed from hardware.asm to avoid conflict with src/c/hardware.c
;
; 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
; This module provides shared data structures and coordinates the hardware
; submodules (hwdet.asm, hwinit.asm, hwcfg.asm, hweep.asm, hwpkt.asm,
; hwdma.asm, hwbus.asm).
;
; Created: 2026-01-25 11:34:15 from hardware.asm modularization
; This file is part of the 3Com Packet Driver project.

bits 16
cpu 386

; C symbol naming bridge (maps C symbols to _symbol)
%include "csym.inc"

; Include assembly interface definitions
%define HARDWARE_MODULE_DEFINING
%define HARDWARE_DATA_DEFINING
%include "asm_interfaces.inc"
%include "lfsr_table.inc"

;=============================================================================
; JIT MODULE HEADER
;=============================================================================
segment MODULE class=MODULE align=16

global _mod_hwcoord_header
_mod_hwcoord_header:
    db  'PKTDRV', 0             ; +00  7 bytes: module signature
    db  1                       ; +07  1 byte:  major version
    db  0                       ; +08  1 byte:  minor version
    db  0                       ; +09  1 byte:  cpu_req
    db  0                       ; +0A  1 byte:  nic_type (0 = generic)
    db  1                       ; +0B  1 byte:  cap_flags (1 = MOD_CAP_CORE)
    dw  hot_end - hot_start     ; +0C  2 bytes: hot code size
    dw  patch_table             ; +0E  2 bytes: offset to patch table
    dw  patch_table_end - patch_table  ; +10  2 bytes: patch table size
    dw  hot_start               ; +12  2 bytes: offset to hot_start
    dw  hot_end                 ; +14  2 bytes: offset to hot_end
    times 64 - ($ - _mod_hwcoord_header) db 0  ; Pad to 64 bytes

;=============================================================================
; EXTERNAL MODULE FUNCTIONS
; All actual code has been moved to submodules
;=============================================================================

; hwdet.asm - Hardware Detection
extern detect_3c509b, detect_3c515, hardware_detect_all
extern detect_3c509b_device, detect_3c515_device

; hwinit.asm - Hardware Initialization
extern init_3c509b, init_3c515, hardware_init_asm, hardware_init_system
extern reset_3c509b_device, reset_3c515_device

; hwcfg.asm - Hardware Configuration
extern hardware_configure_3c509b, hardware_configure_3c515
extern configure_3c509b_device, configure_3c515_device
extern setup_3c509b_irq, setup_3c515_irq
extern hardware_detect_and_configure, hardware_get_device_info
extern hardware_set_device_state, hardware_handle_interrupt
extern hardware_validate_configuration

; hweep.asm - EEPROM Operations
extern hardware_get_address, read_3c509b_eeprom, read_3c515_eeprom

; hwpkt.asm - Packet I/O Operations
extern hardware_read_packet, hardware_send_packet_asm
extern io_read_byte, io_write_byte, io_read_word, io_write_word

; hwdma.asm - DMA Operations
extern dma_stall_engines, dma_unstall_engines
extern dma_start_transfer, dma_get_engine_status
extern dma_prepare_coherent_buffer, dma_complete_coherent_buffer
extern setup_advanced_dma_descriptors, advanced_dma_interrupt_check
extern isa_virt_to_phys, check_isa_dma_boundary
extern setup_isa_dma_descriptor, init_3c515_bus_master

; hwbus.asm - Bus Detection
extern is_mca_system, get_ps2_model
extern nic_detect_mca_3c523, nic_detect_mca_3c529
extern is_eisa_system, nic_detect_eisa_3c592
extern nic_detect_eisa_3c597, nic_detect_vlb

; hwirq.asm - IRQ Handlers
extern hardware_handle_3c509b_irq, hardware_handle_3c515_irq

; External utilities
extern get_cpu_features     ; From cpu_detect.asm
extern log_warning          ; Log warning messages
extern log_error            ; Log error messages

;=============================================================================
; SHARED DATA SEGMENT
; All data structures defined here and exported globally for other modules
;=============================================================================

section .data

; Enhanced hardware detection state using structured approach
global hw_instance_table, hw_instance_count
hw_instance_table:   times MAX_HW_INSTANCES * HW_INSTANCE_size db 0
hw_instance_count:   db 0        ; Number of detected instances

; Legacy compatibility fields (maintained for existing code)
global hw_instances, hw_io_bases, hw_irq_lines, hw_types, hw_mac_addresses
hw_instances:        times MAX_HW_INSTANCES db HW_STATE_UNDETECTED
hw_io_bases:         times MAX_HW_INSTANCES dw 0
hw_irq_lines:        times MAX_HW_INSTANCES db 0
hw_types:            times MAX_HW_INSTANCES db 0    ; 0=unknown, 1=3C509B, 2=3C515
hw_mac_addresses:    times MAX_HW_INSTANCES*6 db 0  ; MAC addresses

; Additional tables for enhanced implementation
global hw_iobase_table, hw_type_table, hw_flags_table
; Also export underscore versions for C code (Watcom adds underscore prefix)
global _hw_iobase_table, _hw_type_table, _hw_flags_table
global current_instance, current_iobase, current_irq
global _current_instance, _current_iobase, _current_irq
hw_iobase_table:
_hw_iobase_table:    times MAX_HW_INSTANCES dw 0     ; I/O base addresses
hw_type_table:
_hw_type_table:      times MAX_HW_INSTANCES db 0     ; NIC types (1=3C509B, 2=3C515)
hw_flags_table:
_hw_flags_table:     times MAX_HW_INSTANCES db 0     ; Hardware flags
current_instance:
_current_instance:   db 0                           ; Current active instance
current_iobase:
_current_iobase:     dw 0                           ; Current NIC I/O base
current_irq:
_current_irq:        db 0                           ; Current NIC IRQ

; Enhanced I/O operation statistics
global io_read_count, io_write_count
global io_error_count, io_timeout_count, io_retry_count
io_read_count:       dd 0        ; Total I/O reads
io_write_count:      dd 0        ; Total I/O writes
io_error_count:      dw 0        ; I/O error count
io_timeout_count:    dw 0        ; I/O timeout count
io_retry_count:      dw 0        ; I/O retry count

; System state management
global hardware_initialized, current_instance_2, last_error_code, debug_flags
hardware_initialized: db 0       ; Hardware subsystem initialized
current_instance_2:  db 0        ; Currently selected hardware instance
last_error_code:     db 0        ; Last error code encountered
debug_flags:         db 0        ; Debug control flags

; Timing and synchronization
global timestamp_last_detect, timestamp_last_config
timestamp_last_detect: dd 0      ; Last detection timestamp
timestamp_last_config: dd 0      ; Last configuration timestamp

;=============================================================================
; MINIMAL CODE SECTION - Placeholder for linker
;=============================================================================

section .text
hot_start:

; No code in coordinator - all implementations in submodules
; This section exists only to ensure proper object file generation

hot_end:

patch_table:
patch_table_end:

; End of coordinator module
