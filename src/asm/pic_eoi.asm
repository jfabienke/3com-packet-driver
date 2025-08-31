; @file pic_eoi.asm
; @brief PIC End-of-Interrupt handling for TSR defensive programming
;
; Provides proper EOI handling for both master (8259A) and slave PICs
; following IBM PC/AT interrupt controller conventions.

.MODEL SMALL
.386

_TEXT SEGMENT
        ASSUME  CS:_TEXT

; ======================================================
; send_eoi - Send End-of-Interrupt to appropriate PIC(s)
;
; Sends non-specific EOI to the correct PIC based on IRQ number.
; For IRQs 8–15, sends EOI to slave (0xA0) first, then master (0x20).
; For IRQs 0–7, sends EOI to master (0x20) only.
; Out-of-range IRQ values default to sending to both PICs (safe fallback).
;
; Input:  AL = IRQ number (0..15). Other values => send to both PICs.
; Output: AL preserved. Other registers preserved.
; Uses:   None (internally uses AL but restores it)
; ======================================================
PUBLIC send_eoi
send_eoi PROC
        push    ax                      ; Preserve AL for caller

        cmp     al, 8
        jb      short .master_only      ; IRQ 0-7 -> master PIC only

        ; IRQ 8-15 (or invalid): Send EOI to slave PIC first, then master
        ; This is required because slave PIC is cascaded through master IRQ 2
        mov     al, 20h                 ; Non-specific EOI command
        out     0A0h, al                ; Send to slave PIC command port
        out     020h, al                ; Send to master PIC command port
        pop     ax                      ; Restore original AL
        ret

.master_only:
        ; IRQ 0-7: Send EOI to master PIC only
        mov     al, 20h                 ; Non-specific EOI command
        out     020h, al                ; Send to master PIC command port
        pop     ax                      ; Restore original AL
        ret
send_eoi ENDP

; ======================================================
; send_eoi_for_irq - Alias for send_eoi for clarity
; Send End-of-Interrupt for specific IRQ number
;
; Input:  AL = IRQ number (0..15)
; Output: AL preserved. Other registers preserved.
; ======================================================
PUBLIC send_eoi_for_irq
send_eoi_for_irq PROC
        jmp     send_eoi                ; Use existing send_eoi implementation
send_eoi_for_irq ENDP

; ======================================================
; send_specific_eoi - Send specific EOI to appropriate PIC(s)
;
; Sends specific EOI command to the correct PIC. This is more precise
; than non-specific EOI and recommended for production code.
;
; Input:  AL = IRQ number (0..15)
; Output: AL preserved. Other registers preserved.
; Uses:   None (internally uses AL but restores it)
; ======================================================
PUBLIC send_specific_eoi
send_specific_eoi PROC
        push    ax                      ; Preserve AL for caller
        push    bx                      ; We need BX for calculations

        mov     bl, al                  ; BL = IRQ number
        cmp     bl, 8
        jb      short .master_specific

        ; IRQ 8-15: Send specific EOI to both PICs
        ; For slave PIC: specific EOI = 60h + (IRQ - 8)
        ; For master PIC: specific EOI = 62h (always IRQ 2, cascade)
        sub     bl, 8                   ; Convert to slave IRQ (0-7)
        add     bl, 60h                 ; Specific EOI base (60h) + IRQ
        mov     al, bl
        out     0A0h, al                ; Send specific EOI to slave PIC
        
        mov     al, 62h                 ; Specific EOI for IRQ 2 (cascade)
        out     020h, al                ; Send to master PIC
        
        pop     bx                      ; Restore BX
        pop     ax                      ; Restore original AL
        ret

.master_specific:
        ; IRQ 0-7: Send specific EOI to master PIC only
        ; Specific EOI = 60h + IRQ number
        add     bl, 60h                 ; Specific EOI base + IRQ
        mov     al, bl
        out     020h, al                ; Send to master PIC command port
        
        pop     bx                      ; Restore BX
        pop     ax                      ; Restore original AL
        ret
send_specific_eoi ENDP

; ======================================================
; mask_irq - Mask (disable) specific IRQ at PIC level
;
; Input:  AL = IRQ number (0..15)
; Output: None
; Uses:   AL, DX (internally)
; Note:   Caller must preserve IF and perform under CLI if atomicity required
; ======================================================
PUBLIC mask_irq
mask_irq PROC
        push    ax
        push    dx
        
        cmp     al, 8
        jb      short .mask_master
        
        ; IRQ 8-15: Mask on slave PIC
        sub     al, 8                   ; Convert to slave IRQ (0-7)
        mov     ah, 1
        mov     cl, al
        shl     ah, cl                  ; AH = bit mask for IRQ
        
        mov     dx, 0A1h                ; Slave PIC data port
        in      al, dx                  ; Read current mask
        or      al, ah                  ; Set IRQ bit (mask it)
        out     dx, al                  ; Write back
        jmp     short .done

.mask_master:
        ; IRQ 0-7: Mask on master PIC
        mov     ah, 1
        mov     cl, al
        shl     ah, cl                  ; AH = bit mask for IRQ
        
        mov     dx, 021h                ; Master PIC data port
        in      al, dx                  ; Read current mask
        or      al, ah                  ; Set IRQ bit (mask it)
        out     dx, al                  ; Write back
        
.done:
        pop     dx
        pop     ax
        ret
mask_irq ENDP

; ======================================================
; unmask_irq - Unmask (enable) specific IRQ at PIC level
;
; For IRQs 8-15, also ensures IRQ2 (cascade) is unmasked on master PIC
; to allow slave interrupts to propagate through the cascade connection.
;
; Input:  AL = IRQ number (0..15)
; Output: None
; Uses:   AL, DX (internally)
; Note:   Caller must preserve IF and perform under CLI if atomicity required
; ======================================================
PUBLIC unmask_irq
unmask_irq PROC
        push    ax
        push    dx
        push    cx
        
        cmp     al, 8
        jb      short .unmask_master
        
        ; IRQ 8-15: Unmask on slave PIC
        push    ax                      ; Save original IRQ number
        sub     al, 8                   ; Convert to slave IRQ (0-7)
        mov     ah, 1
        mov     cl, al
        shl     ah, cl                  ; AH = bit mask for IRQ
        not     ah                      ; Invert to create clear mask
        
        mov     dx, 0A1h                ; Slave PIC data port
        in      al, dx                  ; Read current mask
        and     al, ah                  ; Clear IRQ bit (unmask it)
        out     dx, al                  ; Write back
        
        ; CRITICAL: Also ensure IRQ2 (cascade) is unmasked on master
        ; Without this, slave interrupts won't reach the CPU
        mov     dx, 021h                ; Master PIC data port
        in      al, dx                  ; Read current master mask
        and     al, NOT (1 SHL 2)       ; Clear bit 2 (IRQ2 cascade)
        out     dx, al                  ; Write back
        
        pop     ax                      ; Restore original IRQ number
        jmp     short .done

.unmask_master:
        ; IRQ 0-7: Unmask on master PIC
        mov     ah, 1
        mov     cl, al
        shl     ah, cl                  ; AH = bit mask for IRQ
        not     ah                      ; Invert to create clear mask
        
        mov     dx, 021h                ; Master PIC data port
        in      al, dx                  ; Read current mask
        and     al, ah                  ; Clear IRQ bit (unmask it)
        out     dx, al                  ; Write back
        
.done:
        pop     cx
        pop     dx
        pop     ax
        ret
unmask_irq ENDP

_TEXT ENDS
END