;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; @file tsr_crt.asm
;; @brief CRT replacement for resident TSR - strlen + fmemcpy
;;
;; Provides the only two C library functions needed at runtime in the
;; flat TSR image, eliminating the need for the Watcom CRT.
;;
;; Last Updated: 2026-02-01 18:20:35 CET
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

bits 16
cpu 8086

;; ============================================================================
;; MODULE HEADER (JIT copy-down)
;; ============================================================================
segment MODULE class=MODULE align=16

global _mod_tsr_crt_header
_mod_tsr_crt_header:
    db  'PKTDRV', 0             ; +00  7 bytes: module signature
    db  1                       ; +07  1 byte:  major version
    db  0                       ; +08  1 byte:  minor version
    db  0                       ; +09  1 byte:  cpu_req (8086)
    db  0                       ; +0A  1 byte:  nic_type (any)
    db  1                       ; +0B  1 byte:  cap_flags (MOD_CAP_CORE)
    dw  hot_end - hot_start     ; +0C  2 bytes: hot code size
    dw  patch_table             ; +0E  2 bytes: offset to patch table
    dw  patch_table_end - patch_table  ; +10  2 bytes: patch table size
    dw  hot_start               ; +12  2 bytes: offset to hot_start
    dw  hot_end                 ; +14  2 bytes: offset to hot_end
    times 64 - ($ - _mod_tsr_crt_header) db 0  ; Pad to 64 bytes

;; ============================================================================
;; HOT SECTION
;; ============================================================================
hot_start:

;;----------------------------------------------------------------------------
;; tsr_strlen - Get string length (near call)
;;
;; Input:  DS:AX -> null-terminated string
;; Output: AX = length (not counting null)
;; Clobbers: CX, DI
;;----------------------------------------------------------------------------
global tsr_strlen_
tsr_strlen_:
    push    di
    push    es
    push    ds
    pop     es              ; ES = DS
    mov     di, ax          ; ES:DI -> string
    xor     al, al          ; Search for null
    mov     cx, 0FFFFh      ; Max search length
    repne scasb
    not     cx
    dec     cx              ; CX = length
    mov     ax, cx
    pop     es
    pop     di
    retf

;;----------------------------------------------------------------------------
;; tsr_fmemcpy - Far memory copy
;;
;; Watcom large model calling convention:
;;   far void * tsr_fmemcpy(void far *dst, const void far *src, size_t n)
;;   Stack: [BP+6] dst_off, [BP+8] dst_seg, [BP+10] src_off, [BP+12] src_seg,
;;          [BP+14] count
;;   Returns: DX:AX = dst pointer
;;----------------------------------------------------------------------------
global tsr_fmemcpy_
tsr_fmemcpy_:
    push    bp
    mov     bp, sp
    push    ds
    push    es
    push    si
    push    di

    ; Load destination (ES:DI)
    les     di, [bp+6]      ; dst far pointer
    ; Load source (DS:SI)
    lds     si, [bp+10]     ; src far pointer
    ; Load count
    mov     cx, [bp+14]     ; byte count

    ; Copy
    rep movsb

    ; Return dst pointer in DX:AX
    mov     ax, [bp+6]      ; dst offset
    mov     dx, [bp+8]      ; dst segment

    pop     di
    pop     si
    pop     es
    pop     ds
    pop     bp
    retf

hot_end:

;; ============================================================================
;; PATCH TABLE (none needed for CRT functions)
;; ============================================================================
patch_table:
patch_table_end:
