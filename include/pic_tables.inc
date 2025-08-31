; pic_tables.inc - Programmable Interrupt Controller lookup tables
; PIC programming tables for both master (8259A) and slave controllers
; Used for IRQ management and interrupt masking operations
;
; Generated for 3Com Packet Driver - Groups 6A & 6B implementation
; Supports: Master PIC (IRQ 0-7), Slave PIC (IRQ 8-15), Cascade mode

%ifndef PIC_TABLES_INC
%define PIC_TABLES_INC

; PIC Hardware Constants
PIC1_COMMAND        equ 0x20    ; Master PIC command port
PIC1_DATA          equ 0x21    ; Master PIC data port
PIC2_COMMAND        equ 0xA0    ; Slave PIC command port  
PIC2_DATA          equ 0xA1    ; Slave PIC data port

; PIC Command Codes
PIC_EOI            equ 0x20    ; End of Interrupt
PIC_READ_IRR       equ 0x0A    ; Read Interrupt Request Register
PIC_READ_ISR       equ 0x0B    ; Read In-Service Register

; ICW (Initialization Command Words) for PIC setup
ICW1_ICW4          equ 0x01    ; ICW4 needed
ICW1_SINGLE        equ 0x02    ; Single (cascade) mode
ICW1_INTERVAL4     equ 0x04    ; Call address interval 4 (8)
ICW1_LEVEL         equ 0x08    ; Level triggered (edge) mode
ICW1_INIT          equ 0x10    ; Initialization required

ICW4_8086          equ 0x01    ; 8086/88 (MCS-80/85) mode
ICW4_AUTO          equ 0x02    ; Auto (normal) EOI
ICW4_BUF_SLAVE     equ 0x08    ; Buffered mode/slave
ICW4_BUF_MASTER    equ 0x0C    ; Buffered mode/master
ICW4_SFNM          equ 0x10    ; Special fully nested (not)

; IRQ to PIC port mapping table
; Maps IRQ number (0-15) to corresponding PIC command/data ports
align 2
irq_to_pic_ports:
    ; IRQ 0-7: Master PIC
    dw PIC1_COMMAND, PIC1_DATA  ; IRQ 0
    dw PIC1_COMMAND, PIC1_DATA  ; IRQ 1
    dw PIC1_COMMAND, PIC1_DATA  ; IRQ 2
    dw PIC1_COMMAND, PIC1_DATA  ; IRQ 3
    dw PIC1_COMMAND, PIC1_DATA  ; IRQ 4
    dw PIC1_COMMAND, PIC1_DATA  ; IRQ 5
    dw PIC1_COMMAND, PIC1_DATA  ; IRQ 6
    dw PIC1_COMMAND, PIC1_DATA  ; IRQ 7
    ; IRQ 8-15: Slave PIC
    dw PIC2_COMMAND, PIC2_DATA  ; IRQ 8
    dw PIC2_COMMAND, PIC2_DATA  ; IRQ 9
    dw PIC2_COMMAND, PIC2_DATA  ; IRQ 10
    dw PIC2_COMMAND, PIC2_DATA  ; IRQ 11
    dw PIC2_COMMAND, PIC2_DATA  ; IRQ 12
    dw PIC2_COMMAND, PIC2_DATA  ; IRQ 13
    dw PIC2_COMMAND, PIC2_DATA  ; IRQ 14
    dw PIC2_COMMAND, PIC2_DATA  ; IRQ 15

; IRQ mask bit lookup table
; Maps IRQ number to corresponding bit mask for enable/disable
align 2
irq_mask_bits:
    db 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80  ; IRQ 0-7
    db 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80  ; IRQ 8-15

; PIC controller selection table (0=Master, 1=Slave)
align 2
irq_pic_controller:
    db 0, 0, 0, 0, 0, 0, 0, 0  ; IRQ 0-7: Master PIC
    db 1, 1, 1, 1, 1, 1, 1, 1  ; IRQ 8-15: Slave PIC

; Standard PIC initialization sequence
align 2
pic_init_sequence:
    ; Master PIC initialization
    db PIC1_COMMAND, ICW1_INIT | ICW1_ICW4     ; ICW1: Initialize + ICW4
    db PIC1_DATA, 0x08                         ; ICW2: Vector offset 0x08
    db PIC1_DATA, 0x04                         ; ICW3: IRQ2 has slave
    db PIC1_DATA, ICW4_8086                    ; ICW4: 8086 mode
    
    ; Slave PIC initialization  
    db PIC2_COMMAND, ICW1_INIT | ICW1_ICW4     ; ICW1: Initialize + ICW4
    db PIC2_DATA, 0x70                         ; ICW2: Vector offset 0x70
    db PIC2_DATA, 0x02                         ; ICW3: Cascade identity
    db PIC2_DATA, ICW4_8086                    ; ICW4: 8086 mode
    
    db 0xFF, 0xFF  ; End marker

; Network adapter IRQ priority table
; Lower values = higher priority for interrupt handling
align 2
nic_irq_priority:
    db 15, 14, 13, 12, 11, 10, 9, 8   ; IRQ 0-7 priorities
    db 7,  6,  5,  4,  3,  2,  1, 0   ; IRQ 8-15 priorities

; Common NIC IRQs availability mask
; 1 = IRQ commonly available for NICs, 0 = system reserved
COMMON_NIC_IRQS    equ 0xDEB8h    ; IRQ 3,4,5,7,9,10,11,12,14,15

; Utility macros for PIC operations
%macro PIC_DISABLE_IRQ 1
    ; Disable specific IRQ
    ; Input: %1 = IRQ number (0-15)
    push    ax
    push    dx
    mov     al, %1
    call    disable_irq_by_number
    pop     dx
    pop     ax
%endmacro

%macro PIC_ENABLE_IRQ 1
    ; Enable specific IRQ
    ; Input: %1 = IRQ number (0-15)  
    push    ax
    push    dx
    mov     al, %1
    call    enable_irq_by_number
    pop     dx
    pop     ax
%endmacro

%macro PIC_SEND_EOI 1
    ; Send End of Interrupt to appropriate PIC
    ; Input: %1 = IRQ number (0-15)
    push    ax
    push    dx
    mov     al, %1
    cmp     al, 8
    jl      %%master_eoi
    
    ; Slave EOI
    mov     al, PIC_EOI
    mov     dx, PIC2_COMMAND
    out     dx, al
    
    ; Also send to master (cascade)
%%master_eoi:
    mov     al, PIC_EOI
    mov     dx, PIC1_COMMAND
    out     dx, al
    pop     dx
    pop     ax
%endmacro

%macro PIC_READ_MASK 1
    ; Read current IRQ mask
    ; Input: %1 = PIC number (1=master, 2=slave)
    ; Output: AL = mask value
%if %1 == 1
    mov     dx, PIC1_DATA
%else
    mov     dx, PIC2_DATA
%endif
    in      al, dx
%endmacro

%macro PIC_WRITE_MASK 2
    ; Write IRQ mask
    ; Input: %1 = PIC number (1=master, 2=slave), %2 = mask value
%if %1 == 1
    mov     dx, PIC1_DATA
%else
    mov     dx, PIC2_DATA
%endif
    mov     al, %2
    out     dx, al
%endmacro

; Function prototypes (to be implemented in main code)
extern disable_irq_by_number    ; AL = IRQ number
extern enable_irq_by_number     ; AL = IRQ number
extern save_pic_state          ; Save current PIC masks
extern restore_pic_state       ; Restore saved PIC masks

%endif ; PIC_TABLES_INC