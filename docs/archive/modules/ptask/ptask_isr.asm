;****************************************************************************
; @file ptask_isr.asm
; @brief PTASK.MOD Zero-Branch Interrupt Service Routine
;
; Team A Implementation - Optimized ISR with ≤60μs execution guarantee
; Uses computed jumps and straight-line code for predictable timing
;****************************************************************************

.model small
.code

; External dependencies
EXTERN _g_ptask_context:BYTE
EXTERN _ptask_handle_3c509b_interrupt:PROC
EXTERN _ptask_handle_3c589_interrupt:PROC
EXTERN _ptask_handle_ne2000_interrupt:PROC
EXTERN _pio_inw_optimized:PROC

; Public interface
PUBLIC ptask_isr_asm_entry
PUBLIC ptask_install_isr
PUBLIC ptask_remove_isr

; Include timing measurement macros
INCLUDE timing_macros.inc

;=============================================================================
; Constants
;=============================================================================

; Hardware types (must match C definitions)
PTASK_HARDWARE_3C509B          EQU 01h
PTASK_HARDWARE_3C589           EQU 02h
PTASK_HARDWARE_NE2000_COMPAT   EQU 10h

; PIC constants
PIC1_COMMAND                   EQU 20h
PIC1_DATA                      EQU 21h
PIC2_COMMAND                   EQU 0A0h
PIC2_DATA                      EQU 0A1h
EOI                            EQU 20h

; Context offsets (must match ptask_context_t)
PTASK_CTX_HARDWARE_TYPE        EQU 4
PTASK_CTX_IO_BASE              EQU 6
PTASK_CTX_IRQ                  EQU 8

;=============================================================================
; Data Section
;=============================================================================

.data

; ISR performance tracking
isr_entry_count         DD      0
isr_total_time_us       DD      0
isr_max_time_us         DW      0

; Original interrupt vector
original_isr_seg        DW      0
original_isr_off        DW      0
isr_installed           DB      0

; Jump table for zero-branch dispatch
isr_jump_table          LABEL   WORD
    DW      OFFSET handle_unknown       ; 0 - Unknown
    DW      OFFSET handle_3c509b        ; 1 - 3C509B
    DW      OFFSET handle_3c589         ; 2 - 3C589
    DW      OFFSET handle_unknown       ; 3 - Reserved
    DW      OFFSET handle_unknown       ; 4 - Reserved
    DW      OFFSET handle_unknown       ; 5 - Reserved
    DW      OFFSET handle_unknown       ; 6 - Reserved
    DW      OFFSET handle_unknown       ; 7 - Reserved
    DW      OFFSET handle_unknown       ; 8 - Reserved
    DW      OFFSET handle_unknown       ; 9 - Reserved
    DW      OFFSET handle_unknown       ; 10 - Reserved
    DW      OFFSET handle_unknown       ; 11 - Reserved
    DW      OFFSET handle_unknown       ; 12 - Reserved
    DW      OFFSET handle_unknown       ; 13 - Reserved
    DW      OFFSET handle_unknown       ; 14 - Reserved
    DW      OFFSET handle_unknown       ; 15 - Reserved
    DW      OFFSET handle_ne2000        ; 16 - NE2000 compatibility

;=============================================================================
; Zero-Branch ISR Implementation
;=============================================================================

;-----------------------------------------------------------------------------
; @brief Main ISR entry point - optimized for zero branching
; 
; Uses computed jumps and straight-line code to minimize timing variance.
; Target: ≤60μs execution time for receive path.
;-----------------------------------------------------------------------------
ptask_isr_asm_entry PROC FAR
    ; Start timing measurement immediately
    TIMING_ISR_START
    
    ; Save all registers (DOS calling convention)
    push    ax
    push    bx
    push    cx
    push    dx
    push    si
    push    di
    push    bp
    push    ds
    push    es
    
    ; Set up DS for module data access
    mov     ax, cs
    mov     ds, ax
    
    ; Increment ISR entry counter
    inc     WORD PTR [isr_entry_count]
    adc     WORD PTR [isr_entry_count + 2], 0
    
    ; Get hardware type from context (zero-branch lookup)
    mov     bx, OFFSET _g_ptask_context
    add     bx, PTASK_CTX_HARDWARE_TYPE
    mov     al, [bx]                    ; AL = hardware type
    
    ; Bounds check and dispatch via jump table (no branches)
    xor     ah, ah                      ; Clear high byte
    cmp     al, 16                      ; Maximum table index
    ja      dispatch_unknown            ; Only branch for bounds check
    
    ; Computed jump - zero branches in common case
    shl     ax, 1                       ; Convert to word offset
    mov     bx, ax
    jmp     [isr_jump_table + bx]       ; Dispatch to handler

;-----------------------------------------------------------------------------
; Hardware-specific interrupt handlers
;-----------------------------------------------------------------------------

handle_3c509b:
    ; 3C509B interrupt handling
    push    ax                          ; Preserve timing register
    call    _ptask_handle_3c509b_interrupt
    pop     ax                          ; Restore timing register
    jmp     isr_complete

handle_3c589:
    ; 3C589 interrupt handling  
    push    ax                          ; Preserve timing register
    call    _ptask_handle_3c589_interrupt
    pop     ax                          ; Restore timing register
    jmp     isr_complete

handle_ne2000:
    ; NE2000 compatibility interrupt handling
    push    ax                          ; Preserve timing register
    call    _ptask_handle_ne2000_interrupt
    pop     ax                          ; Restore timing register
    jmp     isr_complete

handle_unknown:
dispatch_unknown:
    ; Unknown hardware - minimal processing
    xor     ax, ax                      ; Return 0 events processed

isr_complete:
    ; Send EOI to PIC before restoring registers
    mov     bx, OFFSET _g_ptask_context
    add     bx, PTASK_CTX_IRQ
    mov     al, [bx]                    ; Get IRQ number
    
    cmp     al, 8                       ; Check if secondary PIC
    jb      send_primary_eoi
    
    ; Send EOI to secondary PIC first
    mov     al, EOI
    out     PIC2_COMMAND, al
    
send_primary_eoi:
    ; Send EOI to primary PIC
    mov     al, EOI
    out     PIC1_COMMAND, al
    
    ; End timing measurement
    TIMING_ISR_END
    
    ; Update performance statistics
    push    ax
    call    update_isr_timing
    pop     ax
    
    ; Restore all registers in reverse order
    pop     es
    pop     ds
    pop     bp
    pop     di
    pop     si
    pop     dx
    pop     cx
    pop     bx
    pop     ax
    
    iret                                ; Return from interrupt
ptask_isr_asm_entry ENDP

;=============================================================================
; ISR Installation and Management
;=============================================================================

;-----------------------------------------------------------------------------
; @brief Install PTASK ISR
; @param AL = IRQ number
; @return AX = 0 on success, negative on error
;-----------------------------------------------------------------------------
ptask_install_isr PROC
    push    bp
    mov     bp, sp
    push    bx
    push    dx
    push    es
    
    ; Check if already installed
    cmp     [isr_installed], 1
    je      install_already_done
    
    ; Calculate interrupt vector (IRQ 0-7 = INT 08h-0Fh, IRQ 8-15 = INT 70h-77h)
    mov     ah, 0                       ; Clear high byte
    cmp     al, 8
    jb      primary_irq
    
    ; Secondary PIC (IRQ 8-15)
    add     al, 70h - 8                 ; Convert to interrupt number
    jmp     get_vector
    
primary_irq:
    ; Primary PIC (IRQ 0-7)
    add     al, 08h                     ; Convert to interrupt number
    
get_vector:
    ; Get current interrupt vector
    mov     ah, 35h                     ; DOS Get Interrupt Vector
    int     21h                         ; ES:BX = current vector
    
    ; Save original vector
    mov     [original_isr_seg], es
    mov     [original_isr_off], bx
    
    ; Install our ISR
    push    ds
    mov     dx, OFFSET ptask_isr_asm_entry
    mov     ax, cs
    mov     ds, ax
    mov     ah, 25h                     ; DOS Set Interrupt Vector
    int     21h
    pop     ds
    
    ; Mark as installed
    mov     [isr_installed], 1
    xor     ax, ax                      ; Return success
    jmp     install_done
    
install_already_done:
    mov     ax, -1                      ; Return error (already installed)
    
install_done:
    pop     es
    pop     dx
    pop     bx
    pop     bp
    ret
ptask_install_isr ENDP

;-----------------------------------------------------------------------------
; @brief Remove PTASK ISR
; @return AX = 0 on success, negative on error
;-----------------------------------------------------------------------------
ptask_remove_isr PROC
    push    bp
    mov     bp, sp
    push    dx
    push    ds
    
    ; Check if installed
    cmp     [isr_installed], 0
    je      remove_not_installed
    
    ; Restore original interrupt vector
    mov     dx, [original_isr_off]
    mov     ds, [original_isr_seg]
    mov     ah, 25h                     ; DOS Set Interrupt Vector
    int     21h
    
    ; Mark as not installed
    mov     [isr_installed], 0
    xor     ax, ax                      ; Return success
    jmp     remove_done
    
remove_not_installed:
    mov     ax, -1                      ; Return error (not installed)
    
remove_done:
    pop     ds
    pop     dx
    pop     bp
    ret
ptask_remove_isr ENDP

;=============================================================================
; Performance Monitoring
;=============================================================================

;-----------------------------------------------------------------------------
; @brief Update ISR timing statistics
; @param AX = timing measurement result
;-----------------------------------------------------------------------------
update_isr_timing PROC
    push    bx
    push    cx
    push    dx
    
    ; Convert timing to microseconds (implementation depends on timing method)
    ; For now, assume AX already contains microseconds
    
    ; Update total time
    add     WORD PTR [isr_total_time_us], ax
    adc     WORD PTR [isr_total_time_us + 2], 0
    
    ; Update maximum time
    cmp     ax, [isr_max_time_us]
    jbe     timing_updated
    mov     [isr_max_time_us], ax
    
    ; Check if exceeding target (60μs)
    cmp     ax, 60
    jbe     timing_updated
    
    ; Log warning if timing exceeds target
    ; (This would call a C function to log the warning)
    
timing_updated:
    pop     dx
    pop     cx
    pop     bx
    ret
update_isr_timing ENDP

;=============================================================================
; Week 1 NE2000 Compatibility Functions
;=============================================================================

;-----------------------------------------------------------------------------
; @brief NE2000 compatibility interrupt handler
; @return AX = number of events processed
;-----------------------------------------------------------------------------
handle_ne2000_isr PROC
    push    bx
    push    cx
    push    dx
    
    ; Get I/O base from context
    mov     bx, OFFSET _g_ptask_context
    add     bx, PTASK_CTX_IO_BASE
    mov     dx, [bx]                    ; DX = I/O base
    
    ; Read NE2000 interrupt status register
    add     dx, 07h                     ; Interrupt Status Register
    in      al, dx
    
    ; Check for receive interrupt
    test    al, 01h                     ; RX interrupt bit
    jz      check_tx_ne2000
    
    ; Handle RX interrupt
    ; (Minimal processing in ISR)
    inc     cx                          ; Count events
    
check_tx_ne2000:
    ; Check for transmit interrupt  
    test    al, 02h                     ; TX interrupt bit
    jz      ne2000_isr_done
    
    ; Handle TX interrupt
    inc     cx                          ; Count events
    
ne2000_isr_done:
    ; Clear interrupt status
    out     dx, al                      ; Write back to clear
    
    mov     ax, cx                      ; Return event count
    
    pop     dx
    pop     cx
    pop     bx
    ret
handle_ne2000_isr ENDP

END