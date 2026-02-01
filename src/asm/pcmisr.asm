; @file pcmcia_isr.asm
; @brief Tiny PCMCIA/Card status change ISR stub (integrated in TSR)
;
; Minimal ISR: set event flag in _DATA, EOI PIC, chain if not ours (optional).
; Install function: pcmcia_isr_install(irq)
; Converted to NASM syntax

        bits 16
        cpu 8086

; C symbol naming bridge (maps C symbols to symbol_)
%include "csym.inc"

; Phase 5: C symbol mapped via csym.inc
        extern  pcmcia_event_flag

segment _TEXT class=CODE

; Storage for previous vector
prev_isr_off   dw 0
prev_isr_seg   dw 0
installed_vec  db 0

; Install ISR on given IRQ (AL=irq 0..15)
        global pcmcia_isr_install
pcmcia_isr_install:
        push    ax
        push    bx
        push    dx
        push    ds
        push    es

        mov     bl, al          ; BL = IRQ

        ; Compute vector number
        cmp     bl, 8
        jb      short .master
        ; slave PIC vectors 70h..77h
        mov     al, bl
        add     al, 70h-8
        jmp     short .have_vec
.master:
        mov     al, bl
        add     al, 08h         ; 08h..0Fh
.have_vec:
        ; Save old vector (AH=35h)
        mov     ah, 35h
        int     21h             ; ES:BX old handler
        mov     [prev_isr_off], bx
        mov     [prev_isr_seg], es
        mov     [installed_vec], al

        ; Install our handler (AH=25h, DS:DX = handler)
        push    cs
        pop     ds
        mov     dx, pcmcia_irq_isr
        mov     ah, 25h
        int     21h

        pop     es
        pop     ds
        pop     dx
        pop     bx
        pop     ax
        ret

; ISR body: sets event flag and EOIs PIC
        global pcmcia_irq_isr
pcmcia_irq_isr:
        push    ax
        push    ds
        ; DS=_DATA
        mov     ax, _DATA
        mov     ds, ax
        mov     byte [pcmcia_event_flag], 1

        ; EOI PIC: slave before master if on IRQ>=8 (we don't know, EOI both safely)
        mov     al, 20h
        out     0A0h, al
        out     020h, al

        pop     ds
        pop     ax
        iret

; Uninstall ISR and restore previous vector
        global pcmcia_isr_uninstall
pcmcia_isr_uninstall:
        push    ax
        push    dx
        push    ds
        ; Load saved vector number
        mov     al, [installed_vec]
        or      al, al
        jz      short .done
        ; DS:DX = previous handler
        mov     dx, [prev_isr_off]
        mov     ax, [prev_isr_seg]
        or      ax, ax
        jz      short .done
        mov     ds, ax
        mov     ah, 25h
        ; AL already contains vector
        int     21h
.done:
        pop     ds
        pop     dx
        pop     ax
        ret

segment _DATA class=DATA
