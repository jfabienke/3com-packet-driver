;
; @file tiny_isr.asm  
; @brief Tiny interrupt service routines (15 instructions max)
;
; Implements minimal ISRs that only ACK interrupts and mark work pending.
; Bottom-half processing happens in the main loop for better cache locality
; and reduced interrupt latency. Based on DRIVER_TUNING.md specifications.
;

SEGMENT _TEXT PUBLIC CLASS=CODE USE16

; External data references
EXTERN _io_bases: WORD
EXTERN _work_pending: BYTE

; Public ISR entry points
PUBLIC _isr_nic0
PUBLIC _isr_nic1
PUBLIC _isr_nic2
PUBLIC _isr_nic3
PUBLIC _install_tiny_isrs
PUBLIC _uninstall_tiny_isrs

; Interrupt vectors (configurable)
IRQ_BASE        EQU 08h         ; Hardware IRQ base (IRQ0-7)
IRQ_BASE_HIGH   EQU 70h         ; High IRQ base (IRQ8-15)

;-----------------------------------------------------------------------------
; Tiny ISR for NIC 0 (≤15 instructions)
;-----------------------------------------------------------------------------
_isr_nic0 PROC FAR
        push    ax
        push    dx
        push    ds
        
        ; Load our data segment
        mov     ax, SEG _io_bases
        mov     ds, ax
        
        ; ACK interrupt at NIC
        mov     dx, [_io_bases]     ; Get I/O base for NIC 0
        add     dx, 0Eh             ; IntStatus register
        mov     ax, 0FFFFh          ; ACK all interrupts
        out     dx, ax
        
        ; Mark work pending
        mov     BYTE PTR [_work_pending], 1
        
        ; EOI to PIC (check if slave needed)
        mov     al, [_nic0_irq]     ; Get IRQ number
        cmp     al, 8
        jb      .master_only
        
        ; Slave PIC EOI first (IRQ 8-15)
        mov     al, 20h
        out     0A0h, al            ; Slave PIC EOI
        
.master_only:
        mov     al, 20h
        out     20h, al             ; Master PIC EOI
        
        pop     ds
        pop     dx
        pop     ax
        iret
_isr_nic0 ENDP

;-----------------------------------------------------------------------------
; Tiny ISR for NIC 1 (≤15 instructions)
;-----------------------------------------------------------------------------
_isr_nic1 PROC FAR
        push    ax
        push    dx
        
        ; ACK interrupt at NIC
        mov     dx, [_io_bases + 2] ; Get I/O base for NIC 1
        add     dx, 0Eh
        mov     ax, 0FFFFh
        out     dx, ax
        
        ; Mark work pending
        mov     BYTE PTR [_work_pending + 1], 1
        
        ; EOI to PIC
        mov     al, 20h
        out     20h, al
        
        pop     dx
        pop     ax
        iret
_isr_nic1 ENDP

;-----------------------------------------------------------------------------
; Tiny ISR for NIC 2 (≤15 instructions)
;-----------------------------------------------------------------------------
_isr_nic2 PROC FAR
        push    ax
        push    dx
        
        ; ACK interrupt at NIC
        mov     dx, [_io_bases + 4] ; Get I/O base for NIC 2
        add     dx, 0Eh
        mov     ax, 0FFFFh
        out     dx, ax
        
        ; Mark work pending
        mov     BYTE PTR [_work_pending + 2], 1
        
        ; EOI to PIC
        mov     al, 20h
        out     20h, al
        
        pop     dx
        pop     ax
        iret
_isr_nic2 ENDP

;-----------------------------------------------------------------------------
; Tiny ISR for NIC 3 (≤15 instructions)
;-----------------------------------------------------------------------------
_isr_nic3 PROC FAR
        push    ax
        push    dx
        
        ; ACK interrupt at NIC
        mov     dx, [_io_bases + 6] ; Get I/O base for NIC 3
        add     dx, 0Eh
        mov     ax, 0FFFFh
        out     dx, ax
        
        ; Mark work pending
        mov     BYTE PTR [_work_pending + 3], 1
        
        ; EOI to PIC
        mov     al, 20h
        out     20h, al
        
        pop     dx
        pop     ax
        iret
_isr_nic3 ENDP

;-----------------------------------------------------------------------------
; Shared ISR for high IRQs (8-15) - adds slave PIC EOI
;-----------------------------------------------------------------------------
_isr_high_irq PROC FAR
        push    ax
        push    dx
        push    bx
        
        ; Determine which NIC based on IRQ (stored in BX by dispatcher)
        ; This would be set up by the IRQ dispatcher
        
        ; ACK interrupt at NIC
        mov     bx, ax              ; IRQ number passed in AX
        sub     bx, 8               ; Convert to NIC index
        shl     bx, 1               ; Word offset
        mov     dx, [_io_bases + bx]
        add     dx, 0Eh
        mov     ax, 0FFFFh
        out     dx, ax
        
        ; Mark work pending
        shr     bx, 1               ; Back to byte offset
        mov     BYTE PTR [_work_pending + bx], 1
        
        ; EOI to slave PIC first
        mov     al, 20h
        out     0A0h, al            ; Slave PIC EOI
        
        ; EOI to master PIC
        out     20h, al             ; Master PIC EOI
        
        pop     bx
        pop     dx
        pop     ax
        iret
_isr_high_irq ENDP

;-----------------------------------------------------------------------------
; Install tiny ISRs for configured NICs
; Input: CX = number of NICs, DS:SI -> IRQ configuration array
;-----------------------------------------------------------------------------
_install_tiny_isrs PROC NEAR
        push    bp
        mov     bp, sp
        push    ax
        push    bx
        push    cx
        push    dx
        push    si
        push    di
        push    es
        
        ; Save old interrupt vectors
        xor     di, di              ; NIC index
        
.install_loop:
        test    cx, cx
        jz      .done
        
        ; Get IRQ for this NIC
        mov     al, [si]            ; IRQ number
        inc     si
        
        ; Calculate interrupt vector
        cmp     al, 8
        jb      .low_irq
        
.high_irq:
        ; IRQ 8-15 uses vectors 70h-77h
        add     al, 70h - 8
        jmp     .set_vector
        
.low_irq:
        ; IRQ 0-7 uses vectors 08h-0Fh
        add     al, 08h
        
.set_vector:
        ; Save old vector
        push    ax
        mov     ah, 35h             ; Get interrupt vector
        int     21h
        mov     [old_vectors + di*4], bx
        mov     [old_vectors + di*4 + 2], es
        pop     ax
        
        ; Set new vector based on NIC index
        push    ax
        mov     ah, 25h             ; Set interrupt vector
        push    ds
        push    cs
        pop     ds
        
        ; Select appropriate ISR based on NIC index
        cmp     di, 0
        je      .set_nic0
        cmp     di, 1
        je      .set_nic1
        cmp     di, 2
        je      .set_nic2
        
.set_nic3:
        mov     dx, OFFSET _isr_nic3
        jmp     .do_set
.set_nic2:
        mov     dx, OFFSET _isr_nic2
        jmp     .do_set
.set_nic1:
        mov     dx, OFFSET _isr_nic1
        jmp     .do_set
.set_nic0:
        mov     dx, OFFSET _isr_nic0
        
.do_set:
        int     21h
        pop     ds
        pop     ax
        
        ; Enable IRQ in PIC
        push    ax
        cmp     al, 10h             ; Check if high IRQ
        jb      .enable_low
        
.enable_high:
        ; Enable in slave PIC
        sub     al, 70h             ; Convert to IRQ 8-15
        mov     cl, al
        in      al, 0A1h            ; Read slave mask
        mov     ah, 1
        shl     ah, cl
        not     ah
        and     al, ah              ; Clear bit to enable
        out     0A1h, al
        jmp     .next_nic
        
.enable_low:
        ; Enable in master PIC
        sub     al, 08h             ; Convert to IRQ 0-7
        mov     cl, al
        in      al, 21h             ; Read master mask
        mov     ah, 1
        shl     ah, cl
        not     ah
        and     al, ah              ; Clear bit to enable
        out     21h, al
        
.next_nic:
        pop     ax
        inc     di
        dec     cx
        jmp     .install_loop
        
.done:
        pop     es
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        pop     bp
        ret
_install_tiny_isrs ENDP

;-----------------------------------------------------------------------------
; Uninstall tiny ISRs and restore original vectors
; Input: CX = number of NICs to restore
;-----------------------------------------------------------------------------
_uninstall_tiny_isrs PROC NEAR
        push    bp
        mov     bp, sp
        push    ax
        push    bx
        push    cx
        push    dx
        push    di
        push    ds
        
        xor     di, di              ; NIC index
        
.restore_loop:
        test    cx, cx
        jz      .done
        
        ; Restore old vector
        push    cx
        mov     ax, 2500h + 08h     ; Base vector
        add     al, [saved_irqs + di]
        
        lds     dx, [old_vectors + di*4]
        int     21h
        
        pop     cx
        inc     di
        dec     cx
        jmp     .restore_loop
        
.done:
        pop     ds
        pop     di
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        pop     bp
        ret
_uninstall_tiny_isrs ENDP

;-----------------------------------------------------------------------------
; Data section
;-----------------------------------------------------------------------------
; Saved interrupt vectors (4 NICs max)
old_vectors     DD 4 DUP(0)

; Saved IRQ numbers for each NIC
saved_irqs      DB 4 DUP(0)

; IRQ numbers for each NIC (for EOI determination)
PUBLIC _nic0_irq, _nic1_irq, _nic2_irq, _nic3_irq
_nic0_irq       DB 0
_nic1_irq       DB 0
_nic2_irq       DB 0
_nic3_irq       DB 0

;-----------------------------------------------------------------------------
; Statistics tracking (updated from bottom half)
;-----------------------------------------------------------------------------
PUBLIC _isr_stats
_isr_stats:
        isr_count       DD 4 DUP(0)     ; ISR call counts
        work_done       DD 4 DUP(0)     ; Work items processed
        max_latency     DW 4 DUP(0)     ; Max ticks between ISR and processing

ENDS

END