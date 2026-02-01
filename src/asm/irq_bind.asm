; irq_bind.asm - Bind NIC IRQ/IO to ASM handler and install vector
;
; 3Com Packet Driver - Replaces src/c/irq_bind.c
; Last Updated: 2026-02-01 16:00:00 CET
;
; Original C: 24 LOC, 1,670 bytes compiled. ASM: ~50 bytes.

segment irq_bind_TEXT class=CODE

extern nic_irq_set_binding_
extern irq_handler_init_
extern irq_handler_uninstall_

; nic_info_t struct offsets (with -zp1 packing):
;   type      (enum/int):  offset 0,  2 bytes
;   ops       (far ptr):   offset 2,  4 bytes
;   index     (uint8_t):   offset 6,  1 byte
;   status    (uint32_t):  offset 7,  4 bytes
;   capabilities (uint32): offset 11, 4 bytes
;   io_base   (uint16_t):  offset 15, 2 bytes
;   io_range  (uint16_t):  offset 17, 2 bytes
;   mem_base  (uint32_t):  offset 19, 4 bytes
;   mem_size  (uint32_t):  offset 23, 4 bytes
;   irq       (uint8_t):   offset 27, 1 byte
NIC_IO_BASE equ 15
NIC_IRQ     equ 27
NIC_INDEX   equ 6

; void nic_irq_bind_and_install(const nic_info_t far *nic)
; Watcom register convention: far ptr in DX:AX (seg:offset)
global nic_irq_bind_and_install_
nic_irq_bind_and_install_:
    ; NULL check
    mov     bx, ax
    or      bx, dx
    jz      .null

    ; Load nic fields via ES:BX
    push    es
    mov     es, dx
    mov     bx, ax

    ; Load params for nic_irq_set_binding(io_base, irq, nic_index)
    ; Watcom: AX=io_base, DX=irq, BX=nic_index
    mov     ax, [es:bx+NIC_IO_BASE]
    movzx   dx, byte [es:bx+NIC_IRQ]
    movzx   cx, byte [es:bx+NIC_INDEX]    ; save index in CX temporarily
    pop     es

    mov     bx, cx                          ; BX = nic_index (3rd param)
    call far nic_irq_set_binding_
    call far irq_handler_init_
.null:
    retf

; void nic_irq_uninstall(void)
global nic_irq_uninstall_
nic_irq_uninstall_:
    call far irq_handler_uninstall_
    retf
