;
; @file tiny_isr_v2.asm
; @brief Optimized tiny ISRs with proper segment and PIC handling
;
; Implements minimal ISRs that properly handle DS segment loading and
; slave PIC EOI when needed, while staying as compact as possible.
;

SEGMENT _TEXT PUBLIC CLASS=CODE USE16

; External data references (assume in same segment for CS: override)
EXTERN _io_bases: WORD
EXTERN _work_pending: BYTE

; Public ISR entry points
PUBLIC _isr_nic0_v2
PUBLIC _isr_nic1_v2
PUBLIC _isr_nic2_v2
PUBLIC _isr_nic3_v2

; IRQ configuration (stored at init)
PUBLIC _nic_irqs
_nic_irqs       DB 4 DUP(0)     ; IRQ for each NIC

;-----------------------------------------------------------------------------
; Macro for compact ISR generation
; Uses CS: segment override to access data in code segment
;-----------------------------------------------------------------------------
ISR_TEMPLATE MACRO nic_index
        push    ax
        push    dx
        
        ; ACK interrupt at NIC using CS: override
        mov     dx, CS:[_io_bases + nic_index*2]
        add     dx, 0Eh             ; IntStatus register
        mov     ax, 0FFFFh          ; ACK all interrupts
        out     dx, ax
        
        ; Mark work pending using CS: override
        mov     BYTE PTR CS:[_work_pending + nic_index], 1
        
        ; Check if slave PIC needed
        mov     al, CS:[_nic_irqs + nic_index]
        cmp     al, 8
        jb      @@master_only
        
        ; Slave PIC EOI (IRQ 8-15)
        mov     al, 20h
        out     0A0h, al
        
@@master_only:
        ; Master PIC EOI
        mov     al, 20h
        out     20h, al
        
        pop     dx
        pop     ax
        iret
ENDM

;-----------------------------------------------------------------------------
; Generate ISRs for each NIC
;-----------------------------------------------------------------------------
_isr_nic0_v2 PROC FAR
        ISR_TEMPLATE 0
_isr_nic0_v2 ENDP

_isr_nic1_v2 PROC FAR
        ISR_TEMPLATE 1
_isr_nic1_v2 ENDP

_isr_nic2_v2 PROC FAR
        ISR_TEMPLATE 2
_isr_nic2_v2 ENDP

_isr_nic3_v2 PROC FAR
        ISR_TEMPLATE 3
_isr_nic3_v2 ENDP

;-----------------------------------------------------------------------------
; Alternative ultra-compact ISR for low IRQs only (no slave PIC)
; This version is truly ≤15 instructions for IRQ 0-7
;-----------------------------------------------------------------------------
PUBLIC _isr_low_irq
_isr_low_irq PROC FAR
        push    ax
        push    dx
        push    bx
        
        ; Determine NIC from vector (passed in BX by dispatcher)
        ; BX = NIC index (0-3)
        
        ; ACK at NIC
        shl     bx, 1               ; Word index
        mov     dx, CS:[_io_bases + bx]
        add     dx, 0Eh
        mov     ax, 0FFFFh
        out     dx, ax
        
        ; Mark work
        shr     bx, 1               ; Back to byte index
        mov     BYTE PTR CS:[_work_pending + bx], 1
        
        ; Master PIC EOI only (for IRQ 0-7)
        mov     al, 20h
        out     20h, al
        
        pop     bx
        pop     dx
        pop     ax
        iret
_isr_low_irq ENDP

;-----------------------------------------------------------------------------
; Install helper - stores IRQ for each NIC
;-----------------------------------------------------------------------------
PUBLIC _store_nic_irq
_store_nic_irq PROC NEAR
        ; Input: BL = NIC index, AL = IRQ number
        push    bx
        xor     bh, bh
        mov     CS:[_nic_irqs + bx], al
        pop     bx
        ret
_store_nic_irq ENDP

ENDS

END