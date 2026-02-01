;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; @file mod_data.asm
;; @brief Runtime data area template (JIT-assembled)
;;
;; Fixed-offset data layout used by all other modules. Contains MAC address,
;; I/O base, IRQ number, counters, and ring buffer pointers. All fields are
;; at fixed offsets from the module base for direct inter-module access.
;;
;; No executable code - data declarations only.
;;
;; Hot path size target: ~2.0KB (data area)
;;
;; Last Updated: 2026-02-01 11:02:20 CET
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

bits 16
cpu 286

%include "patch_macros.inc"

;; ============================================================================
;; Patch type constants (local aliases)
;; ============================================================================
PATCH_TYPE_IMM16        equ 6
PATCH_TYPE_IMM8         equ 7
PATCH_TYPE_RELOC_NEAR   equ 8

;; ############################################################################
;; MODULE SEGMENT
;; ############################################################################
segment MODULE class=MODULE align=16

;; ============================================================================
;; 64-byte Module Header
;; ============================================================================
global _mod_data_header
_mod_data_header:
    db  'PKTDRV', 0             ; +00  7 bytes: module signature
    db  1                       ; +07  1 byte:  major version
    db  0                       ; +08  1 byte:  minor version
    db  0                       ; +09  1 byte:  cpu_req
    db  0                       ; +0A  1 byte:  nic_type
    db  0                       ; +0B  1 byte:  cap_flags
    dw  hot_end - hot_start     ; +0C  2 bytes: hot data size
    dw  patch_table             ; +0E  2 bytes: offset to patch table
    dw  patch_table_end - patch_table  ; +10  2 bytes: patch table size
    dw  hot_start               ; +12  2 bytes: offset to hot_start
    dw  hot_end                 ; +14  2 bytes: offset to hot_end
    times 64 - ($ - _mod_data_header) db 0  ; Pad to 64 bytes

;; ============================================================================
;; HOT DATA START - all offsets are relative to hot_start
;; ============================================================================
hot_start:

;; +0x0000: MAC address (6 bytes, patched by init)
global data_mac_addr
data_mac_addr:
    db  0, 0, 0, 0, 0, 0

;; +0x0006: I/O base address (2 bytes, patched by init)
global data_io_base
data_io_base:
    dw  0

;; +0x0008: IRQ number (1 byte, patched by init)
global data_irq_number
data_irq_number:
    db  0

;; +0x0009: DMA channel (1 byte, 0xFF = none)
global data_dma_channel
data_dma_channel:
    db  0FFh

;; +0x000A: NIC type identifier (2 bytes)
global data_nic_type
data_nic_type:
    dw  0

;; +0x000C: CPU type identifier (2 bytes)
global data_cpu_type
data_cpu_type:
    dw  0

;; +0x000E: Runtime flags (2 bytes)
global data_flags
data_flags:
    dw  0

;; +0x0010: Packets received counter (4 bytes)
global data_packets_rx
data_packets_rx:
    dd  0

;; +0x0014: Packets transmitted counter (4 bytes)
global data_packets_tx
data_packets_tx:
    dd  0

;; +0x0018: Bytes received counter (4 bytes)
global data_bytes_rx
data_bytes_rx:
    dd  0

;; +0x001C: Bytes transmitted counter (4 bytes)
global data_bytes_tx
data_bytes_tx:
    dd  0

;; +0x0020: Error counter (4 bytes)
global data_errors
data_errors:
    dd  0

;; +0x0024: RX ring head index (2 bytes)
global data_rx_ring_head
data_rx_ring_head:
    dw  0

;; +0x0026: RX ring tail index (2 bytes)
global data_rx_ring_tail
data_rx_ring_tail:
    dw  0

;; +0x0028: TX ring head index (2 bytes)
global data_tx_ring_head
data_tx_ring_head:
    dw  0

;; +0x002A: TX ring tail index (2 bytes)
global data_tx_ring_tail
data_tx_ring_tail:
    dw  0

;; +0x002C: Start of ring buffers (sized at init time)
;;          Init code extends this area based on available memory.
global data_ring_buffers
data_ring_buffers:
    dw  0DEADh                              ; Sentinel / start marker

hot_end:

;; ============================================================================
;; PATCH TABLE
;; ============================================================================
patch_table:
    PATCH_TABLE_ENTRY  data_mac_addr,    PATCH_TYPE_COPY
    PATCH_TABLE_ENTRY  data_io_base,     PATCH_TYPE_IO
    PATCH_TABLE_ENTRY  data_irq_number,  PATCH_TYPE_IMM8
    PATCH_TABLE_ENTRY  data_dma_channel, PATCH_TYPE_IMM8
    PATCH_TABLE_ENTRY  data_nic_type,    PATCH_TYPE_IMM16
    PATCH_TABLE_ENTRY  data_cpu_type,    PATCH_TYPE_IMM16
patch_table_end:
