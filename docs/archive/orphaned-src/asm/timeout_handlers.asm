; @file timeout_handlers.asm
; @brief Hardware timeout protection and recovery handlers
;
; 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
;
; This file implements comprehensive timeout protection for all hardware
; operations to prevent hangs and deadlocks. Includes exponential backoff
; retry mechanisms and automatic recovery procedures.
;
; Based on defensive programming patterns from defensive_integration.asm
; and Linux driver timeout strategies.

.MODEL SMALL
.386

include 'tsr_defensive.inc'
include 'timing_macros.inc'

; Export timeout handler functions
PUBLIC timeout_init
PUBLIC timeout_cleanup
PUBLIC timeout_set_operation
PUBLIC timeout_check_expired
PUBLIC timeout_reset
PUBLIC timeout_hardware_io
PUBLIC timeout_wait_ready
PUBLIC timeout_dma_complete
PUBLIC timeout_interrupt_wait
PUBLIC retry_with_backoff
PUBLIC get_system_ticks

; Data segment
_DATA SEGMENT
        ASSUME  DS:_DATA

; Timeout configuration constants
TIMEOUT_IO_DEFAULT         equ 100      ; Default I/O timeout (ticks)
TIMEOUT_DMA_DEFAULT        equ 200      ; Default DMA timeout (ticks)
TIMEOUT_INTERRUPT_DEFAULT  equ 50       ; Default interrupt timeout (ticks)
TIMEOUT_RESET_DEFAULT      equ 500      ; Default reset timeout (ticks)

; Maximum retry attempts with exponential backoff
MAX_RETRY_ATTEMPTS         equ 5
BASE_RETRY_DELAY          equ 2        ; Base delay in ticks
MAX_RETRY_DELAY           equ 100      ; Maximum delay in ticks

; Timeout tracking structure (per operation type)
timeout_tracker STRUC
    start_time      dd ?    ; Start time in BIOS ticks
    timeout_value   dw ?    ; Timeout value in ticks
    operation_type  db ?    ; Operation type (IO, DMA, etc.)
    nic_index      db ?    ; NIC index for multi-adapter support
    retry_count    db ?    ; Current retry attempt
    last_error     db ?    ; Last error code
    flags          db ?    ; Status flags
    reserved       db ?    ; Alignment padding
timeout_tracker ENDS

; Operation type constants
OP_TYPE_IO         equ 1
OP_TYPE_DMA        equ 2
OP_TYPE_INTERRUPT  equ 3
OP_TYPE_RESET      equ 4
OP_TYPE_EEPROM     equ 5

; Status flags
TIMEOUT_FLAG_ACTIVE     equ 01h
TIMEOUT_FLAG_EXPIRED    equ 02h
TIMEOUT_FLAG_RETRYING   equ 04h
TIMEOUT_FLAG_FAILED     equ 08h

; Global timeout state
timeout_initialized     db 0
timeout_enabled         db 1

; Timeout tracking for multiple concurrent operations (max 8)
MAX_TIMEOUT_TRACKERS    equ 8
timeout_trackers        timeout_tracker MAX_TIMEOUT_TRACKERS dup(<>)
active_tracker_mask     db 0           ; Bitmask of active trackers

; Statistics and monitoring
timeout_stats STRUC
    total_operations    dd 0    ; Total operations tracked
    timeouts_detected   dd 0    ; Timeouts detected
    retries_attempted   dd 0    ; Retry attempts
    recovery_success    dd 0    ; Successful recoveries
    recovery_failures   dd 0    ; Failed recoveries
timeout_stats ENDS

global_timeout_stats    timeout_stats <>

; Error recovery function pointers (set by C code)
recovery_soft_reset     dd 0    ; Soft reset function
recovery_hard_reset     dd 0    ; Hard reset function  
recovery_adapter_disable dd 0   ; Adapter disable function

_DATA ENDS

; Code segment
_TEXT SEGMENT
        ASSUME  CS:_TEXT, DS:_DATA

;------------------------------------------------------------------------------
; timeout_init - Initialize timeout handling system
; 
; Input:  None
; Output: AL = 0 on success, AL = error code on failure
; Modifies: AX, BX, CX, DX
;------------------------------------------------------------------------------
timeout_init PROC
        SAFE_CLI                    ; Disable interrupts during init
        
        ; Check if already initialized
        cmp     timeout_initialized, 1
        je      init_success
        
        ; Clear all timeout trackers
        mov     cx, MAX_TIMEOUT_TRACKERS
        mov     bx, 0
        
clear_trackers:
        push    cx
        push    bx
        
        ; Calculate tracker offset
        mov     ax, SIZE timeout_tracker
        mul     bx
        lea     si, timeout_trackers[ax]
        
        ; Clear the tracker structure
        mov     cx, SIZE timeout_tracker
        xor     ax, ax
        mov     di, si
        rep     stosb
        
        pop     bx
        pop     cx
        inc     bx
        loop    clear_trackers
        
        ; Clear active tracker mask
        mov     active_tracker_mask, 0
        
        ; Initialize statistics
        mov     cx, SIZE timeout_stats / 2
        lea     di, global_timeout_stats
        xor     ax, ax
        rep     stosw
        
        mov     timeout_initialized, 1
        
init_success:
        SAFE_STI                    ; Re-enable interrupts
        xor     al, al              ; Success
        ret

init_error:
        SAFE_STI                    ; Re-enable interrupts
        mov     al, 1               ; Error
        ret
timeout_init ENDP

;------------------------------------------------------------------------------
; timeout_cleanup - Cleanup timeout handling system
;
; Input:  None
; Output: None
; Modifies: AX, CX, DI
;------------------------------------------------------------------------------
timeout_cleanup PROC
        SAFE_CLI                    ; Disable interrupts
        
        ; Cancel all active timeouts
        mov     cx, MAX_TIMEOUT_TRACKERS
        mov     bx, 0
        
cancel_loop:
        push    cx
        push    bx
        
        ; Check if tracker is active
        mov     cl, bl
        mov     al, 1
        shl     al, cl
        test    active_tracker_mask, al
        jz      next_cancel
        
        ; Clear the active tracker
        and     active_tracker_mask, NOT al
        
        ; Clear tracker structure
        mov     ax, SIZE timeout_tracker
        mul     bx
        lea     di, timeout_trackers[ax]
        mov     cx, SIZE timeout_tracker
        xor     ax, ax
        rep     stosb
        
next_cancel:
        pop     bx
        pop     cx
        inc     bx
        loop    cancel_loop
        
        mov     timeout_initialized, 0
        
        SAFE_STI                    ; Re-enable interrupts
        ret
timeout_cleanup ENDP

;------------------------------------------------------------------------------
; timeout_set_operation - Start timeout tracking for an operation
;
; Input:  AL = operation type, BL = NIC index, CX = timeout value (ticks)
; Output: AL = tracker index (0-7), or 0FFh if no trackers available
; Modifies: AX, BX, CX, DX, SI
;------------------------------------------------------------------------------
timeout_set_operation PROC
        push    es
        push    di
        
        SAFE_CLI                    ; Atomic operation
        
        ; Find available tracker
        mov     dl, active_tracker_mask
        mov     dh, 0               ; Tracker index
        mov     cl, 1               ; Bit mask
        
find_tracker:
        test    dl, cl              ; Check if tracker is free
        jz      found_tracker       ; If bit is clear, tracker is free
        
        inc     dh                  ; Next tracker index
        shl     cl, 1               ; Next bit
        cmp     dh, MAX_TIMEOUT_TRACKERS
        jb      find_tracker
        
        ; No free trackers
        SAFE_STI
        mov     al, 0FFh            ; Error - no trackers available
        jmp     timeout_set_done
        
found_tracker:
        ; Mark tracker as active
        or      active_tracker_mask, cl
        
        ; Calculate tracker offset
        push    ax                  ; Save operation type
        mov     al, dh
        mov     bl, SIZE timeout_tracker
        mul     bl
        lea     si, timeout_trackers[ax]
        pop     ax                  ; Restore operation type
        
        ; Get current system time
        call    get_system_ticks
        mov     [si].timeout_tracker.start_time, eax
        
        ; Set operation parameters
        mov     [si].timeout_tracker.operation_type, al
        mov     [si].timeout_tracker.nic_index, bl
        mov     [si].timeout_tracker.timeout_value, cx
        mov     [si].timeout_tracker.retry_count, 0
        mov     [si].timeout_tracker.last_error, 0
        mov     [si].timeout_tracker.flags, TIMEOUT_FLAG_ACTIVE
        
        ; Update statistics
        inc     global_timeout_stats.total_operations
        
        SAFE_STI
        
        ; Return tracker index
        mov     al, dh
        
timeout_set_done:
        pop     di
        pop     es
        ret
timeout_set_operation ENDP

;------------------------------------------------------------------------------
; timeout_check_expired - Check if operation has timed out
;
; Input:  AL = tracker index
; Output: AL = 1 if expired, 0 if not expired, 0FFh if invalid tracker
; Modifies: AX, BX, CX, DX, SI
;------------------------------------------------------------------------------
timeout_check_expired PROC
        ; Validate tracker index
        cmp     al, MAX_TIMEOUT_TRACKERS
        jae     check_invalid
        
        ; Check if tracker is active
        mov     cl, al
        mov     bl, 1
        shl     bl, cl
        test    active_tracker_mask, bl
        jz      check_invalid
        
        ; Calculate tracker offset
        mov     bl, SIZE timeout_tracker
        mul     bl
        lea     si, timeout_trackers[ax]
        
        ; Check if already marked as expired
        test    [si].timeout_tracker.flags, TIMEOUT_FLAG_EXPIRED
        jnz     check_expired
        
        ; Get current time
        call    get_system_ticks
        
        ; Calculate elapsed time
        sub     eax, [si].timeout_tracker.start_time
        
        ; Handle midnight rollover (BIOS timer resets at midnight)
        jnc     no_rollover
        ; If current time < start time, we crossed midnight
        ; Add the maximum timer value to get correct elapsed time
        add     eax, 0x1800B0       ; Ticks per day
        
no_rollover:
        ; Compare with timeout value
        movzx   ebx, [si].timeout_tracker.timeout_value
        cmp     eax, ebx
        jb      check_not_expired
        
        ; Timeout detected
        or      [si].timeout_tracker.flags, TIMEOUT_FLAG_EXPIRED
        inc     global_timeout_stats.timeouts_detected
        
check_expired:
        mov     al, 1               ; Expired
        ret
        
check_not_expired:
        xor     al, al              ; Not expired
        ret
        
check_invalid:
        mov     al, 0FFh            ; Invalid tracker
        ret
timeout_check_expired ENDP

;------------------------------------------------------------------------------
; timeout_reset - Reset/clear a timeout tracker
;
; Input:  AL = tracker index
; Output: AL = 0 on success, 0FFh on error
; Modifies: AX, BX, CX, SI, DI
;------------------------------------------------------------------------------
timeout_reset PROC
        ; Validate tracker index
        cmp     al, MAX_TIMEOUT_TRACKERS
        jae     reset_error
        
        SAFE_CLI                    ; Atomic operation
        
        ; Check if tracker is active
        mov     cl, al
        mov     bl, 1
        shl     bl, cl
        test    active_tracker_mask, bl
        jz      reset_not_active
        
        ; Clear active bit
        not     bl
        and     active_tracker_mask, bl
        
        ; Clear tracker structure
        mov     bl, SIZE timeout_tracker
        mul     bl
        lea     si, timeout_trackers[ax]
        
        mov     cx, SIZE timeout_tracker
        xor     ax, ax
        mov     di, si
        rep     stosb
        
        SAFE_STI
        xor     al, al              ; Success
        ret
        
reset_not_active:
        SAFE_STI
        
reset_error:
        mov     al, 0FFh            ; Error
        ret
timeout_reset ENDP

;------------------------------------------------------------------------------
; timeout_hardware_io - Protected hardware I/O with timeout
;
; Input:  DX = I/O port, AL = operation (0=read, 1=write), BX = data (for write)
;         CL = NIC index, CH = timeout multiplier (0 = default)
; Output: AX = data (for read) or status (0=success, error code for timeout)
;         CF = set on timeout/error
; Modifies: AX, BX, CX, DX
;------------------------------------------------------------------------------
timeout_hardware_io PROC
        push    si
        push    di
        
        ; Set default timeout if not specified
        test    ch, ch
        jnz     use_custom_timeout
        mov     ch, 1               ; Use default multiplier
        
use_custom_timeout:
        ; Calculate timeout value
        movzx   cx, ch
        mov     bx, TIMEOUT_IO_DEFAULT
        mul     bx                  ; AX = timeout ticks
        mov     cx, ax
        
        ; Start timeout tracking
        push    ax                  ; Save timeout value
        push    bx                  ; Save data
        push    dx                  ; Save port
        
        mov     al, OP_TYPE_IO      ; Operation type
        mov     bl, cl              ; NIC index
        call    timeout_set_operation
        
        pop     dx                  ; Restore port
        pop     bx                  ; Restore data
        mov     si, ax              ; Save tracker index
        pop     ax                  ; Restore timeout value
        
        cmp     si, 0FFh            ; Check if tracker allocation failed
        je      io_no_tracker
        
        ; Perform the I/O operation
        test    al, al              ; Check operation type (read/write)
        jnz     do_write
        
do_read:
        in      ax, dx              ; Read from port
        jmp     io_success
        
do_write:
        mov     ax, bx              ; Get data to write
        out     dx, ax              ; Write to port
        
io_success:
        push    ax                  ; Save result
        
        ; Clear timeout tracker
        mov     al, sil             ; Tracker index
        call    timeout_reset
        
        pop     ax                  ; Restore result
        clc                         ; Clear carry flag (success)
        jmp     io_done
        
io_no_tracker:
        mov     ax, 0FFh            ; Error - no tracker available
        stc                         ; Set carry flag (error)
        
io_done:
        pop     di
        pop     si
        ret
timeout_hardware_io ENDP

;------------------------------------------------------------------------------
; timeout_wait_ready - Wait for hardware ready with timeout protection
;
; Input:  DX = status register port, AL = ready mask, BL = NIC index
;         CX = timeout value (0 = default)
; Output: AL = 0 on success (ready), error code on timeout
;         CF = set on timeout
; Modifies: AX, BX, CX, DX
;------------------------------------------------------------------------------
timeout_wait_ready PROC
        push    si
        
        ; Set default timeout if not specified
        test    cx, cx
        jnz     wait_use_timeout
        mov     cx, TIMEOUT_IO_DEFAULT
        
wait_use_timeout:
        ; Start timeout tracking
        push    ax                  ; Save ready mask
        push    dx                  ; Save port
        
        mov     al, OP_TYPE_IO      ; Operation type
        call    timeout_set_operation
        mov     si, ax              ; Save tracker index
        
        pop     dx                  ; Restore port
        pop     ax                  ; Restore ready mask
        
        cmp     si, 0FFh            ; Check tracker allocation
        je      wait_no_tracker
        
        ; Wait loop with timeout checking
wait_loop:
        ; Read status register
        push    ax                  ; Save ready mask
        in      ax, dx
        mov     bx, ax              ; Save status
        pop     ax                  ; Restore ready mask
        
        ; Check if ready condition met
        and     bx, ax              ; Mask the status
        cmp     bx, ax              ; Check if all required bits are set
        je      wait_ready
        
        ; Check for timeout
        push    ax                  ; Save ready mask
        mov     al, sil             ; Tracker index
        call    timeout_check_expired
        pop     bx                  ; Restore ready mask (in BX now)
        
        test    al, al              ; Check timeout status
        jz      wait_continue       ; Continue if not timed out
        
        ; Timeout detected
        mov     al, sil             ; Tracker index
        call    timeout_reset       ; Clear tracker
        
        mov     ax, 0FFh            ; Timeout error
        stc                         ; Set carry flag
        jmp     wait_done
        
wait_continue:
        mov     ax, bx              ; Restore ready mask
        jmp     wait_loop
        
wait_ready:
        ; Success - clear tracker
        mov     al, sil             ; Tracker index
        call    timeout_reset
        
        xor     ax, ax              ; Success
        clc                         ; Clear carry flag
        jmp     wait_done
        
wait_no_tracker:
        mov     ax, 0FEh            ; No tracker error
        stc                         ; Set carry flag
        
wait_done:
        pop     si
        ret
timeout_wait_ready ENDP

;------------------------------------------------------------------------------
; timeout_dma_complete - Wait for DMA completion with timeout
;
; Input:  DX = DMA status register, AL = completion mask, BL = NIC index
;         CX = timeout value (0 = default)
; Output: AL = 0 on success, error code on timeout/failure
;         CF = set on timeout/error
; Modifies: AX, BX, CX, DX
;------------------------------------------------------------------------------
timeout_dma_complete PROC
        push    si
        
        ; Set default timeout if not specified
        test    cx, cx
        jnz     dma_use_timeout
        mov     cx, TIMEOUT_DMA_DEFAULT
        
dma_use_timeout:
        ; Start timeout tracking
        push    ax                  ; Save completion mask
        push    dx                  ; Save port
        
        mov     al, OP_TYPE_DMA     ; Operation type
        call    timeout_set_operation
        mov     si, ax              ; Save tracker index
        
        pop     dx                  ; Restore port
        pop     ax                  ; Restore completion mask
        
        cmp     si, 0FFh            ; Check tracker allocation
        je      dma_no_tracker
        
        ; Wait for DMA completion
dma_wait_loop:
        ; Read DMA status
        push    ax                  ; Save completion mask
        in      ax, dx
        mov     bx, ax              ; Save status
        pop     ax                  ; Restore completion mask
        
        ; Check completion condition
        test    bx, ax              ; Check if completion bits are set
        jnz     dma_complete
        
        ; Check for timeout
        push    ax                  ; Save completion mask
        mov     al, sil             ; Tracker index
        call    timeout_check_expired
        pop     bx                  ; Restore completion mask
        
        test    al, al              ; Check timeout
        jz      dma_continue        ; Continue if not timed out
        
        ; DMA timeout
        mov     al, sil             ; Tracker index
        call    timeout_reset
        
        mov     ax, 0FDh            ; DMA timeout error
        stc                         ; Set carry flag
        jmp     dma_done
        
dma_continue:
        mov     ax, bx              ; Restore completion mask
        jmp     dma_wait_loop
        
dma_complete:
        ; DMA completed successfully
        mov     al, sil             ; Tracker index
        call    timeout_reset
        
        xor     ax, ax              ; Success
        clc                         ; Clear carry flag
        jmp     dma_done
        
dma_no_tracker:
        mov     ax, 0FEh            ; No tracker error
        stc                         ; Set carry flag
        
dma_done:
        pop     si
        ret
timeout_dma_complete ENDP

;------------------------------------------------------------------------------
; retry_with_backoff - Implement exponential backoff retry logic
;
; Input:  AL = tracker index, BL = error code
; Output: AL = 0 to continue retry, 1 to give up
;         CX = delay ticks for this retry
; Modifies: AX, BX, CX, DX, SI
;------------------------------------------------------------------------------
retry_with_backoff PROC
        push    di
        
        ; Validate tracker index
        cmp     al, MAX_TIMEOUT_TRACKERS
        jae     retry_invalid
        
        ; Calculate tracker offset
        mov     cl, SIZE timeout_tracker
        mul     cl
        lea     si, timeout_trackers[ax]
        
        ; Check if we've exceeded max retry attempts
        mov     al, [si].timeout_tracker.retry_count
        cmp     al, MAX_RETRY_ATTEMPTS
        jae     retry_give_up
        
        ; Increment retry count
        inc     [si].timeout_tracker.retry_count
        mov     [si].timeout_tracker.last_error, bl
        or      [si].timeout_tracker.flags, TIMEOUT_FLAG_RETRYING
        
        ; Calculate exponential backoff delay
        ; delay = BASE_RETRY_DELAY * (2 ^ retry_count)
        mov     cx, BASE_RETRY_DELAY
        mov     al, [si].timeout_tracker.retry_count
        test    al, al
        jz      retry_no_backoff    ; First retry, no delay
        
        ; Calculate 2^retry_count
        mov     bx, 1
backoff_loop:
        shl     bx, 1               ; Multiply by 2
        dec     al
        jnz     backoff_loop
        
        ; Multiply base delay by backoff factor
        mov     ax, cx
        mul     bx
        mov     cx, ax
        
        ; Cap at maximum delay
        cmp     cx, MAX_RETRY_DELAY
        jbe     retry_no_backoff
        mov     cx, MAX_RETRY_DELAY
        
retry_no_backoff:
        ; Update statistics
        inc     global_timeout_stats.retries_attempted
        
        ; Reset timeout tracker for next attempt
        call    get_system_ticks
        mov     [si].timeout_tracker.start_time, eax
        and     [si].timeout_tracker.flags, NOT TIMEOUT_FLAG_EXPIRED
        
        xor     al, al              ; Continue retrying
        jmp     retry_done
        
retry_give_up:
        ; Mark as failed
        or      [si].timeout_tracker.flags, TIMEOUT_FLAG_FAILED
        inc     global_timeout_stats.recovery_failures
        
        mov     al, 1               ; Give up
        xor     cx, cx              ; No delay
        jmp     retry_done
        
retry_invalid:
        mov     al, 0FFh            ; Invalid
        xor     cx, cx              ; No delay
        
retry_done:
        pop     di
        ret
retry_with_backoff ENDP

;------------------------------------------------------------------------------
; get_system_ticks - Get current BIOS timer tick count
;
; Input:  None
; Output: EAX = current timer ticks since midnight
; Modifies: EAX, CX, DX
;------------------------------------------------------------------------------
get_system_ticks PROC
        push    bx
        
        ; Use DOS INT 1Ah to get system timer
        mov     ah, 00h             ; Get system timer count
        int     1Ah                 ; BIOS timer interrupt
        
        ; Combine CX:DX into 32-bit value in EAX
        mov     ax, dx              ; Low word
        shl     ecx, 16             ; Shift high word
        mov     cx, ax              ; Combine
        mov     eax, ecx            ; Result in EAX
        
        pop     bx
        ret
get_system_ticks ENDP

_TEXT ENDS

END