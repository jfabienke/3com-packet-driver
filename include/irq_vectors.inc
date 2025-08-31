; irq_vectors.inc - IRQ to interrupt vector mapping tables  
; Maps hardware IRQ lines to DOS interrupt vectors for packet driver
; Supports both standard and remapped interrupt configurations
;
; Generated for 3Com Packet Driver - Groups 6A & 6B implementation
; Compatible with: DOS 2.0+, standard PIC configuration, BIOS remapping

%ifndef IRQ_VECTORS_INC
%define IRQ_VECTORS_INC

; Standard DOS interrupt vector base addresses
DOS_IRQ_BASE       equ 0x08    ; IRQ 0-7 -> INT 08h-0Fh
DOS_IRQ_SLAVE_BASE equ 0x70    ; IRQ 8-15 -> INT 70h-77h

; Protected mode interrupt bases (for future expansion)
PM_IRQ_BASE        equ 0x20    ; Protected mode IRQ base
PM_IRQ_SLAVE_BASE  equ 0x28    ; Protected mode slave base

; IRQ to interrupt vector mapping table
; Maps IRQ number (0-15) to corresponding interrupt vector
align 2
irq_to_vector_table:
    ; Hardware IRQ 0-7 -> INT 08h-0Fh
    db 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F
    ; Hardware IRQ 8-15 -> INT 70h-77h  
    db 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77

; Reverse mapping: interrupt vector to IRQ number
; Supports both standard and common remapped configurations
align 2
vector_to_irq_table:
    ; Standard mapping (INT 08h-0Fh -> IRQ 0-7)
    times 8 db 0xFF         ; INT 00h-07h: Invalid
    db 0, 1, 2, 3, 4, 5, 6, 7   ; INT 08h-0Fh -> IRQ 0-7
    times 96 db 0xFF        ; INT 10h-6Fh: Invalid  
    db 8, 9, 10, 11, 12, 13, 14, 15  ; INT 70h-77h -> IRQ 8-15
    times 136 db 0xFF       ; INT 78h-FFh: Invalid

; Network adapter common IRQ assignments
; Priority ordered list of IRQs commonly used by NICs
align 2
nic_common_irqs:
    db 5, 10, 11, 9, 7, 3, 4, 12, 15, 14  ; Preferred order
    db 0xFF ; End marker

; IRQ availability flags for runtime detection
; 1 = Available for NIC use, 0 = System reserved/in use
align 2
irq_availability_mask:
    dw 0xDEB8h  ; IRQ 3,4,5,7,9,10,11,12,14,15 potentially available

; IRQ conflict detection table
; Bitmask of IRQs that commonly conflict with each other
align 2
irq_conflict_table:
    dw 0x0001   ; IRQ 0: Timer (never available)
    dw 0x0002   ; IRQ 1: Keyboard (never available) 
    dw 0x0004   ; IRQ 2: Cascade (never available)
    dw 0x0088   ; IRQ 3: Conflicts with COM2/4
    dw 0x0110   ; IRQ 4: Conflicts with COM1/3
    dw 0x0220   ; IRQ 5: May conflict with LPT2/sound
    dw 0x0040   ; IRQ 6: Floppy (usually not available)
    dw 0x0480   ; IRQ 7: Conflicts with LPT1
    dw 0x0100   ; IRQ 8: RTC (never available)
    dw 0x0A00   ; IRQ 9: May conflict with VGA
    dw 0x0C00   ; IRQ 10: Often available
    dw 0x1800   ; IRQ 11: Often available
    dw 0x3000   ; IRQ 12: May conflict with PS/2 mouse
    dw 0x2000   ; IRQ 13: Math coprocessor (usually not available)
    dw 0x4000   ; IRQ 14: Primary IDE (may conflict)
    dw 0x8000   ; IRQ 15: Secondary IDE (may conflict)

; Interrupt vector table offsets (IVT at 0000:0000)
; Each vector is 4 bytes (segment:offset)
%define IVT_ENTRY_SIZE 4

%macro GET_IVT_OFFSET 1
    ; Calculate IVT offset for interrupt vector
    ; Input: %1 = interrupt number
    ; Output: offset in IVT
    (%1 * IVT_ENTRY_SIZE)
%endmacro

; Utility macros for IRQ/vector operations
%macro IRQ_TO_VECTOR 1
    ; Convert IRQ number to interrupt vector
    ; Input: %1 = IRQ number (0-15)
    ; Output: AL = interrupt vector
    push    bx
    mov     bx, irq_to_vector_table
    mov     al, %1
    add     bx, ax
    mov     al, [bx]
    pop     bx
%endmacro

%macro VECTOR_TO_IRQ 1
    ; Convert interrupt vector to IRQ number  
    ; Input: %1 = interrupt vector
    ; Output: AL = IRQ number (0xFF if invalid)
    push    bx
    mov     bx, vector_to_irq_table
    mov     al, %1
    add     bx, ax
    mov     al, [bx]
    pop     bx
%endmacro

%macro HOOK_IRQ_VECTOR 2
    ; Hook IRQ interrupt vector
    ; Input: %1 = IRQ number, %2 = handler address
    push    ax
    push    bx
    push    dx
    push    es
    
    ; Get interrupt vector number
    mov     bx, irq_to_vector_table
    mov     al, %1
    xor     ah, ah
    add     bx, ax
    mov     al, [bx]
    
    ; Hook the vector
    mov     dx, %2
    mov     ah, 0x25    ; DOS set vector function
    int     0x21
    
    pop     es
    pop     dx
    pop     bx
    pop     ax
%endmacro

%macro UNHOOK_IRQ_VECTOR 2
    ; Restore original IRQ interrupt vector
    ; Input: %1 = IRQ number, %2 = original handler address
    push    ax
    push    dx
    
    ; Get interrupt vector number
    mov     bx, irq_to_vector_table
    mov     al, %1
    xor     ah, ah
    add     bx, ax
    mov     al, [bx]
    
    ; Restore the vector
    mov     dx, %2
    mov     ah, 0x25    ; DOS set vector function
    int     0x21
    
    pop     dx
    pop     ax
%endmacro

%macro GET_IRQ_VECTOR 1
    ; Get current IRQ interrupt vector
    ; Input: %1 = IRQ number
    ; Output: ES:BX = handler address
    push    ax
    
    ; Get interrupt vector number
    push    bx
    mov     bx, irq_to_vector_table
    mov     al, %1
    xor     ah, ah
    add     bx, ax
    mov     al, [bx]
    pop     bx
    
    ; Get the vector
    mov     ah, 0x35    ; DOS get vector function
    int     0x21
    ; ES:BX now contains handler address
    
    pop     ax
%endmacro

; NIC-specific IRQ management constants
MAX_NIC_IRQS       equ 2       ; Maximum NICs supported simultaneously
DEFAULT_NIC1_IRQ   equ 10      ; Default IRQ for first NIC
DEFAULT_NIC2_IRQ   equ 11      ; Default IRQ for second NIC

; IRQ sharing support flags
IRQ_EXCLUSIVE      equ 0x00    ; IRQ cannot be shared
IRQ_SHAREABLE      equ 0x01    ; IRQ can be shared
IRQ_LEVEL_TRIGGER  equ 0x02    ; Level-triggered interrupt
IRQ_EDGE_TRIGGER   equ 0x00    ; Edge-triggered interrupt

; Function prototypes (to be implemented in main code)
extern detect_irq_availability    ; Returns available IRQ mask in AX
extern check_irq_conflict        ; AL=IRQ, returns conflict mask in AX
extern find_free_irq             ; Returns free IRQ in AL (0xFF if none)
extern validate_irq_assignment   ; AL=IRQ, returns 1 if valid, 0 if not

%endif ; IRQ_VECTORS_INC