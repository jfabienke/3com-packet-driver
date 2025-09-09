; isr_tiny.asm - Ultra-minimal ISR for maximum performance
; This ISR is designed to be as small as possible (~8-10 instructions)
; and heavily optimized with Self-Modifying Code (SMC)

section .text

global _install_tiny_isr
global _tiny_isr_entry
global _tiny_isr_smc_patches

; SMC patch locations
SMC_IOBASE      equ 0xDEAD      ; Patched with device I/O base
SMC_STATUS_REG  equ 0xBEEF      ; Patched with status register offset  
SMC_WORK_FLAG   equ 0xCAFE      ; Patched with work flag address
SMC_EOI_PORT    equ 0xFEED      ; Patched with PIC EOI port

; Pipeline flush macro for safe SMC on 486+
%macro FLUSH_PIPELINE 0
    jmp     short $+2       ; Near jump to flush prefetch queue
    nop                     ; Landing pad
%endmacro

; Install tiny ISR for a device
; void install_tiny_isr(struct el3_device *dev)
_install_tiny_isr:
    push    bp
    mov     bp, sp
    push    si
    push    di
    push    es
    
    ; Get device pointer
    mov     si, [bp+4]      ; dev parameter
    
    ; Patch I/O base address
    mov     ax, [si+8]      ; dev->iobase
    mov     [smc_iobase_patch], ax
    mov     [smc_iobase_patch2], ax
    
    ; Patch status register (typically iobase + 0x0E for EL3)
    add     ax, 0x0E
    mov     [smc_status_patch], ax
    
    ; Patch work flag address
    lea     ax, [si+32]     ; &dev->work_pending
    mov     [smc_work_flag_patch], ax
    
    ; Determine PIC EOI port based on IRQ
    mov     al, [si+13]     ; dev->irq
    cmp     al, 8
    jb      .master_pic
    
    ; Slave PIC (IRQ 8-15)
    mov     ax, 0xA0
    mov     [smc_eoi_patch], ax
    jmp     .flush
    
.master_pic:
    ; Master PIC (IRQ 0-7)
    mov     ax, 0x20
    mov     [smc_eoi_patch], ax
    
.flush:
    ; Flush pipeline after patching
    FLUSH_PIPELINE
    
    pop     es
    pop     di
    pop     si
    pop     bp
    ret

; Ultra-minimal ISR entry point
; This is the actual interrupt handler that gets called
; Target: 8-10 instructions maximum
_tiny_isr_entry:
    ; Save minimal registers (AX, DX only)
    push    ax
    push    dx
    
    ; Read interrupt status (SMC-patched address)
    mov     dx, SMC_IOBASE          ; Patched with actual I/O base
smc_iobase_patch equ $-2
    add     dx, 0x0E                ; Status register offset
smc_status_patch equ $-2
    in      ax, dx                  ; Read status
    
    ; ACK interrupt by writing status back (EL3 behavior)
    out     dx, ax                  ; Write clears interrupt
    
    ; Set work pending flag (SMC-patched address)
    mov     dx, SMC_WORK_FLAG       ; Patched with flag address
smc_work_flag_patch equ $-2
    mov     byte [dx], 1            ; Mark work pending
    
    ; EOI to PIC (SMC-patched port)
    mov     al, 0x20
    mov     dx, SMC_EOI_PORT        ; Patched with correct PIC port
smc_eoi_patch equ $-2
    out     dx, al
    
    ; Restore registers and return
    pop     dx
    pop     ax
    iret

; Advanced tiny ISR for bus master devices (3C515)
; Slightly larger but optimized for DMA devices
_tiny_isr_busmaster:
    push    ax
    push    dx
    
    ; Read DMA status
    mov     dx, SMC_IOBASE
smc_iobase_patch2 equ $-2
    add     dx, 0x0C                ; DMA status register
    in      ax, dx
    
    ; Check if our interrupt
    test    ax, 0x8000              ; DMA interrupt bit
    jz      .not_ours
    
    ; ACK DMA interrupt
    out     dx, ax                  ; Clear by writing back
    
    ; Set work pending
    mov     dx, SMC_WORK_FLAG
smc_work_flag_patch2 equ $-2
    mov     byte [dx], 1
    
    ; EOI to PIC
    mov     al, 0x20
    mov     dx, SMC_EOI_PORT
smc_eoi_patch2 equ $-2
    out     dx, al
    
.not_ours:
    pop     dx
    pop     ax
    iret

; ISR statistics and debugging support
section .data

; ISR performance counters
isr_call_count      dd 0        ; Total ISR calls
isr_work_generated  dd 0        ; Work items generated
isr_spurious        dd 0        ; Spurious interrupts
isr_max_cycles      dd 0        ; Maximum ISR cycles

; SMC patch table for debugging
_tiny_isr_smc_patches:
    dw smc_iobase_patch
    dw smc_status_patch
    dw smc_work_flag_patch
    dw smc_eoi_patch
    dw smc_iobase_patch2
    dw smc_work_flag_patch2
    dw smc_eoi_patch2
    dw 0                        ; Terminator

section .text

; Performance measurement wrapper (debug only)
; Measures ISR execution time
_tiny_isr_measured:
    push    ax
    push    dx
    push    cx
    
    ; Read TSC if available (486+)
    rdtsc                           ; EDX:EAX = timestamp
    push    eax                     ; Save start time
    push    edx
    
    ; Call actual ISR
    call    _tiny_isr_entry
    
    ; Read TSC again
    rdtsc                           ; EDX:EAX = end time
    pop     edx                     ; Restore start time high
    pop     ecx                     ; Restore start time low
    
    ; Calculate cycles (simplified)
    sub     eax, ecx                ; End - start (low part)
    
    ; Update statistics
    inc     dword [isr_call_count]
    cmp     eax, [isr_max_cycles]
    jbe     .not_max
    mov     [isr_max_cycles], eax
    
.not_max:
    pop     cx
    pop     dx
    pop     ax
    ret

; ISR installation helper
; Sets up interrupt vector and enables IRQ
; void install_isr_vector(uint8_t irq, void far *handler)
_install_isr_vector:
    push    bp
    mov     bp, sp
    push    es
    push    bx
    push    dx
    
    ; Get parameters
    mov     al, [bp+4]              ; irq
    mov     dx, [bp+6]              ; handler offset
    mov     bx, [bp+8]              ; handler segment
    
    ; Calculate interrupt vector
    add     al, 8                   ; IRQ to interrupt vector
    cmp     al, 16
    jb      .master
    add     al, 0x68                ; Slave PIC offset
    jmp     .set_vector
    
.master:
    add     al, 0x08                ; Master PIC offset
    
.set_vector:
    ; Set interrupt vector
    mov     ah, 0x25                ; DOS set vector
    push    ds
    mov     ds, bx                  ; Handler segment
    int     0x21
    pop     ds
    
    ; Enable IRQ in PIC
    mov     bl, al
    sub     bl, 8                   ; Back to IRQ number
    cmp     bl, 8
    jb      .enable_master
    
    ; Enable slave PIC
    sub     bl, 8
    mov     cl, bl
    mov     al, 1
    shl     al, cl                  ; IRQ bit mask
    not     al                      ; Invert for enable
    
    in      al, 0xA1                ; Read slave mask
    and     al, bl                  ; Clear IRQ bit
    out     0xA1, al                ; Write back
    
    ; Also enable cascade on master
    in      al, 0x21
    and     al, 0xFB                ; Enable IRQ 2 (cascade)
    out     0x21, al
    jmp     .done
    
.enable_master:
    ; Enable master PIC
    mov     cl, bl
    mov     al, 1
    shl     al, cl                  ; IRQ bit mask
    not     al                      ; Invert for enable
    
    in      al, 0x21                ; Read master mask
    and     al, bl                  ; Clear IRQ bit
    out     0x21, al                ; Write back
    
.done:
    pop     dx
    pop     bx
    pop     es
    pop     bp
    ret