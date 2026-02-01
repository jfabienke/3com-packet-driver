;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; @file mod_pio.asm
;; @brief Programmed I/O transfer module for 8086/286 ISA systems
;;
;; JIT-assembled runtime module for 16-bit DOS packet driver TSR.
;; Performs programmed I/O transfers using REP INSW/OUTSW from/to NIC FIFO.
;;
;; Last Updated: 2026-02-01 11:02:20 CET
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

bits 16

%include "patch_macros.inc"

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Constants
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
CPU_REQ         equ 0               ; 8086 baseline - works on all CPUs
CAP_FLAGS       equ 0x0000          ; No special capabilities required
PATCH_COUNT     equ 2               ; Two patch points: read port, write port

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Module segment
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
section .text class=MODULE

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; 64-byte module header
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
global _mod_pio_header
_mod_pio_header:
header:
    db 'PKTDRV',0                   ; 7 bytes - module signature
    db 1, 0                         ; 2 bytes - version 1.0
    dw hot_start                    ; hot section start offset
    dw hot_end                      ; hot section end offset
    dw 0, 0                         ; cold_start, cold_end (unused)
    dw patch_table                  ; patch table offset
    dw PATCH_COUNT                  ; number of patch entries
    dw (hot_end - header)           ; module_size
    dw (hot_end - hot_start)        ; required_memory
    db CPU_REQ                      ; cpu_requirements (8086)
    db 0                            ; nic_type (0 = any)
    dw CAP_FLAGS                    ; capability flags
    times (64 - ($ - header)) db 0  ; pad header to 64 bytes

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Hot section - performance-critical transfer routines
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
hot_start:

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; pio_read_packet - Read packet data from NIC FIFO port using REP INSW
;;
;; Input:
;;   ES:DI = destination buffer
;;   CX    = byte count to read
;; Output:
;;   ES:DI = updated past end of data
;;   CX    = 0
;; Clobbers: AX, CX, DX, DI
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
pio_read_packet:
    push    dx
    cld                             ; Ensure forward direction

    ;; Load NIC FIFO port address (patched at runtime)
    PATCH_POINT pp_read_port
    mov     dx, 0x0000              ; Placeholder: patched to NIC FIFO port

    ;; Convert byte count to word count, save odd byte flag
    mov     ax, cx
    shr     cx, 1                   ; CX = word count

    ;; Bulk word transfer from NIC FIFO
    rep insw                        ; Read CX words from port DX to ES:DI

    ;; Handle trailing odd byte if present
    test    al, 1                   ; Was original byte count odd?
    jz      pio_read_done
    in      al, dx                  ; Read final byte from port
    stosb                           ; Store to ES:DI

pio_read_done:
    pop     dx
    ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; pio_write_packet - Write packet data to NIC FIFO port using REP OUTSW
;;
;; Input:
;;   DS:SI = source buffer
;;   CX    = byte count to write
;; Output:
;;   DS:SI = updated past end of data
;;   CX    = 0
;; Clobbers: AX, CX, DX, SI
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
pio_write_packet:
    push    dx
    cld                             ; Ensure forward direction

    ;; Load NIC FIFO port address (patched at runtime)
    PATCH_POINT pp_write_port
    mov     dx, 0x0000              ; Placeholder: patched to NIC FIFO port

    ;; Convert byte count to word count, save odd byte flag
    mov     ax, cx
    shr     cx, 1                   ; CX = word count

    ;; Bulk word transfer to NIC FIFO
    rep outsw                       ; Write CX words from DS:SI to port DX

    ;; Handle trailing odd byte if present
    test    al, 1                   ; Was original byte count odd?
    jz      pio_write_done
    lodsb                           ; Load final byte from DS:SI
    out     dx, al                  ; Write to port

pio_write_done:
    pop     dx
    ret

hot_end:

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Patch table
;;
;; Each entry: dw offset, db type, db size
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
patch_table:
    PATCH_TABLE_ENTRY pp_read_port,  PATCH_TYPE_IO   ; NIC FIFO read port
    PATCH_TABLE_ENTRY pp_write_port, PATCH_TYPE_IO   ; NIC FIFO write port
