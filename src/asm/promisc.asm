; @file promisc.asm
; @brief Low-level promiscuous mode support with optimized packet capture
;
; 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
;
; This file is part of the 3Com Packet Driver project.
;

bits 16
cpu 386

; C symbol naming bridge (maps C symbols to symbol_)
%include "csym.inc"

; External C functions and variables (csym.inc maps to trailing underscore)
extern g_promisc_buffer_tail
; NOTE: g_promisc_buffers removed - now uses XMS allocation on 386+ systems.
; Assembly routines now call C functions for buffer management.
extern promisc_capture_packet

; promisc_add_buffer_packet_asm is defined in linkasm.asm (ASM-to-ASM, no mapping)
extern promisc_add_buffer_packet_asm

; External CPU optimization level from packet_ops.asm
extern current_cpu_opt

; CPU optimization level constants (must match packet_ops.asm)
OPT_8086            EQU 0       ; 8086/8088 baseline (no 186+ instructions)
OPT_16BIT           EQU 1       ; 186+ optimizations (PUSHA/POPA available)
OPT_32BIT           EQU 2       ; 386+ optimizations (32-bit registers)

;==============================================================================
; CPU-adaptive register save/restore macros for interrupt handlers
;
; SAVE_ALL_REGS_32: Save all registers (8086/286: 16-bit, 386+: 32-bit)
; RESTORE_ALL_REGS_32: Restore all registers
;
; These macros check current_cpu_opt at runtime to select the appropriate
; instruction sequence. On 386+, uses PUSHAD/POPAD. On 286, uses PUSHA/POPA.
; On 8086/8088, uses explicit push/pop sequences.
;==============================================================================

%macro SAVE_ALL_REGS_32 0
        push ax
        mov al, [current_cpu_opt]
        cmp al, OPT_32BIT
        pop ax
        jae %%use_pushad
        push ax
        mov al, [current_cpu_opt]
        test al, OPT_16BIT
        pop ax
        jnz %%use_pusha
        ;; 8086 path: explicit push sequence (pushes same regs as PUSHA)
        push ax
        push cx
        push dx
        push bx
        push sp
        push bp
        push si
        push di
        jmp short %%done
%%use_pusha:
        ;; 286 path: use PUSHA (16-bit)
        pusha
        jmp short %%done
%%use_pushad:
        ;; 386+ path: use PUSHAD (32-bit)
        pushad
%%done:
%endmacro

%macro RESTORE_ALL_REGS_32 0
        ;; Must determine CPU type without corrupting registers being restored
        ;; Use stack to preserve test value
        push ax
        mov al, [current_cpu_opt]
        cmp al, OPT_32BIT
        pop ax
        jae %%use_popad
        push ax
        mov al, [current_cpu_opt]
        test al, OPT_16BIT
        pop ax
        jnz %%use_popa
        ;; 8086 path: explicit pop sequence (reverse of push)
        pop di
        pop si
        pop bp
        add sp, 2               ; Skip SP (was pushed but not restored)
        pop bx
        pop dx
        pop cx
        pop ax
        jmp short %%done
%%use_popa:
        ;; 286 path: use POPA (16-bit)
        popa
        jmp short %%done
%%use_popad:
        ;; 386+ path: use POPAD (32-bit)
        popad
%%done:
%endmacro

; Constants
PROMISC_BUFFER_SIZE     EQU     1600
PROMISC_BUFFER_COUNT    EQU     64

; Data segment
segment _DATA class=DATA
        ; Capture state variables
        capture_enabled         db      0
        capture_timestamp       dd      0
        capture_buffer_ptr      dd      0

        ; Performance counters
        asm_packets_captured    dd      0
        asm_capture_errors      dd      0


; Code segment
segment _TEXT class=CODE

; Export functions for C
global _promisc_asm_capture_packet
global _promisc_asm_enable_capture
global _promisc_asm_disable_capture
global _promisc_asm_get_timestamp

;==============================================================================
; promisc_asm_capture_packet - High-speed packet capture
;
; Parameters:
;   [BP+6] = packet pointer (far)
;   [BP+10] = packet length (word)
;
; Returns:
;   AX = 0 on success, error code on failure
;==============================================================================
_promisc_asm_capture_packet:
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si
        push    di
        push    es
        push    ds

        ; Check if capture is enabled
        cmp     byte [capture_enabled], 0
        je      capture_disabled

        ; Get packet parameters
        les     si, [bp+6]              ; ES:SI = packet pointer
        mov     cx, word [bp+10]        ; CX = packet length

        ; Validate parameters
        test    cx, cx
        jz      invalid_length
        cmp     cx, PROMISC_BUFFER_SIZE
        ja      packet_too_large

        ; Get current timestamp
        call    get_high_precision_timestamp
        mov     [capture_timestamp], eax

        ; Fast memcpy for packet data
        call    fast_packet_copy
        jc      copy_error

        ; Update counters
        inc     dword [asm_packets_captured]

        ; Success
        xor     ax, ax
        jmp     capture_exit

capture_disabled:
        mov     ax, 1               ; ERROR_NOT_ENABLED
        jmp     capture_exit

invalid_length:
        mov     ax, 2               ; ERROR_INVALID_PARAM
        jmp     capture_exit

packet_too_large:
        mov     ax, 3               ; ERROR_BUFFER_TOO_SMALL
        jmp     capture_exit

copy_error:
        inc     dword [asm_capture_errors]
        mov     ax, 4               ; ERROR_COPY_FAILED
        jmp     capture_exit

capture_exit:
        pop     ds
        pop     es
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret

;==============================================================================
; promisc_asm_enable_capture - Enable high-speed capture mode
;
; Returns:
;   AX = 0 on success
;==============================================================================
_promisc_asm_enable_capture:
        push    bp
        mov     bp, sp

        ; Enable capture
        mov     byte [capture_enabled], 1

        ; Reset counters
        mov     dword [asm_packets_captured], 0
        mov     dword [asm_capture_errors], 0

        ; Initialize timestamp
        call    get_high_precision_timestamp
        mov     [capture_timestamp], eax

        xor     ax, ax              ; Success

        pop     bp
        ret

;==============================================================================
; promisc_asm_disable_capture - Disable high-speed capture mode
;
; Returns:
;   AX = 0 on success
;==============================================================================
_promisc_asm_disable_capture:
        push    bp
        mov     bp, sp

        ; Disable capture
        mov     byte [capture_enabled], 0

        xor     ax, ax              ; Success

        pop     bp
        ret

;==============================================================================
; promisc_asm_get_timestamp - Get high-precision timestamp
;
; Returns:
;   DX:AX = 32-bit timestamp
;==============================================================================
_promisc_asm_get_timestamp:
        push    bp
        mov     bp, sp

        call    get_high_precision_timestamp

        ; Return value in DX:AX
        mov     dx, ax
        shr     eax, 16
        xchg    ax, dx
        ; AX already contains low 16 bits

        pop     bp
        ret

;==============================================================================
; Internal helper functions
;==============================================================================

;------------------------------------------------------------------------------
; get_high_precision_timestamp - Get system timestamp
;
; Returns:
;   EAX = 32-bit timestamp
;------------------------------------------------------------------------------
get_high_precision_timestamp:
        push    dx
        push    cx

        ; Use BIOS timer tick count for now
        ; In real implementation, could use RTC or other high-precision timer
        mov     ah, 00h
        int     1Ah                 ; Get timer tick count

        ; CX:DX contains tick count
        mov     eax, ecx
        shl     eax, 16
        mov     ax, dx

        pop     cx
        pop     dx
        ret

;------------------------------------------------------------------------------
; fast_packet_copy - Optimized packet copying with CPU detection
;
; Input:
;   ES:SI = source packet
;   CX = length
;
; Returns:
;   CF = 0 on success, 1 on error
;------------------------------------------------------------------------------
fast_packet_copy:
        ; XMS OPTIMIZATION NOTE:
        ; The promiscuous capture buffers are now allocated from XMS on 386+ systems.
        ; Direct buffer access from assembly is no longer possible.
        ; This function now calls the C-level buffer management function.
        ;
        ; Input:
        ;   ES:SI = source packet
        ;   CX = length
        ;
        ; Returns:
        ;   CF = 0 on success, 1 on error

        push    ax
        push    bx
        push    dx

        ; Call C function: promisc_add_buffer_packet_asm(packet, length, nic_index, filter)
        ; Push parameters in reverse order (C calling convention)
        push    word 0                  ; filter_matched = 0
        push    word 0                  ; nic_index = 0
        push    cx                      ; length
        push    es                      ; packet segment
        push    si                      ; packet offset

        ; Call C function to handle buffer management (including XMS copy)
        call    promisc_add_buffer_packet_asm

        ; Clean up stack (5 words = 10 bytes)
        add     sp, 10

        ; Check return value
        test    ax, ax
        jnz     .copy_error

        clc                             ; Clear carry (success)
        jmp     .copy_exit

.copy_error:
        stc                             ; Set carry (error)

.copy_exit:
        pop     dx
        pop     bx
        pop     ax
        ret

;------------------------------------------------------------------------------
; detect_cpu_for_copy - Detect CPU type for optimal copy routine selection
;
; Returns:
;   AL = CPU type (0=8086, 1=8088, 2=286, 3=386, 4=486+)
;------------------------------------------------------------------------------
detect_cpu_for_copy:
        push    bx
        push    cx
        push    dx

        ; Simple CPU detection for copy optimization
        ; Test for 386+ by checking if we can set/clear AC flag
        pushfd                          ; Try to push 32-bit flags
        jc      .cpu_286_or_less        ; If carry set, not 386+

        ; We have 386+
        mov     al, 3
        jmp     .cpu_detect_done

.cpu_286_or_less:
        ; Test for 286 by checking protected mode capability
        pushf                           ; Push 16-bit flags
        pop     bx                      ; Get flags in BX
        mov     cx, bx                  ; Save original flags
        or      bx, 7000h              ; Try to set bits 12-14
        push    bx
        popf
        pushf
        pop     bx
        and     bx, 7000h              ; Check if bits stayed set
        push    cx
        popf                            ; Restore original flags

        cmp     bx, 7000h
        je      .cpu_is_286             ; If bits set, it's 286+

        ; It's 8086/8088
        mov     al, 0
        jmp     .cpu_detect_done

.cpu_is_286:
        mov     al, 2

.cpu_detect_done:
        pop     dx
        pop     cx
        pop     bx
        ret

;------------------------------------------------------------------------------
; promisc_asm_interrupt_handler - High-speed interrupt handler for promiscuous mode
;
; This function can be called from NIC interrupt handlers to capture packets
; with minimal overhead and error packet handling
;
; Register save/restore is CPU-adaptive:
;   - 8086/8088: Explicit PUSH/POP sequence (16-bit)
;   - 80286: PUSHA/POPA (16-bit)
;   - 80386+: PUSHAD/POPAD (32-bit)
;------------------------------------------------------------------------------
promisc_asm_interrupt_handler:
        SAVE_ALL_REGS_32        ; CPU-adaptive: PUSHAD (386+), PUSHA (286), explicit (8086)
        push    es
        push    ds

        ; Check if capture enabled
        cmp     byte [capture_enabled], 0
        je      .interrupt_exit

        ; Enhanced interrupt handling with error packet detection
        ; Get packet status from NIC-specific registers

        ; For 3C509B: Check Window 1, RX Status
        ; For 3C515-TX: Check DMA descriptor status

        ; Fast path: check for common error conditions
        call    check_packet_errors
        jc      .handle_error_packet

        ; Fast packet processing for valid packets
        call    fast_packet_reception
        jnc     .update_counters

.handle_error_packet:
        ; Handle error packets in promiscuous mode
        ; Even error packets should be captured for network analysis
        inc     dword [asm_capture_errors]

        ; Check if we should still capture error packets
        ; (promiscuous mode typically wants ALL packets)
        call    capture_error_packet

.update_counters:
        inc     dword [asm_packets_captured]

.interrupt_exit:
        pop     ds
        pop     es
        RESTORE_ALL_REGS_32     ; CPU-adaptive: POPAD (386+), POPA (286), explicit (8086)
        ret

;------------------------------------------------------------------------------
; check_packet_errors - Check for common packet errors
;
; Returns:
;   CF = 0 if packet is valid, CF = 1 if error detected
;------------------------------------------------------------------------------
check_packet_errors:
        push    ax
        push    dx

        ; This would be NIC-specific error checking
        ; For now, assume packet is valid
        clc                             ; Clear carry (no error)

        pop     dx
        pop     ax
        ret

;------------------------------------------------------------------------------
; fast_packet_reception - Optimized packet reception using REP INSW
;
; Returns:
;   CF = 0 on success, CF = 1 on error
;------------------------------------------------------------------------------
fast_packet_reception:
        push    ax
        push    cx
        push    dx
        push    di

        ; This is a placeholder for NIC-specific fast reception
        ; Real implementation would use:
        ; - REP INSW for 3C509B (PIO mode)
        ; - DMA descriptor processing for 3C515-TX

        ; For demonstration, simulate successful reception
        clc                             ; Clear carry (success)

        pop     di
        pop     dx
        pop     cx
        pop     ax
        ret

;------------------------------------------------------------------------------
; capture_error_packet - Capture error packet for analysis
;
; Returns:
;   CF = 0 on success, CF = 1 on error
;------------------------------------------------------------------------------
capture_error_packet:
        push    ax

        ; In promiscuous mode, we often want to capture error packets
        ; for network troubleshooting and analysis
        ; This would copy the error packet to the capture buffer
        ; with appropriate error flags set

        clc                             ; Clear carry (success)

        pop     ax
        ret

;------------------------------------------------------------------------------
; promisc_asm_buffer_management - Optimized buffer management
;
; Manages the circular buffer for captured packets
;------------------------------------------------------------------------------
promisc_asm_buffer_management:
        push    eax
        push    ebx

        ; Check buffer space
        mov     eax, [g_promisc_buffer_tail]
        inc     eax
        cmp     eax, PROMISC_BUFFER_COUNT
        jb      .no_wrap
        xor     eax, eax                ; Wrap to 0

.no_wrap:
        ; Check if buffer would be full
        ; (implementation depends on head pointer)

        ; Update tail pointer
        mov     [g_promisc_buffer_tail], eax

        pop     ebx
        pop     eax
        ret

;------------------------------------------------------------------------------
; CPU-specific optimizations
;------------------------------------------------------------------------------

;------------------------------------------------------------------------------
; promisc_asm_386_copy - 386+ optimized copy routine
;------------------------------------------------------------------------------
promisc_asm_386_copy:
        ; Use 32-bit registers and instructions for faster copying
        ; This routine is called when CPU detection shows 386+

        push    eax
        push    ecx
        push    esi
        push    edi

        ; Convert 16-bit pointers to 32-bit
        movzx   esi, si
        movzx   edi, di
        movzx   ecx, cx

        ; Copy using 32-bit moves
        shr     ecx, 2              ; Convert to dwords

.copy_386_dwords:
        mov     eax, [es:esi]
        mov     [ds:edi], eax
        add     esi, 4
        add     edi, 4
        loop    .copy_386_dwords

        pop     edi
        pop     esi
        pop     ecx
        pop     eax
        ret

;------------------------------------------------------------------------------
; promisc_asm_286_copy - 286 optimized copy routine
;------------------------------------------------------------------------------
promisc_asm_286_copy:
        ; Use 16-bit optimized copy for 286 systems
        push    ax
        push    cx

        shr     cx, 1               ; Convert to words

.copy_286_words:
        mov     ax, [es:si]
        mov     [ds:di], ax
        add     si, 2
        add     di, 2
        loop    .copy_286_words

        pop     cx
        pop     ax
        ret

;------------------------------------------------------------------------------
; Statistics and monitoring functions
;------------------------------------------------------------------------------

;------------------------------------------------------------------------------
; promisc_asm_get_stats - Get ASM-level statistics
;
; Returns performance counters for ASM-level operations
;------------------------------------------------------------------------------
promisc_asm_get_stats:
        push    bx

        ; Return stats in registers
        mov     eax, [asm_packets_captured]
        mov     ebx, [asm_capture_errors]

        pop     bx
        ret

;------------------------------------------------------------------------------
; promisc_asm_reset_stats - Reset ASM-level statistics
;------------------------------------------------------------------------------
promisc_asm_reset_stats:
        mov     dword [asm_packets_captured], 0
        mov     dword [asm_capture_errors], 0
        ret

