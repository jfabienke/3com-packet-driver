; @file tsr_memory_opt.asm
; @brief TSR memory optimization and layout management
;
; 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
;
; Advanced memory optimization for minimal conventional memory usage.
; Implements segment ordering, overlay loading, and stack optimization.
;

.model small
.386

include 'tsr_defensive.inc'

; Memory optimization constants
INIT_SIGNATURE          equ 'INIT'
RESIDENT_SIGNATURE      equ 'RESN'
OVERLAY_SIGNATURE       equ 'OVLY'
MIN_STACK_SIZE          equ 512     ; Minimum stack for normal operation
STACK_SAFETY_MARGIN     equ 64      ; Safety margin for stack depth checks
OVERLAY_BUFFER_SIZE     equ 2048    ; Buffer for overlay loading

; Memory layout definitions
PSP_SIZE                equ 256     ; PSP size in bytes
PARAGRAPH_SIZE          equ 16      ; Bytes per paragraph

;=============================================================================
; OPTIMIZED SEGMENT ORDERING
;=============================================================================

; Critical resident code segment - must stay in conventional memory
RESIDENT_CODE_SEG SEGMENT PARA PUBLIC 'RESIDENT_CODE'
ASSUME CS:RESIDENT_CODE_SEG

; Resident code marker
resident_code_start:
    db RESIDENT_SIGNATURE

; Critical ISR handlers (must be resident)
PUBLIC resident_packet_handler
resident_packet_handler PROC FAR
    ; Minimal packet handler - chains to full handler or overlay
    pusha
    push ds
    push es
    
    ; Check if full handler is resident
    test byte ptr cs:[full_handler_resident], 1
    jnz .call_resident_handler
    
    ; Load overlay if needed
    call load_packet_handler_overlay
    jc .overlay_failed
    
.call_resident_handler:
    ; Call full packet handler
    call far ptr full_packet_handler
    jmp short .exit
    
.overlay_failed:
    ; Return error - packet dropped
    mov dh, BAD_COMMAND
    stc
    
.exit:
    pop es
    pop ds
    popa
    iret
resident_packet_handler ENDP

; Minimal hardware interrupt handler
PUBLIC resident_hardware_isr
resident_hardware_isr PROC FAR
    ; Save minimal state
    push ax
    push dx
    
    ; Quick hardware check
    mov dx, cs:[nic_base_port]
    in al, dx
    test al, 01h                    ; Check for pending interrupt
    jz .not_ours
    
    ; Queue deferred processing
    mov ax, offset deferred_irq_handler
    call queue_deferred_work
    
.not_ours:
    ; Send EOI
    mov al, 20h
    out 20h, al
    
    pop dx
    pop ax
    iret
resident_hardware_isr ENDP

; Memory usage tracking
resident_code_end:
    nop

RESIDENT_CODE_SEG ENDS

; Resident data segment - critical runtime data only
RESIDENT_DATA_SEG SEGMENT PARA PUBLIC 'RESIDENT_DATA'
ASSUME DS:RESIDENT_DATA_SEG

resident_data_start:
    db RESIDENT_SIGNATURE

; Critical runtime variables only
full_handler_resident   db 1        ; Full handler availability flag
overlay_loaded          db 0        ; Overlay load status
nic_base_port           dw 0        ; NIC base I/O port
packet_buffer_ptr       dw 0        ; Current packet buffer pointer
handle_count           db 0         ; Active handle count
error_flags            db 0         ; Error status flags

; Compact handle table (8 bytes per handle)
handle_table           db 16*8 dup(?)  ; 16 handles max, 8 bytes each

; Small packet buffer for critical operations
critical_packet_buffer db 64 dup(?)    ; 64 bytes for urgent packets

; Stack depth monitoring
stack_low_water        dw 0         ; Lowest stack pointer seen
stack_overflow_count   dw 0         ; Stack overflow events

resident_data_end:
    nop

RESIDENT_DATA_SEG ENDS

; Minimal resident stack - 512 bytes for normal operation
RESIDENT_STACK_SEG SEGMENT PARA PUBLIC 'RESIDENT_STACK'

resident_stack_start:
    db MIN_STACK_SIZE dup(?)
resident_stack_end:
    equ $ - 2                       ; Top of stack

RESIDENT_STACK_SEG ENDS

;=============================================================================
; DISCARDABLE INITIALIZATION SEGMENTS
;=============================================================================

; Initialization code - discarded after TSR installation
INIT_CODE_SEG SEGMENT PARA PUBLIC 'INIT_CODE'
ASSUME CS:INIT_CODE_SEG

init_code_start:
    db INIT_SIGNATURE

PUBLIC initialize_memory_optimization
initialize_memory_optimization PROC
    push bp
    mov bp, sp
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    
    ; Calculate resident memory layout
    call calculate_memory_layout
    
    ; Initialize stack monitoring
    call initialize_stack_monitoring
    
    ; Set up overlay loading infrastructure
    call initialize_overlay_system
    
    ; Optimize data structures
    call optimize_data_structures
    
    ; Test memory protection
    call test_memory_protection
    
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    pop bp
    ret
initialize_memory_optimization ENDP

calculate_memory_layout PROC
    push ax
    push bx
    push cx
    push dx
    
    ; Calculate resident code size
    mov ax, offset resident_code_end
    mov bx, offset resident_code_start
    sub ax, bx
    mov [resident_code_size], ax
    
    ; Calculate resident data size  
    mov ax, offset resident_data_end
    mov bx, offset resident_data_start
    sub ax, bx
    mov [resident_data_size], ax
    
    ; Calculate total resident size in paragraphs
    mov ax, [resident_code_size]
    add ax, [resident_data_size]
    add ax, MIN_STACK_SIZE
    add ax, PSP_SIZE
    
    ; Round up to paragraphs
    add ax, PARAGRAPH_SIZE - 1
    and ax, not (PARAGRAPH_SIZE - 1)
    shr ax, 4                       ; Convert to paragraphs
    
    mov [total_resident_paragraphs], ax
    
    pop dx
    pop cx
    pop bx
    pop ax
    ret
calculate_memory_layout ENDP

initialize_stack_monitoring PROC
    push ax
    push bx
    
    ; Set initial stack low water mark
    mov ax, sp
    mov [stack_low_water], ax
    
    ; Clear overflow counter
    mov word ptr [stack_overflow_count], 0
    
    pop bx
    pop ax
    ret
initialize_stack_monitoring ENDP

initialize_overlay_system PROC
    push ax
    push bx
    
    ; Mark overlays as not loaded
    mov byte ptr [overlay_loaded], 0
    
    ; Set up overlay buffer
    mov ax, seg overlay_buffer
    mov [overlay_buffer_segment], ax
    
    pop bx
    pop ax
    ret
initialize_overlay_system ENDP

optimize_data_structures PROC
    push ax
    push bx
    push cx
    push di
    push es
    
    ; Zero-initialize handle table for compactness
    mov ax, seg handle_table
    mov es, ax
    mov di, offset handle_table
    mov cx, 16*8/2                  ; Size in words
    xor ax, ax
    rep stosw
    
    ; Initialize packet buffer pointer
    mov ax, offset critical_packet_buffer
    mov [packet_buffer_ptr], ax
    
    pop es
    pop di
    pop cx
    pop bx
    pop ax
    ret
optimize_data_structures ENDP

test_memory_protection PROC
    push ax
    push bx
    
    ; Test stack depth detection
    call check_stack_depth
    
    ; Test overlay loading mechanism
    call test_overlay_mechanism
    
    pop bx
    pop ax
    ret
test_memory_protection ENDP

test_overlay_mechanism PROC
    ; Placeholder for overlay loading test
    ; In full implementation, would test overlay file access
    ret
test_overlay_mechanism ENDP

; Hardware initialization (discarded after init)
PUBLIC init_hardware_memory_layout
init_hardware_memory_layout PROC
    push ax
    push dx
    
    ; Store NIC base port for resident code
    mov dx, 300h                    ; Example I/O port
    mov [nic_base_port], dx
    
    ; Initialize critical packet buffer
    mov ax, offset critical_packet_buffer
    mov [packet_buffer_ptr], ax
    
    pop dx
    pop ax
    ret
init_hardware_memory_layout ENDP

init_code_end:
    nop

INIT_CODE_SEG ENDS

; Initialization data - discarded after TSR installation
INIT_DATA_SEG SEGMENT PARA PUBLIC 'INIT_DATA'

init_data_start:
    db INIT_SIGNATURE

; Size calculation variables (discarded after init)
resident_code_size      dw 0
resident_data_size      dw 0
total_resident_paragraphs dw 0
init_code_size          dw 0
init_data_size          dw 0

; Hardware detection data (discarded after init)
detected_nics           db 4*32 dup(?)  ; Detection results
nic_count              db 0

; Initialization messages (discarded after init)
memory_opt_banner       db 'Memory optimization initialized', 0Dh, 0Ah, 0
resident_size_msg       db 'Resident size: %d paragraphs', 0Dh, 0Ah, 0
init_size_msg          db 'Initialization size: %d paragraphs (discarded)', 0Dh, 0Ah, 0

; Overlay loading infrastructure data
overlay_buffer_segment  dw 0
overlay_files          db 'PKTDIAG.OVL', 0
                      db 'PKTSTATS.OVL', 0
                      db 0               ; End marker

init_data_end:
    nop

INIT_DATA_SEG ENDS

;=============================================================================
; OVERLAY LOADING SYSTEM
;=============================================================================

; Overlay code segment - loaded on demand
OVERLAY_CODE_SEG SEGMENT PARA PUBLIC 'OVERLAY_CODE'
ASSUME CS:OVERLAY_CODE_SEG

overlay_code_start:
    db OVERLAY_SIGNATURE

; Full packet handler (loaded on demand)
PUBLIC full_packet_handler
full_packet_handler PROC FAR
    ; Full implementation loaded from overlay file
    ; This is a placeholder - actual code loaded from disk
    
    ; For now, minimal implementation
    mov dh, BAD_COMMAND
    stc
    retf
full_packet_handler ENDP

; Diagnostics overlay entry point
PUBLIC diagnostics_overlay_entry
diagnostics_overlay_entry PROC FAR
    ; Diagnostic functions loaded on demand
    pusha
    push ds
    push es
    
    ; Perform diagnostic operations
    call run_diagnostics
    
    pop es
    pop ds
    popa
    retf
diagnostics_overlay_entry ENDP

run_diagnostics PROC
    ; Placeholder for diagnostic routines
    ret
run_diagnostics ENDP

overlay_code_end:
    nop

OVERLAY_CODE_SEG ENDS

; Overlay buffer - temporary storage for loaded code
OVERLAY_BUFFER_SEG SEGMENT PARA PUBLIC 'OVERLAY_BUFFER'

overlay_buffer:
    db OVERLAY_BUFFER_SIZE dup(?)

OVERLAY_BUFFER_SEG ENDS

;=============================================================================
; RUNTIME MEMORY MANAGEMENT
;=============================================================================

.code

; Stack depth monitoring
PUBLIC check_stack_depth
check_stack_depth PROC
    push ax
    push bx
    
    ; Get current stack pointer
    mov ax, sp
    
    ; Update low water mark if necessary
    cmp ax, [stack_low_water]
    jae .no_update
    mov [stack_low_water], ax
    
.no_update:
    ; Check for potential overflow
    mov bx, offset resident_stack_start
    add bx, STACK_SAFETY_MARGIN
    cmp ax, bx
    ja .stack_ok
    
    ; Stack getting low - increment counter
    inc word ptr [stack_overflow_count]
    
    ; Set error flag
    or byte ptr [error_flags], 01h  ; Stack overflow warning
    
.stack_ok:
    pop bx
    pop ax
    ret
check_stack_depth ENDP

; Overlay loading mechanism
PUBLIC load_packet_handler_overlay
load_packet_handler_overlay PROC
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push ds
    push es
    
    ; Check if already loaded
    test byte ptr [overlay_loaded], 1
    jnz .already_loaded
    
    ; In a full implementation, would:
    ; 1. Open overlay file (PKTHAND.OVL)
    ; 2. Read into overlay buffer
    ; 3. Relocate addresses
    ; 4. Set overlay_loaded flag
    
    ; For now, just mark as loaded
    mov byte ptr [overlay_loaded], 1
    clc                             ; Success
    jmp short .exit
    
.already_loaded:
    clc                             ; Success
    
.exit:
    pop es
    pop ds
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    ret
load_packet_handler_overlay ENDP

; Deferred IRQ processing
deferred_irq_handler PROC
    ; Process hardware interrupt in background
    push ax
    push dx
    
    ; Read and clear interrupt status
    mov dx, [nic_base_port]
    in al, dx
    
    ; Process received packets if any
    test al, 02h                    ; RX ready flag
    jz .no_rx
    
    call process_received_packet
    
.no_rx:
    ; Clear processed flag
    and byte ptr [error_flags], not 02h
    
    pop dx
    pop ax
    ret
deferred_irq_handler ENDP

process_received_packet PROC
    ; Minimal packet processing for memory efficiency
    push ax
    push bx
    push cx
    push si
    push di
    
    ; Use critical packet buffer for minimal processing
    mov si, [packet_buffer_ptr]
    
    ; Read packet header (simplified)
    mov dx, [nic_base_port]
    add dx, 4                       ; Data port offset
    in al, dx
    mov [si], al                    ; Store first byte
    
    ; Set packet ready flag
    or byte ptr [error_flags], 04h  ; Packet available
    
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret
process_received_packet ENDP

; Memory usage reporting
PUBLIC get_memory_usage_stats
get_memory_usage_stats PROC
    ; Returns memory usage statistics
    ; ES:DI -> statistics buffer
    
    push ax
    push bx
    push cx
    push si
    
    ; Calculate current resident size
    mov ax, [total_resident_paragraphs]
    mov es:[di], ax                 ; Resident paragraphs
    
    ; Stack usage statistics
    mov ax, MIN_STACK_SIZE
    mov bx, [stack_low_water]
    sub ax, bx
    mov es:[di+2], ax               ; Stack usage (bytes)
    
    ; Error counters
    mov ax, [stack_overflow_count]
    mov es:[di+4], ax               ; Stack overflow events
    
    ; Handle usage
    xor al, al
    mov [handle_count], al
    mov al, [handle_count]
    mov es:[di+6], al               ; Active handles
    
    pop si
    pop cx
    pop bx
    pop ax
    ret
get_memory_usage_stats ENDP

; Memory compaction and optimization
PUBLIC compact_memory_structures
compact_memory_structures PROC
    push ax
    push bx
    push cx
    push si
    push di
    
    ; Compact handle table - remove unused entries
    mov si, offset handle_table
    mov di, si
    mov cx, 16                      ; Max handles
    xor bl, bl                      ; Active count
    
.compact_loop:
    ; Check if handle is active (first byte != 0)
    cmp byte ptr [si], 0
    jz .skip_handle
    
    ; Copy active handle to compacted position
    push cx
    mov cx, 8                       ; Handle entry size
    rep movsb
    pop cx
    inc bl                          ; Count active handles
    jmp short .next_handle
    
.skip_handle:
    add si, 8                       ; Skip inactive handle
    
.next_handle:
    loop .compact_loop
    
    ; Update active handle count
    mov [handle_count], bl
    
    ; Zero remaining table space
    mov ax, 16
    sub al, bl
    jz .no_cleanup
    mov cl, al
    mov al, cl
    mov cl, 8
    mul cl                          ; AX = bytes to clear
    mov cx, ax
    xor al, al
    rep stosb
    
.no_cleanup:
    pop di
    pop si
    pop cx
    pop bx
    pop ax
    ret
compact_memory_structures ENDP

; Export symbols for linker
PUBLIC resident_code_start, resident_code_end
PUBLIC resident_data_start, resident_data_end
PUBLIC init_code_start, init_code_end
PUBLIC init_data_start, init_data_end

.data
; Global memory optimization state
memory_opt_initialized  db 0

end