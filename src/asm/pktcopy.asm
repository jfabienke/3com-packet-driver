; @file packet_copy_c_wrapper.asm
; @brief C-callable wrapper for packet_copy_fast (ISR-safe copy)
;
; Exposes a simple C ABI function that copies bytes using the optimized
; ASM routine packet_copy_fast. Intended for ISR paths where minimizing
; prolog/epilog and keeping DF/segment control matters.
;
; Converted to NASM syntax - 2026-01-23

bits 16
cpu 386

extern packet_copy_fast

; ############################################################################
; MODULE SEGMENT
; ############################################################################
segment MODULE class=MODULE align=16

; ============================================================================
; 64-byte Module Header
; ============================================================================
global _mod_pktcopy_header
_mod_pktcopy_header:
    db  'PKTDRV', 0             ; +00  7 bytes: module signature
    db  1                       ; +07  1 byte:  major version
    db  0                       ; +08  1 byte:  minor version
    db  0                       ; +09  1 byte:  cpu_req (0 = 8086)
    db  0                       ; +0A  1 byte:  nic_type (0 = generic)
    db  1                       ; +0B  1 byte:  cap_flags (1 = MOD_CAP_CORE)
    dw  hot_end - hot_start     ; +0C  2 bytes: hot code size
    dw  patch_table             ; +0E  2 bytes: offset to patch table
    dw  patch_table_end - patch_table  ; +10  2 bytes: patch table size
    dw  hot_start               ; +12  2 bytes: offset to hot_start
    dw  hot_end                 ; +14  2 bytes: offset to hot_end
    times 64 - ($ - _mod_pktcopy_header) db 0  ; Pad to 64 bytes

segment _TEXT class=CODE

; ============================================================================
; HOT PATH START
; ============================================================================
hot_start:

; void asm_packet_copy_fast(void* dest, const void* src, uint16_t len);
global asm_packet_copy_fast
asm_packet_copy_fast:
        push    bp
        mov     bp, sp
        push    ds
        push    es
        push    si
        push    di
        push    cx

        ; Load arguments (small model cdecl)
        ; [bp+4] = dest (near), [bp+6] = src (near), [bp+8] = len (uint16)
        mov     di, [bp+4]         ; DI = dest offset
        mov     si, [bp+6]         ; SI = src offset
        mov     cx, [bp+8]         ; CX = length

        ; Ensure ES = DS = DGROUP for near pointers
        mov     ax, ds
        mov     es, ax

        ; Call optimized copy
        call    packet_copy_fast

        ; Ignore AX return in C wrapper; no error reporting here

        pop     cx
        pop     di
        pop     si
        pop     es
        pop     ds
        pop     bp
        ret

; ============================================================================
; HOT PATH END
; ============================================================================
hot_end:

; ============================================================================
; PATCH TABLE
; ============================================================================
patch_table:
patch_table_end:
