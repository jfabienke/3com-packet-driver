; spurious_irq_handler.asm - Spurious IRQ 7 and IRQ 15 detection and handling
; Addresses GPT-5's production polish suggestion for spurious interrupt handling

section .text

global _install_spurious_irq_handlers
global _spurious_irq7_handler
global _spurious_irq15_handler
global _get_spurious_irq_stats

; Spurious IRQ statistics
section .data
spurious_irq7_count     dd 0
spurious_irq15_count    dd 0
total_irq7_count        dd 0
total_irq15_count       dd 0
original_irq7_vector    dd 0
original_irq15_vector   dd 0

section .text

; Install spurious IRQ handlers
; void install_spurious_irq_handlers(void)
_install_spurious_irq_handlers:
    push    bp
    mov     bp, sp
    push    ax
    push    bx
    push    dx
    push    es
    
    ; Save original IRQ 7 vector (INT 0x0F)
    push    ds
    mov     ax, 0x350F              ; Get interrupt vector
    int     0x21
    mov     [original_irq7_vector], bx    ; Offset
    mov     [original_irq7_vector+2], es  ; Segment
    pop     ds
    
    ; Install spurious IRQ 7 handler
    mov     dx, _spurious_irq7_handler
    mov     ax, 0x250F              ; Set interrupt vector
    int     0x21
    
    ; Save original IRQ 15 vector (INT 0x77)
    push    ds
    mov     ax, 0x3577              ; Get interrupt vector
    int     0x21
    mov     [original_irq15_vector], bx   ; Offset
    mov     [original_irq15_vector+2], es ; Segment
    pop     ds
    
    ; Install spurious IRQ 15 handler
    mov     dx, _spurious_irq15_handler
    mov     ax, 0x2577              ; Set interrupt vector
    int     0x21
    
    pop     es
    pop     dx
    pop     bx
    pop     ax
    pop     bp
    ret

; Spurious IRQ 7 handler (Master PIC)
_spurious_irq7_handler:
    push    ax
    push    dx
    
    ; Check if this is actually a spurious interrupt
    ; Read ISR (In-Service Register) from master PIC
    mov     al, 0x0B                ; OCW3: Read ISR
    out     0x20, al                ; Send to master PIC
    jmp     short $+2               ; I/O delay
    in      al, 0x20                ; Read ISR
    
    ; Check if IRQ 7 bit is set in ISR
    test    al, 0x80                ; Test bit 7 (IRQ 7)
    jnz     .real_irq7              ; Not spurious - handle normally
    
    ; This is a spurious interrupt
    inc     dword [spurious_irq7_count]
    
    ; Don't send EOI for spurious IRQ 7!
    ; This is critical - spurious IRQ 7 should not receive EOI
    
    pop     dx
    pop     ax
    iret
    
.real_irq7:
    ; Real IRQ 7 - increment counter and chain to original handler
    inc     dword [total_irq7_count]
    
    ; Restore registers
    pop     dx
    pop     ax
    
    ; Chain to original IRQ 7 handler
    pushf                           ; Simulate interrupt call
    call    far [original_irq7_vector]
    iret

; Spurious IRQ 15 handler (Slave PIC)  
_spurious_irq15_handler:
    push    ax
    push    dx
    
    ; Check if this is actually a spurious interrupt
    ; Read ISR from slave PIC
    mov     al, 0x0B                ; OCW3: Read ISR
    out     0xA0, al                ; Send to slave PIC
    jmp     short $+2               ; I/O delay
    in      al, 0xA0                ; Read ISR
    
    ; Check if IRQ 15 bit is set in ISR (bit 7 of slave PIC)
    test    al, 0x80                ; Test bit 7 (IRQ 15)
    jnz     .real_irq15             ; Not spurious - handle normally
    
    ; This is a spurious interrupt from slave PIC
    inc     dword [spurious_irq15_count]
    
    ; For spurious IRQ 15, we must still send EOI to master PIC
    ; but NOT to slave PIC
    mov     al, 0x20
    out     0x20, al                ; EOI to master only
    
    pop     dx
    pop     ax
    iret
    
.real_irq15:
    ; Real IRQ 15 - increment counter and chain to original handler
    inc     dword [total_irq15_count]
    
    ; Restore registers  
    pop     dx
    pop     ax
    
    ; Chain to original IRQ 15 handler
    pushf                           ; Simulate interrupt call
    call    far [original_irq15_vector]
    iret

; Enhanced spurious IRQ detection for any IRQ
; bool is_spurious_interrupt(uint8_t irq)
global _is_spurious_interrupt
_is_spurious_interrupt:
    push    bp
    mov     bp, sp
    push    dx
    
    mov     al, [bp+4]              ; Get IRQ number
    xor     ah, ah                  ; Clear high byte
    
    cmp     al, 8
    jb      .master_pic
    
    ; Slave PIC IRQ (8-15)
    sub     al, 8                   ; Convert to slave PIC bit
    mov     cl, al
    mov     al, 1
    shl     al, cl                  ; Create bit mask
    mov     dl, al                  ; Save mask
    
    ; Read slave PIC ISR
    mov     al, 0x0B
    out     0xA0, al
    jmp     short $+2
    in      al, 0xA0
    
    test    al, dl                  ; Test our IRQ bit
    jz      .is_spurious
    jmp     .not_spurious
    
.master_pic:
    ; Master PIC IRQ (0-7)
    mov     cl, al
    mov     al, 1
    shl     al, cl                  ; Create bit mask
    mov     dl, al                  ; Save mask
    
    ; Read master PIC ISR
    mov     al, 0x0B
    out     0x20, al
    jmp     short $+2
    in      al, 0x20
    
    test    al, dl                  ; Test our IRQ bit
    jz      .is_spurious
    
.not_spurious:
    mov     ax, 0                   ; Return false
    jmp     .done
    
.is_spurious:
    mov     ax, 1                   ; Return true
    
.done:
    pop     dx
    pop     bp
    ret

; Get spurious IRQ statistics
; void get_spurious_irq_stats(struct spurious_irq_stats *stats)
_get_spurious_irq_stats:
    push    bp
    mov     bp, sp
    push    si
    push    di
    push    ds
    push    es
    
    ; Get stats pointer
    mov     di, [bp+4]              ; stats parameter
    
    ; Copy statistics (assuming stats structure layout)
    mov     ax, ds
    mov     es, ax                  ; ES = DS for string operations
    
    mov     si, spurious_irq7_count
    mov     cx, 4                   ; 4 dwords to copy
    cld
    rep     movsd                   ; Copy to stats structure
    
    pop     es
    pop     ds
    pop     di
    pop     si
    pop     bp
    ret

; Restore original IRQ handlers (for cleanup)
; void restore_spurious_irq_handlers(void)
global _restore_spurious_irq_handlers
_restore_spurious_irq_handlers:
    push    bp
    mov     bp, sp
    push    ax
    push    dx
    push    ds
    
    ; Restore IRQ 7 vector
    lds     dx, [original_irq7_vector]
    mov     ax, 0x250F
    int     0x21
    
    ; Restore IRQ 15 vector  
    lds     dx, [original_irq15_vector]
    mov     ax, 0x2577
    int     0x21
    
    pop     ds
    pop     dx
    pop     ax
    pop     bp
    ret

; Advanced spurious interrupt handling for specific NICs
; Some NICs may generate spurious interrupts under certain conditions
; void handle_nic_spurious_interrupt(uint8_t device_id, uint16_t irq)
global _handle_nic_spurious_interrupt
_handle_nic_spurious_interrupt:
    push    bp
    mov     bp, sp
    push    ax
    push    bx
    push    dx
    
    mov     al, [bp+4]              ; device_id
    mov     bx, [bp+6]              ; irq
    
    ; Log the spurious interrupt (would call C logging function)
    ; For now, just increment a counter
    
    ; Check if we need to mask this IRQ temporarily
    ; This is a policy decision - for now, we don't mask
    
    ; Proper EOI handling based on IRQ number
    cmp     bl, 8
    jb      .master_eoi
    
    ; Slave PIC EOI for spurious interrupts from slave
    cmp     bl, 15                  ; Special case for spurious IRQ 15
    je      .spurious_irq15_eoi
    
    ; Normal dual EOI for real slave interrupts
    mov     al, 0x20
    out     0xA0, al                ; EOI slave
    out     0x20, al                ; EOI master
    jmp     .done
    
.spurious_irq15_eoi:
    ; Spurious IRQ 15 - only EOI master
    mov     al, 0x20
    out     0x20, al                ; EOI master only
    jmp     .done
    
.master_eoi:
    ; Check for spurious IRQ 7
    cmp     bl, 7
    je      .check_spurious_irq7
    
    ; Normal master PIC EOI
    mov     al, 0x20
    out     0x20, al
    jmp     .done
    
.check_spurious_irq7:
    ; For spurious IRQ 7, we should have already determined
    ; it's spurious before calling this function, so no EOI
    jmp     .done
    
.done:
    pop     dx
    pop     bx
    pop     ax
    pop     bp
    ret

; Helper function to read PIC ISR
; uint8_t read_pic_isr(bool slave)
global _read_pic_isr
_read_pic_isr:
    push    bp
    mov     bp, sp
    push    dx
    
    mov     al, [bp+4]              ; slave parameter
    test    al, al
    jz      .read_master
    
    ; Read slave PIC ISR
    mov     al, 0x0B
    out     0xA0, al
    jmp     short $+2
    in      al, 0xA0
    jmp     .done
    
.read_master:
    ; Read master PIC ISR
    mov     al, 0x0B
    out     0x20, al
    jmp     short $+2
    in      al, 0x20
    
.done:
    xor     ah, ah                  ; Clear high byte
    pop     dx
    pop     bp
    ret

; Helper function to read PIC IRR (Interrupt Request Register)  
; uint8_t read_pic_irr(bool slave)
global _read_pic_irr
_read_pic_irr:
    push    bp
    mov     bp, sp
    push    dx
    
    mov     al, [bp+4]              ; slave parameter
    test    al, al
    jz      .read_master
    
    ; Read slave PIC IRR
    mov     al, 0x0A                ; OCW3: Read IRR
    out     0xA0, al
    jmp     short $+2
    in      al, 0xA0
    jmp     .done
    
.read_master:
    ; Read master PIC IRR  
    mov     al, 0x0A                ; OCW3: Read IRR
    out     0x20, al
    jmp     short $+2
    in      al, 0x20
    
.done:
    xor     ah, ah                  ; Clear high byte
    pop     dx
    pop     bp
    ret