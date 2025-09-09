;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; @file loopback.asm
;; @brief Cold section loopback implementation for 3C515
;;
;; Implements MAC internal loopback that exercises RX DMA descriptors
;; Called from hot section trampoline, executes in cold section
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

        .8086
        .model small
        .code

        public  cold_loopback_impl
        
        ; External references
        extern  nic_io_base:word
        extern  loopback_enabled:byte
        extern  saved_auto_neg:byte
        
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; 3C515 Register definitions
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
CMD_REG             equ     0Eh     ; Command register
WINDOW_CMD          equ     0800h   ; Window select command base
MAC_CTRL_REG        equ     06h     ; MAC control register (Window 3)
PHY_MGMT_REG        equ     08h     ; PHY management (Window 4)

; MAC Control bits (Window 3, offset 6)
MAC_LOOPBACK_BIT    equ     4000h   ; Bit 14: Internal loopback
MAC_FULL_DUPLEX     equ     0020h   ; Bit 5: Full duplex

; PHY bits (Window 4, offset 8)
PHY_AUTO_NEG        equ     1000h   ; Auto-negotiation enable

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Cold loopback implementation
;; AL = 0 to disable, 1 to enable
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
cold_loopback_impl proc far
        push    dx
        push    cx
        push    bx
        
        mov     dx, cs:[nic_io_base]
        
        cmp     al, 1
        je      .enable_loopback
        
.disable_loopback:
        ; Window 3 for MAC control
        mov     ax, WINDOW_CMD or 3
        add     dx, CMD_REG
        out     dx, ax
        sub     dx, CMD_REG
        
        ; Clear loopback bit
        add     dx, MAC_CTRL_REG
        in      ax, dx
        and     ax, not MAC_LOOPBACK_BIT
        out     dx, ax
        sub     dx, MAC_CTRL_REG
        
        ; Restore auto-negotiation if it was enabled
        cmp     byte ptr cs:[saved_auto_neg], 0
        je      .no_restore_autoneg
        
        ; Window 4 for PHY
        mov     ax, WINDOW_CMD or 4
        add     dx, CMD_REG
        out     dx, ax
        sub     dx, CMD_REG
        
        add     dx, PHY_MGMT_REG
        in      ax, dx
        or      ax, PHY_AUTO_NEG
        out     dx, ax
        
.no_restore_autoneg:
        ; Mark disabled
        mov     byte ptr cs:[loopback_enabled], 0
        
        ; Return to Window 1 (operational)
        mov     dx, cs:[nic_io_base]
        add     dx, CMD_REG
        mov     ax, WINDOW_CMD or 1
        out     dx, ax
        
        xor     ax, ax          ; Success
        jmp     .done
        
.enable_loopback:
        ; First save auto-negotiation state
        ; Window 4 for PHY
        mov     ax, WINDOW_CMD or 4
        add     dx, CMD_REG
        out     dx, ax
        sub     dx, CMD_REG
        
        add     dx, PHY_MGMT_REG
        in      ax, dx
        test    ax, PHY_AUTO_NEG
        setnz   byte ptr cs:[saved_auto_neg]
        
        ; Disable auto-negotiation for loopback
        and     ax, not PHY_AUTO_NEG
        out     dx, ax
        sub     dx, PHY_MGMT_REG
        
        ; Window 3 for MAC control
        mov     ax, WINDOW_CMD or 3
        add     dx, CMD_REG
        out     dx, ax
        sub     dx, CMD_REG
        
        ; Set loopback bit and force full duplex
        add     dx, MAC_CTRL_REG
        in      ax, dx
        or      ax, MAC_LOOPBACK_BIT or MAC_FULL_DUPLEX
        out     dx, ax
        
        ; Small delay to let MAC settle
        mov     cx, 100
.delay:
        loop    .delay
        
        ; Verify RX ring can receive in loopback
        ; This is done by checking RX status register
        sub     dx, MAC_CTRL_REG
        add     dx, 0Ch         ; RX status register
        in      ax, dx
        test    ax, 8000h       ; Check if RX is enabled
        jz      .loopback_failed
        
        ; Mark enabled
        mov     byte ptr cs:[loopback_enabled], 1
        
        ; Return to Window 1
        mov     dx, cs:[nic_io_base]
        add     dx, CMD_REG
        mov     ax, WINDOW_CMD or 1
        out     dx, ax
        
        xor     ax, ax          ; Success
        jmp     .done
        
.loopback_failed:
        ; Loopback set but RX not working - PHY only loopback
        ; Clear loopback bit
        mov     dx, cs:[nic_io_base]
        add     dx, MAC_CTRL_REG
        in      ax, dx
        and     ax, not MAC_LOOPBACK_BIT
        out     dx, ax
        
        mov     ax, 7009h       ; Loopback not supported
        
.done:
        pop     bx
        pop     cx
        pop     dx
        retf
cold_loopback_impl endp

        end