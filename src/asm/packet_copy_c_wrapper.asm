; @file packet_copy_c_wrapper.asm
; @brief C-callable wrapper for packet_copy_fast (ISR-safe copy)
;
; Exposes a simple C ABI function that copies bytes using the optimized
; ASM routine packet_copy_fast. Intended for ISR paths where minimizing
; prolog/epilog and keeping DF/segment control matters.

.MODEL SMALL
.386

EXTERN packet_copy_fast:PROC

_TEXT SEGMENT
        ASSUME  CS:_TEXT

; void asm_packet_copy_fast(void* dest, const void* src, uint16_t len);
PUBLIC asm_packet_copy_fast
asm_packet_copy_fast PROC
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
asm_packet_copy_fast ENDP

_TEXT ENDS

END

