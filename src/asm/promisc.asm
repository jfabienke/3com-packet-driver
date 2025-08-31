; @file promisc.asm
; @brief Low-level promiscuous mode support with optimized packet capture
;
; 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
;
; This file is part of the 3Com Packet Driver project.
;

.MODEL SMALL
.386

; External C functions and variables
EXTERN _g_promisc_buffer_tail:DWORD
EXTERN _g_promisc_buffers:BYTE
EXTERN _promisc_capture_packet:PROC

; Constants
PROMISC_BUFFER_SIZE     EQU     1600
PROMISC_BUFFER_COUNT    EQU     64

; Data segment
_DATA SEGMENT
        ; Capture state variables
        capture_enabled         db      0
        capture_timestamp       dd      0
        capture_buffer_ptr      dd      0
        
        ; Performance counters
        asm_packets_captured    dd      0
        asm_capture_errors      dd      0
        
_DATA ENDS

; Code segment
_TEXT SEGMENT
        ASSUME  CS:_TEXT, DS:_DATA

; Export functions for C
PUBLIC _promisc_asm_capture_packet
PUBLIC _promisc_asm_enable_capture
PUBLIC _promisc_asm_disable_capture
PUBLIC _promisc_asm_get_timestamp

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
_promisc_asm_capture_packet PROC FAR
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
        cmp     capture_enabled, 0
        je      capture_disabled
        
        ; Get packet parameters
        les     si, dword ptr [bp+6]   ; ES:SI = packet pointer
        mov     cx, word ptr [bp+10]   ; CX = packet length
        
        ; Validate parameters
        test    cx, cx
        jz      invalid_length
        cmp     cx, PROMISC_BUFFER_SIZE
        ja      packet_too_large
        
        ; Get current timestamp
        call    get_high_precision_timestamp
        mov     capture_timestamp, eax
        
        ; Fast memcpy for packet data
        call    fast_packet_copy
        jc      copy_error
        
        ; Update counters
        inc     asm_packets_captured
        
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
        inc     asm_capture_errors
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
_promisc_asm_capture_packet ENDP

;==============================================================================
; promisc_asm_enable_capture - Enable high-speed capture mode
;
; Returns:
;   AX = 0 on success
;==============================================================================
_promisc_asm_enable_capture PROC FAR
        push    bp
        mov     bp, sp
        
        ; Enable capture
        mov     capture_enabled, 1
        
        ; Reset counters
        mov     asm_packets_captured, 0
        mov     asm_capture_errors, 0
        
        ; Initialize timestamp
        call    get_high_precision_timestamp
        mov     capture_timestamp, eax
        
        xor     ax, ax              ; Success
        
        pop     bp
        ret
_promisc_asm_enable_capture ENDP

;==============================================================================
; promisc_asm_disable_capture - Disable high-speed capture mode
;
; Returns:
;   AX = 0 on success
;==============================================================================
_promisc_asm_disable_capture PROC FAR
        push    bp
        mov     bp, sp
        
        ; Disable capture
        mov     capture_enabled, 0
        
        xor     ax, ax              ; Success
        
        pop     bp
        ret
_promisc_asm_disable_capture ENDP

;==============================================================================
; promisc_asm_get_timestamp - Get high-precision timestamp
;
; Returns:
;   DX:AX = 32-bit timestamp
;==============================================================================
_promisc_asm_get_timestamp PROC FAR
        push    bp
        mov     bp, sp
        
        call    get_high_precision_timestamp
        
        ; Return value in DX:AX
        mov     dx, eax
        shr     dx, 16
        ; AX already contains low 16 bits
        
        pop     bp
        ret
_promisc_asm_get_timestamp ENDP

;==============================================================================
; Internal helper functions
;==============================================================================

;------------------------------------------------------------------------------
; get_high_precision_timestamp - Get system timestamp
;
; Returns:
;   EAX = 32-bit timestamp
;------------------------------------------------------------------------------
get_high_precision_timestamp PROC NEAR
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
get_high_precision_timestamp ENDP

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
fast_packet_copy PROC NEAR
        push    ax
        push    bx
        push    dx
        push    di
        push    ds
        
        ; Set up destination (current buffer)
        mov     ax, _DATA
        mov     ds, ax
        
        ; Get buffer tail index
        mov     eax, _g_promisc_buffer_tail
        
        ; Calculate buffer address
        ; Each buffer is PROMISC_BUFFER_SIZE + metadata
        mov     ebx, PROMISC_BUFFER_SIZE + 16   ; Buffer size + metadata
        mul     ebx
        
        ; Add base address of buffers
        add     eax, offset _g_promisc_buffers
        mov     di, ax                  ; DI = destination offset
        
        ; Enhanced copy with CPU-specific optimizations
        push    cx                      ; Save original length
        
        ; Check CPU type for optimization selection
        call    detect_cpu_for_copy
        cmp     al, 3                   ; 386+ ?
        jae     copy_386_optimized
        cmp     al, 2                   ; 286 ?
        je      copy_286_optimized
        jmp     copy_8086_compatible    ; 8086/8088 fallback
        
copy_386_optimized:
        ; 386+ optimized copy using 32-bit operations
        pop     cx                      ; Restore length
        push    cx
        
        ; Check if length is large enough for burst copy
        cmp     cx, 32
        jb      copy_dwords_standard
        
        ; Use REP MOVSD for large packets (burst mode)
        push    ecx
        shr     ecx, 2                  ; Convert to dwords
        cld                             ; Clear direction flag
        rep     movsd                   ; Fast 32-bit copy
        pop     ecx
        and     ecx, 3                  ; Handle remaining bytes
        rep     movsb
        jmp     copy_done
        
copy_dwords_standard:
        shr     cx, 2                   ; Convert to dwords
        jz      copy_remaining_bytes
        
copy_dwords_loop:
        mov     eax, es:[si]
        mov     [di], eax
        add     si, 4
        add     di, 4
        dec     cx
        jnz     copy_dwords_loop
        jmp     copy_remaining_bytes
        
copy_286_optimized:
        ; 286 optimized copy using 16-bit operations
        pop     cx                      ; Restore length
        push    cx
        
        ; Use REP MOVSW for 286 systems
        cmp     cx, 16
        jb      copy_words_standard
        
        push    cx
        shr     cx, 1                   ; Convert to words
        cld
        rep     movsw                   ; Fast 16-bit copy
        pop     cx
        and     cx, 1                   ; Handle odd byte
        rep     movsb
        jmp     copy_done
        
copy_words_standard:
        shr     cx, 1                   ; Convert to words
        jz      copy_remaining_bytes
        
copy_words_loop:
        mov     ax, es:[si]
        mov     [di], ax
        add     si, 2
        add     di, 2
        dec     cx
        jnz     copy_words_loop
        jmp     copy_remaining_bytes
        
copy_8086_compatible:
        ; 8086/8088 compatible byte copy
        pop     cx                      ; Restore length
        push    cx
        
        ; Use REP MOVSB for maximum compatibility
        cld
        rep     movsb
        jmp     copy_done
        
copy_remaining_bytes:
        pop     cx
        and     cx, 3                   ; Remaining bytes (0-3)
        jz      copy_done
        
        ; Copy remaining bytes
copy_bytes:
        mov     al, es:[si]
        mov     [di], al
        inc     si
        inc     di
        dec     cx
        jnz     copy_bytes
        
copy_done:
        clc                             ; Clear carry (success)
        jmp     copy_exit
        
copy_error:
        stc                             ; Set carry (error)
        
copy_exit:
        pop     ds
        pop     di
        pop     dx
        pop     bx
        pop     ax
        ret
fast_packet_copy ENDP

;------------------------------------------------------------------------------
; detect_cpu_for_copy - Detect CPU type for optimal copy routine selection
;
; Returns:
;   AL = CPU type (0=8086, 1=8088, 2=286, 3=386, 4=486+)
;------------------------------------------------------------------------------
detect_cpu_for_copy PROC NEAR
        push    bx
        push    cx
        push    dx
        
        ; Simple CPU detection for copy optimization
        ; Test for 386+ by checking if we can set/clear AC flag
        pushfd                          ; Try to push 32-bit flags
        jc      cpu_286_or_less         ; If carry set, not 386+
        
        ; We have 386+
        mov     al, 3
        jmp     cpu_detect_done
        
cpu_286_or_less:
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
        je      cpu_is_286              ; If bits set, it's 286+
        
        ; It's 8086/8088
        mov     al, 0
        jmp     cpu_detect_done
        
cpu_is_286:
        mov     al, 2
        
cpu_detect_done:
        pop     dx
        pop     cx
        pop     bx
        ret
detect_cpu_for_copy ENDP

;------------------------------------------------------------------------------
; promisc_asm_interrupt_handler - High-speed interrupt handler for promiscuous mode
;
; This function can be called from NIC interrupt handlers to capture packets
; with minimal overhead and error packet handling
;------------------------------------------------------------------------------
promisc_asm_interrupt_handler PROC NEAR
        pushad
        push    es
        push    ds
        
        ; Check if capture enabled
        cmp     capture_enabled, 0
        je      interrupt_exit
        
        ; Enhanced interrupt handling with error packet detection
        ; Get packet status from NIC-specific registers
        
        ; For 3C509B: Check Window 1, RX Status
        ; For 3C515-TX: Check DMA descriptor status
        
        ; Fast path: check for common error conditions
        call    check_packet_errors
        jc      handle_error_packet
        
        ; Fast packet processing for valid packets
        call    fast_packet_reception
        jnc     update_counters
        
handle_error_packet:
        ; Handle error packets in promiscuous mode
        ; Even error packets should be captured for network analysis
        inc     asm_capture_errors
        
        ; Check if we should still capture error packets
        ; (promiscuous mode typically wants ALL packets)
        call    capture_error_packet
        
update_counters:
        inc     asm_packets_captured
        
interrupt_exit:
        pop     ds
        pop     es
        popad
        ret
promisc_asm_interrupt_handler ENDP

;------------------------------------------------------------------------------
; check_packet_errors - Check for common packet errors
;
; Returns:
;   CF = 0 if packet is valid, CF = 1 if error detected
;------------------------------------------------------------------------------
check_packet_errors PROC NEAR
        push    ax
        push    dx
        
        ; This would be NIC-specific error checking
        ; For now, assume packet is valid
        clc                             ; Clear carry (no error)
        
        pop     dx
        pop     ax
        ret
check_packet_errors ENDP

;------------------------------------------------------------------------------
; fast_packet_reception - Optimized packet reception using REP INSW
;
; Returns:
;   CF = 0 on success, CF = 1 on error
;------------------------------------------------------------------------------
fast_packet_reception PROC NEAR
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
fast_packet_reception ENDP

;------------------------------------------------------------------------------
; capture_error_packet - Capture error packet for analysis
;
; Returns:
;   CF = 0 on success, CF = 1 on error
;------------------------------------------------------------------------------
capture_error_packet PROC NEAR
        push    ax
        
        ; In promiscuous mode, we often want to capture error packets
        ; for network troubleshooting and analysis
        ; This would copy the error packet to the capture buffer
        ; with appropriate error flags set
        
        clc                             ; Clear carry (success)
        
        pop     ax
        ret
capture_error_packet ENDP

;------------------------------------------------------------------------------
; promisc_asm_buffer_management - Optimized buffer management
;
; Manages the circular buffer for captured packets
;------------------------------------------------------------------------------
promisc_asm_buffer_management PROC NEAR
        push    eax
        push    ebx
        
        ; Check buffer space
        mov     eax, _g_promisc_buffer_tail
        inc     eax
        cmp     eax, PROMISC_BUFFER_COUNT
        jb      no_wrap
        xor     eax, eax                ; Wrap to 0
        
no_wrap:
        ; Check if buffer would be full
        ; (implementation depends on head pointer)
        
        ; Update tail pointer
        mov     _g_promisc_buffer_tail, eax
        
        pop     ebx
        pop     eax
        ret
promisc_asm_buffer_management ENDP

;------------------------------------------------------------------------------
; CPU-specific optimizations
;------------------------------------------------------------------------------

;------------------------------------------------------------------------------
; promisc_asm_386_copy - 386+ optimized copy routine
;------------------------------------------------------------------------------
promisc_asm_386_copy PROC NEAR
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
        
copy_386_dwords:
        mov     eax, es:[esi]
        mov     ds:[edi], eax
        add     esi, 4
        add     edi, 4
        loop    copy_386_dwords
        
        pop     edi
        pop     esi
        pop     ecx
        pop     eax
        ret
promisc_asm_386_copy ENDP

;------------------------------------------------------------------------------
; promisc_asm_286_copy - 286 optimized copy routine
;------------------------------------------------------------------------------
promisc_asm_286_copy PROC NEAR
        ; Use 16-bit optimized copy for 286 systems
        push    ax
        push    cx
        
        shr     cx, 1               ; Convert to words
        
copy_286_words:
        mov     ax, es:[si]
        mov     ds:[di], ax
        add     si, 2
        add     di, 2
        loop    copy_286_words
        
        pop     cx
        pop     ax
        ret
promisc_asm_286_copy ENDP

;------------------------------------------------------------------------------
; Statistics and monitoring functions
;------------------------------------------------------------------------------

;------------------------------------------------------------------------------
; promisc_asm_get_stats - Get ASM-level statistics
;
; Returns performance counters for ASM-level operations
;------------------------------------------------------------------------------
promisc_asm_get_stats PROC NEAR
        push    bx
        
        ; Return stats in registers
        mov     eax, asm_packets_captured
        mov     ebx, asm_capture_errors
        
        pop     bx
        ret
promisc_asm_get_stats ENDP

;------------------------------------------------------------------------------
; promisc_asm_reset_stats - Reset ASM-level statistics
;------------------------------------------------------------------------------
promisc_asm_reset_stats PROC NEAR
        mov     asm_packets_captured, 0
        mov     asm_capture_errors, 0
        ret
promisc_asm_reset_stats ENDP

_TEXT ENDS

END
