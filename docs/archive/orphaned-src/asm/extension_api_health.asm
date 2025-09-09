;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; @file extension_api_health.asm
;; @brief Health status byte addition for AH=80h
;;
;; Adds compact health status to vendor discovery for quick status checks
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

        .8086
        .model small
        .data

; Health status codes (1 byte)
HEALTH_OK           equ     00h     ; All systems operational
HEALTH_PIO_FORCED   equ     01h     ; Running in PIO mode (safe)
HEALTH_DEGRADED     equ     02h     ; Running with limitations
HEALTH_TEST_MODE    equ     03h     ; Bus master test in progress
HEALTH_FAILED       equ     0FFh    ; Critical failure detected

; Current health status
health_status       db      HEALTH_PIO_FORCED   ; Default to safe mode

        .code

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Enhanced AH=80h handler with health status
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
vendor_discovery_enhanced:
        push    si
        
        ; Load standard discovery data
        mov     si, offset extension_snapshot
        mov     ax, [si]                ; AX = signature '3C'
        mov     bx, [si+2]              ; BX = version
        mov     cx, [si+4]              ; CX = max function
        mov     dx, [si+6]              ; DX = capabilities
        
        ; Add health status in DH (high byte of DX)
        mov     dh, [health_status]     ; DH = health byte
        ; DL still has capability flags (low byte)
        
        pop     si
        clc                             ; Success
        ret

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Health status update functions
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

; Called when system is fully operational
set_health_ok proc near
        push    ax
        mov     al, HEALTH_OK
        mov     [health_status], al
        pop     ax
        ret
set_health_ok endp

; Called when PIO mode is forced
set_health_pio proc near
        push    ax
        mov     al, HEALTH_PIO_FORCED
        mov     [health_status], al
        pop     ax
        ret
set_health_pio endp

; Called when running with limitations
set_health_degraded proc near
        push    ax
        mov     al, HEALTH_DEGRADED
        mov     [health_status], al
        pop     ax
        ret
set_health_degraded endp

; Called during bus master testing
set_health_test_mode proc near
        push    ax
        mov     al, HEALTH_TEST_MODE
        mov     [health_status], al
        pop     ax
        ret
set_health_test_mode endp

; Called on critical failure
set_health_failed proc near
        push    ax
        mov     al, HEALTH_FAILED
        mov     [health_status], al
        
        ; Also set kill switch in safety snapshot
        or      word ptr [safety_snapshot], 8000h
        
        pop     ax
        ret
set_health_failed endp

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Health status decision logic
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
update_health_status proc near
        push    ax
        push    bx
        
        ; Check critical failures first
        test    word ptr [safety_snapshot], 8000h  ; Kill switch?
        jnz     .failed
        
        ; Check if in test mode
        cmp     byte ptr [busmaster_test_active], 1
        je      .test_mode
        
        ; Check if PIO forced
        test    word ptr [safety_snapshot], 0001h  ; PIO forced?
        jnz     .pio_forced
        
        ; Check if DMA validated
        test    word ptr [safety_snapshot], 0020h  ; DMA validated?
        jz      .degraded                          ; No DMA = degraded
        
        ; Check patch status
        test    word ptr [safety_snapshot], 0002h  ; Patches OK?
        jz      .degraded
        
        ; All good
        mov     al, HEALTH_OK
        jmp     .update
        
.failed:
        mov     al, HEALTH_FAILED
        jmp     .update
        
.test_mode:
        mov     al, HEALTH_TEST_MODE
        jmp     .update
        
.pio_forced:
        mov     al, HEALTH_PIO_FORCED
        jmp     .update
        
.degraded:
        mov     al, HEALTH_DEGRADED
        
.update:
        mov     [health_status], al
        
        pop     bx
        pop     ax
        ret
update_health_status endp

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Integration with Stage 1
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

; Called when Stage 1 bus master test starts
stage1_test_start proc near
        call    set_health_test_mode
        call    update_snapshot_safe
        ret
stage1_test_start endp

; Called when Stage 1 test completes successfully
stage1_test_pass proc near
        ; Update safety flags
        or      word ptr [safety_snapshot], 0020h  ; Set DMA validated
        
        ; Clear PIO forced if test passed
        and     word ptr [safety_snapshot], 0FFFEh
        
        ; Update health
        call    set_health_ok
        call    update_snapshot_safe
        ret
stage1_test_pass endp

; Called when Stage 1 test fails
stage1_test_fail proc near
        ; Force PIO mode
        or      word ptr [safety_snapshot], 0001h
        
        ; Set degraded health
        call    set_health_degraded
        call    update_snapshot_safe
        ret
stage1_test_fail endp

        end

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Size impact: +1 byte data, ~50 bytes code (mostly cold)
;; 
;; Usage by tools:
;;   mov ah, 80h
;;   int 60h
;;   ; DH now contains health status
;;   cmp dh, HEALTH_OK
;;   je  system_healthy
;;