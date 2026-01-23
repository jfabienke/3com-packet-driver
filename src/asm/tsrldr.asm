;;-----------------------------------------------------------------------------
;; @file tsr_loader.asm
;; @brief TSR loader with cold section discard capability
;;
;; This loader manages the TSR installation and discards the cold section
;; after initialization is complete to minimize resident memory.
;;
;; Memory Layout (EXE format):
;;   [PSP - 256 bytes]
;;   [Hot Section - Runtime Code]
;;   [Data Section - Variables/Buffers]
;;   [Stack]
;;   [Cold Section - Init Code] <- Discarded after init
;;-----------------------------------------------------------------------------

        bits 16
        cpu 286

;;-----------------------------------------------------------------------------
;; External symbols
;;-----------------------------------------------------------------------------
        extern main_init           ; Main initialization routine
        extern _edata              ; End of data segment (Watcom symbol)
        extern cpu_detect_init     ; CPU detection (cold)
        extern patch_init_and_apply ; SMC patch application (cold)
        extern nic_detect_init     ; NIC detection (cold)
        
;;-----------------------------------------------------------------------------
;; Public symbols
;;-----------------------------------------------------------------------------
        global tsr_loader_start
        global tsr_loader_install
        global _main               ; Entry point for Watcom C
        
;;-----------------------------------------------------------------------------
;; Segment definitions (Watcom compatible)
;;-----------------------------------------------------------------------------
segment _TEXT class=CODE use16

;;-----------------------------------------------------------------------------
;; Entry Point (called by Watcom C runtime)
;;-----------------------------------------------------------------------------
_main:
tsr_loader_start:
        ; Save initial state
        push    ax
        push    bx
        push    cx
        push    dx
        push    ds
        push    es
        push    bp
        
        ; Get our PSP segment
        mov     ah, 0x62        ; Get PSP address
        int     0x21
        mov     [psp_segment], bx
        mov     bp, bx          ; Keep PSP in BP for calculations
        
        ; Display loading message
        mov     dx, msg_loading
        mov     ah, 0x09
        int     0x21
        
        ; Detect CPU type (cold section)
        call    cpu_detect_init
        test    ax, ax
        jnz     .init_failed
        
        ; Detect NICs (cold section)
        call    nic_detect_init
        test    ax, ax
        jnz     .init_failed
        
        ; Apply SMC patches based on detected CPU (cold section)
        call    patch_init_and_apply
        test    ax, ax
        jnz     .init_failed
        
        ; Call main initialization (in cold section)
        call    main_init
        test    ax, ax
        jnz     .init_failed
        
        ; Calculate resident size (excludes cold section)
        call    calculate_resident_size
        mov     [resident_paragraphs], dx
        
        ; Display size information
        call    display_memory_info
        
        ; Install TSR interrupt handler
        call    install_tsr
        jc      .install_failed
        
        ; Display success message
        mov     dx, msg_success
        mov     ah, 0x09
        int     0x21
        
        ; Set up resident stack (important: must be within kept memory)
        cli
        mov     ax, seg stack_top
        mov     ss, ax
        mov     sp, stack_top
        sti
        
        ; Terminate and stay resident
        mov     dx, [resident_paragraphs]
        mov     ax, 0x3100      ; TSR with return code 0
        int     0x21
        
.init_failed:
        ; Display init failure message
        push    ax
        mov     dx, msg_init_failed
        mov     ah, 0x09
        int     0x21
        pop     ax
        
        ; Display error code
        call    display_error_code
        jmp     .cleanup_exit
        
.install_failed:
        ; Display install failure message
        mov     dx, msg_install_failed
        mov     ah, 0x09
        int     0x21
        
.cleanup_exit:
        ; Restore state and exit normally
        pop     bp
        pop     es
        pop     ds
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        
        ; Exit with error code
        mov     ax, 0x4C01      ; Exit with code 1
        int     0x21

;;-----------------------------------------------------------------------------
;; Calculate Resident Size (EXE-aware)
;; Returns: DX = size in paragraphs (16-byte units) including PSP
;;-----------------------------------------------------------------------------
calculate_resident_size:
        push    ax
        push    bx
        push    cx
        push    bp
        
        ; Get PSP segment if not already in BP
        mov     ah, 0x62
        int     0x21
        mov     bp, bx          ; BP = PSP segment
        
        ; Calculate paragraphs from PSP to last resident label
        ; DX = SEG(last_resident) - PSP
        mov     dx, seg last_resident
        sub     dx, bp
        
        ; Add offset of last_resident with proper rounding
        ; AX = OFFSET(last_resident) + 15
        mov     ax, last_resident
        add     ax, 0x0F
        adc     dx, 0           ; Carry from offset adds 1 paragraph
        shr     ax, 4           ; Convert offset to paragraphs
        add     dx, ax          ; DX = total paragraphs including PSP
        
        ; Store for display
        mov     [total_size], dx
        
        ; Calculate hot section size for display (approximation)
        mov     ax, seg _DATA
        sub     ax, seg _TEXT
        mov     [hot_size], ax
        
        ; Calculate cold section size for display (approximation)
        mov     ax, seg COLD_TEXT
        sub     ax, seg _DATA
        mov     [cold_size], ax
        
        pop     bp
        pop     cx
        pop     bx
        pop     ax
        ret

;;-----------------------------------------------------------------------------
;; Install TSR
;; Returns: CF=1 on error, CF=0 on success
;;-----------------------------------------------------------------------------
install_tsr:
        push    ax
        push    bx
        push    dx
        push    es
        
        ; Check if already installed
        mov     ax, 0x6000      ; Packet driver signature check
        xor     bx, bx
        int     0x60
        cmp     ax, 0x0001      ; Check for response
        je      .already_installed
        
        ; Hook INT 60h for packet driver API
        mov     ax, 0x3560      ; Get interrupt vector
        int     0x21
        mov     [old_int60_off], bx
        mov     [old_int60_seg], es
        
        ; Set new interrupt vector
        push    ds
        push    cs
        pop     ds
        mov     dx, packet_driver_handler
        mov     ax, 0x2560      ; Set interrupt vector
        int     0x21
        pop     ds
        
        clc                     ; Success
        jmp     .done
        
.already_installed:
        stc                     ; Error - already installed
        
.done:
        pop     es
        pop     dx
        pop     bx
        pop     ax
        ret

;;-----------------------------------------------------------------------------
;; Packet Driver Handler Stub (in hot section)
;;-----------------------------------------------------------------------------
packet_driver_handler:
        ; Save all registers
        push    ax
        push    bx
        push    cx
        push    dx
        push    si
        push    di
        push    bp
        push    ds
        push    es
        
        ; Call the actual handler (to be implemented)
        ; For now, just check function in AH
        cmp     ah, 0x60        ; Driver info function
        jne     .not_info
        
        ; Return driver info
        mov     ax, 0x0001      ; Version 1.0
        mov     bx, 0x0001      ; Class 1 (Ethernet)
        mov     cx, 0x0001      ; Type 1
        mov     dx, 0x0000      ; Number 0
        jmp     .done
        
.not_info:
        ; Unknown function
        mov     ax, 0xFFFF      ; Error
        
.done:
        ; Restore registers
        pop     es
        pop     ds
        pop     bp
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        add     sp, 2           ; Don't restore AX (return value)
        iret

;;-----------------------------------------------------------------------------
;; Display Memory Information
;;-----------------------------------------------------------------------------
display_memory_info:
        push    ax
        push    dx
        
        ; Display header
        mov     dx, msg_memory_info
        mov     ah, 0x09
        int     0x21
        
        ; Display hot section size
        mov     dx, msg_hot_size
        mov     ah, 0x09
        int     0x21
        mov     ax, [hot_size]
        call    display_size_kb
        
        ; Display cold section size (to be discarded)
        mov     dx, msg_cold_size
        mov     ah, 0x09
        int     0x21
        mov     ax, [cold_size]
        call    display_size_kb
        
        ; Display final resident size
        mov     dx, msg_resident_size
        mov     ah, 0x09
        int     0x21
        mov     ax, [total_size]
        call    display_size_kb
        
        pop     dx
        pop     ax
        ret

;;-----------------------------------------------------------------------------
;; Display Size in KB
;; Input: AX = size in paragraphs
;;-----------------------------------------------------------------------------
display_size_kb:
        push    ax
        push    bx
        push    cx
        push    dx
        
        ; Convert paragraphs to KB (para * 16 / 1024)
        shl     ax, 4           ; * 16 to get bytes
        mov     bx, 1024
        xor     dx, dx
        div     bx              ; AX = KB, DX = remainder bytes
        
        ; Display KB value
        call    display_decimal
        
        ; Display "KB" suffix
        mov     dx, msg_kb_suffix
        mov     ah, 0x09
        int     0x21
        
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        ret

;;-----------------------------------------------------------------------------
;; Display Decimal Number
;; Input: AX = number to display
;;-----------------------------------------------------------------------------
display_decimal:
        push    ax
        push    bx
        push    cx
        push    dx
        
        mov     bx, 10
        xor     cx, cx
        
.divide_loop:
        xor     dx, dx
        div     bx
        push    dx
        inc     cx
        test    ax, ax
        jnz     .divide_loop
        
.display_loop:
        pop     dx
        add     dl, '0'
        mov     ah, 0x02
        int     0x21
        loop    .display_loop
        
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        ret

;;-----------------------------------------------------------------------------
;; Display Error Code
;; Input: AX = error code
;;-----------------------------------------------------------------------------
display_error_code:
        push    ax
        push    dx
        
        ; Display "Error code: "
        mov     dx, msg_error_code
        mov     ah, 0x09
        int     0x21
        
        ; Display error number
        neg     ax              ; Make positive
        call    display_decimal
        
        ; Display newline
        mov     dx, msg_newline
        mov     ah, 0x09
        int     0x21
        
        pop     dx
        pop     ax
        ret

;;-----------------------------------------------------------------------------
;; Data Section
;;-----------------------------------------------------------------------------
segment _DATA class=DATA use16

; TSR state variables
psp_segment         dw  0
resident_paragraphs dw  0
old_int60_off       dw  0
old_int60_seg       dw  0

; Size tracking
total_size          dw  0
hot_size            dw  0
cold_size           dw  0

; Messages
msg_loading         db  '3Com Packet Driver TSR Loader v1.0', 13, 10
                    db  'Detecting hardware...', 13, 10, '$'
                    
msg_success         db  'Driver optimized and installed!', 13, 10, '$'

msg_init_failed     db  'Initialization failed!', 13, 10, '$'

msg_install_failed  db  'TSR installation failed!', 13, 10
                    db  'Driver may already be installed.', 13, 10, '$'
                    
msg_memory_info     db  13, 10, 'Memory allocation:', 13, 10, '$'

msg_hot_size        db  '  Hot section (resident): $'

msg_cold_size       db  '  Cold section (discarded): $'

msg_resident_size   db  '  Total resident size: $'

msg_kb_suffix       db  ' KB', 13, 10, '$'

msg_error_code      db  'Error code: $'

msg_newline         db  13, 10, '$'

; Resident stack (must be in kept memory)
                    align 16
stack_bottom:       times 512 db 0
stack_top:

; Mark end of resident portion
last_resident:
                    align 16

;;-----------------------------------------------------------------------------
;; Cold Section (discarded after initialization)
;;-----------------------------------------------------------------------------
segment COLD_TEXT class=CODE use16

; Cold section code will go here
; This includes all initialization-only functions

;;-----------------------------------------------------------------------------
;; End of file
;;-----------------------------------------------------------------------------