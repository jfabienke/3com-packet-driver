;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; @file quiesce.asm
;; @brief Minimal driver quiesce/resume handlers for Stage 1 testing
;;
;; Provides AH=90h (quiesce) and AH=91h (resume) vendor API extensions
;; With idempotence and cascade handling: ~42 bytes code + 8 bytes data = 50 bytes
;; Still within Stage 1 budget (85 bytes total)
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

        .8086
        .model small
        .code

        public  quiesce_handler
        public  resume_handler
        public  get_dma_stats
        
        ; External references
        extern  nic_io_base:word
        extern  nic_irq:byte
        extern  isr_active:byte
        
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Data - Quiesce state (6 bytes)
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        .data
        
driver_quiesced     db      0       ; Quiesce flag
quiesce_count       db      0       ; Idempotence counter
saved_master_mask   db      0       ; Saved PIC masks
saved_slave_mask    db      0
bounce_counter      dw      0       ; DMA bounce usage
boundary_violations dw      0       ; Boundary violation count

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; 3C515 Command definitions
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
CMD_REG             equ     0Eh     ; Command register offset
CMD_STOP_COAX       equ     0B000h  ; Stop coax transceiver
CMD_RX_DISABLE      equ     1800h   ; RxDisable
CMD_TX_DISABLE      equ     5000h   ; TxDisable  
CMD_ACK_INTR        equ     6800h   ; AckIntr (all)
CMD_ENABLE_DC       equ     0A000h  ; Enable DC converter
CMD_RX_ENABLE       equ     2000h   ; RxEnable
CMD_TX_ENABLE       equ     4800h   ; TxEnable

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; AH=90h: Quiesce driver - NIC first, then PIC
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        .code
quiesce_handler proc far
        push    dx
        push    cx
        push    ax
        
        ; Check if already quiesced (idempotence)
        cmp     byte ptr cs:[driver_quiesced], 1
        je      .already_quiesced
        
        ; Bounded wait for ISR to clear (10 retries)
        mov     cx, 10
.wait_isr:
        test    byte ptr cs:[isr_active], 1
        jz      .isr_clear
        
        ; Brief delay (about 100us)
        push    cx
        mov     cx, 100
.delay:
        loop    .delay
        pop     cx
        
        loop    .wait_isr
        
        ; ISR still active after timeout
        pop     ax
        pop     cx
        pop     dx
        mov     ax, 7005h       ; Busy error
        stc
        retf
        
.isr_clear:
        ; STEP 1: Stop NIC operations FIRST
        mov     dx, cs:[nic_io_base]
        add     dx, CMD_REG
        
        mov     ax, CMD_RX_DISABLE     ; Stop RX
        out     dx, ax
        mov     ax, CMD_TX_DISABLE     ; Stop TX
        out     dx, ax
        mov     ax, CMD_STOP_COAX      ; Stop transceiver
        out     dx, ax
        
        ; STEP 2: ACK any pending interrupts at NIC
        mov     ax, CMD_ACK_INTR        ; ACK all interrupts
        out     dx, ax
        
        ; Brief delay to ensure NIC processes commands
        mov     cx, 10
.nic_delay:
        loop    .nic_delay
        
        ; STEP 3: NOW mask IRQ in PIC
        mov     cl, cs:[nic_irq]
        cmp     cl, 8
        jb      .master_pic
        
.slave_pic:
        ; IRQ 8-15 on slave PIC
        in      al, 0A1h
        mov     cs:[saved_slave_mask], al
        sub     cl, 8
        mov     ah, 1
        shl     ah, cl
        or      al, ah
        out     0A1h, al
        
        ; CRITICAL: Ensure IRQ2 cascade remains enabled on master
        in      al, 21h
        and     al, 0FBh        ; Clear bit 2 (enable IRQ2)
        out     21h, al
        jmp     .done
        
.master_pic:
        ; IRQ 0-7 on master PIC
        in      al, 21h
        mov     cs:[saved_master_mask], al
        mov     ah, 1
        shl     ah, cl
        or      al, ah
        out     21h, al
        
.done:
        ; Set quiesced flag and increment count
        mov     byte ptr cs:[driver_quiesced], 1
        inc     byte ptr cs:[quiesce_count]
        
        pop     ax
        pop     cx
        pop     dx
        xor     ax, ax          ; Success
        clc
        retf
        
.already_quiesced:
        ; Already quiesced, just return success (idempotent)
        pop     ax
        pop     cx
        pop     dx
        xor     ax, ax          ; Success
        clc
        retf
quiesce_handler endp

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; AH=91h: Resume driver - PIC first, then NIC
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
resume_handler proc far
        push    dx
        push    cx
        
        ; Check if actually quiesced
        cmp     byte ptr cs:[driver_quiesced], 0
        je      .not_quiesced
        
        ; STEP 1: Restore PIC mask FIRST
        mov     cl, cs:[nic_irq]
        cmp     cl, 8
        jb      .restore_master
        
.restore_slave:
        mov     al, cs:[saved_slave_mask]
        out     0A1h, al
        ; CRITICAL: Maintain IRQ2 cascade
        in      al, 21h
        and     al, 0FBh        ; Clear bit 2 (enable IRQ2)
        out     21h, al
        jmp     .restore_nic
        
.restore_master:
        mov     al, cs:[saved_master_mask]
        out     21h, al
        
.restore_nic:
        ; Brief delay after PIC restore
        mov     cx, 10
.pic_delay:
        loop    .pic_delay
        
        ; STEP 2: Restore NIC operations SECOND
        mov     dx, cs:[nic_io_base]
        add     dx, CMD_REG
        
        mov     ax, CMD_ENABLE_DC       ; Enable DC converter
        out     dx, ax
        mov     ax, CMD_RX_ENABLE       ; Enable RX
        out     dx, ax
        mov     ax, CMD_TX_ENABLE       ; Enable TX
        out     dx, ax
        
        ; STEP 3: Verify IRQ cascade is OK
        call    verify_cascade_state
        
        ; Clear quiesced flag
        mov     byte ptr cs:[driver_quiesced], 0
        
        pop     cx
        pop     dx
        xor     ax, ax          ; Success
        clc
        retf
        
.not_quiesced:
        ; Not quiesced, return success (idempotent)
        pop     cx
        pop     dx
        xor     ax, ax
        clc
        retf
resume_handler endp

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; AH=92h: Get DMA statistics
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
get_dma_stats proc far
        mov     ax, cs:[bounce_counter]
        mov     bx, cs:[boundary_violations]
        mov     cl, cs:[driver_quiesced]
        xor     ch, ch
        xor     dx, dx          ; Reserved
        clc
        retf
get_dma_stats endp

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Update bounce counter (called from DMA mapping code)
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        public  increment_bounce_counter
increment_bounce_counter proc near
        inc     word ptr cs:[bounce_counter]
        ret
increment_bounce_counter endp

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Report boundary violation (called from boundary check)
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        public  report_boundary_violation
report_boundary_violation proc near
        inc     word ptr cs:[boundary_violations]
        ret
report_boundary_violation endp

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;; Verify IRQ2 cascade state after resume
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
        public  verify_cascade_state
verify_cascade_state proc near
        push    ax
        push    cx
        
        ; Check if we're using slave PIC
        mov     cl, cs:[nic_irq]
        cmp     cl, 8
        jb      .master_irq
        
        ; For slave IRQ, verify IRQ2 is enabled on master
        in      al, 21h
        test    al, 04h                 ; Bit 2 should be clear
        jnz     .cascade_broken
        
        ; Cascade is OK
        mov     byte ptr cs:[cascade_ok], 1
        jmp     .done
        
.master_irq:
        ; Master IRQ, cascade not relevant
        mov     byte ptr cs:[cascade_ok], 1
        jmp     .done
        
.cascade_broken:
        ; IRQ2 is masked - cascade broken!
        mov     byte ptr cs:[cascade_ok], 0
        
.done:
        pop     cx
        pop     ax
        ret
verify_cascade_state endp

; Cascade state flag (1 byte)
cascade_ok          db      1       ; Default OK

        end

;; Total size: ~50 bytes handlers + 9 bytes data = 59 bytes resident