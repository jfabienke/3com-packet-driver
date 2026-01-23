;;-----------------------------------------------------------------------------
;; @file tsrldr.asm
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
;;
;; Last Updated: 2026-01-23 20:15:00 CET
;; Converted from NASM to WASM/MASM syntax for unified toolchain
;;-----------------------------------------------------------------------------

.MODEL SMALL
.286

;;-----------------------------------------------------------------------------
;; External symbols
;;-----------------------------------------------------------------------------
EXTRN main_init:PROC            ; Main initialization routine
EXTRN _edata:WORD               ; End of data segment (Watcom symbol)
EXTRN cpu_detect_init:PROC      ; CPU detection (cold)
EXTRN patch_init_and_apply:PROC ; SMC patch application (cold)
EXTRN nic_detect_init:PROC      ; NIC detection (cold)

;;-----------------------------------------------------------------------------
;; Public symbols
;;-----------------------------------------------------------------------------
PUBLIC tsr_loader_start
PUBLIC tsr_loader_install
PUBLIC _main                    ; Entry point for Watcom C

;;-----------------------------------------------------------------------------
;; Code Segment
;;-----------------------------------------------------------------------------
_TEXT SEGMENT WORD PUBLIC 'CODE'
        ASSUME CS:_TEXT, DS:_DATA

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
        mov     ah, 62h         ; Get PSP address
        int     21h
        mov     psp_segment, bx
        mov     bp, bx          ; Keep PSP in BP for calculations

        ; Display loading message
        mov     dx, OFFSET msg_loading
        mov     ah, 09h
        int     21h

        ; Detect CPU type (cold section)
        call    cpu_detect_init
        test    ax, ax
        jnz     init_failed

        ; Detect NICs (cold section)
        call    nic_detect_init
        test    ax, ax
        jnz     init_failed

        ; Apply SMC patches based on detected CPU (cold section)
        call    patch_init_and_apply
        test    ax, ax
        jnz     init_failed

        ; Call main initialization (in cold section)
        call    main_init
        test    ax, ax
        jnz     init_failed

        ; Calculate resident size (excludes cold section)
        call    calculate_resident_size
        mov     resident_paragraphs, dx

        ; Display size information
        call    display_memory_info

        ; Install TSR interrupt handler
        call    install_tsr
        jc      install_failed

        ; Display success message
        mov     dx, OFFSET msg_success
        mov     ah, 09h
        int     21h

        ; Set up resident stack (important: must be within kept memory)
        cli
        mov     ax, SEG stack_top
        mov     ss, ax
        mov     sp, OFFSET stack_top
        sti

        ; Terminate and stay resident
        mov     dx, resident_paragraphs
        mov     ax, 3100h       ; TSR with return code 0
        int     21h

init_failed:
        ; Display init failure message
        push    ax
        mov     dx, OFFSET msg_init_failed
        mov     ah, 09h
        int     21h
        pop     ax

        ; Display error code
        call    display_error_code
        jmp     cleanup_exit

install_failed:
        ; Display install failure message
        mov     dx, OFFSET msg_install_failed
        mov     ah, 09h
        int     21h

cleanup_exit:
        ; Restore state and exit normally
        pop     bp
        pop     es
        pop     ds
        pop     dx
        pop     cx
        pop     bx
        pop     ax

        ; Exit with error code
        mov     ax, 4C01h       ; Exit with code 1
        int     21h

;;-----------------------------------------------------------------------------
;; Calculate Resident Size (EXE-aware)
;; Returns: DX = size in paragraphs (16-byte units) including PSP
;;-----------------------------------------------------------------------------
calculate_resident_size PROC
        push    ax
        push    bx
        push    cx
        push    bp

        ; Get PSP segment if not already in BP
        mov     ah, 62h
        int     21h
        mov     bp, bx          ; BP = PSP segment

        ; Calculate paragraphs from PSP to last resident label
        ; DX = SEG(last_resident) - PSP
        mov     dx, SEG last_resident
        sub     dx, bp

        ; Add offset of last_resident with proper rounding
        ; AX = OFFSET(last_resident) + 15
        mov     ax, OFFSET last_resident
        add     ax, 0Fh
        adc     dx, 0           ; Carry from offset adds 1 paragraph
        shr     ax, 4           ; Convert offset to paragraphs
        add     dx, ax          ; DX = total paragraphs including PSP

        ; Store for display
        mov     total_size, dx

        ; Calculate hot section size for display (approximation)
        mov     ax, SEG _DATA
        sub     ax, SEG _TEXT
        mov     hot_size, ax

        ; Calculate cold section size for display (approximation)
        mov     ax, SEG COLD_TEXT
        sub     ax, SEG _DATA
        mov     cold_size, ax

        pop     bp
        pop     cx
        pop     bx
        pop     ax
        ret
calculate_resident_size ENDP

;;-----------------------------------------------------------------------------
;; Install TSR
;; Returns: CF=1 on error, CF=0 on success
;;-----------------------------------------------------------------------------
install_tsr PROC
        push    ax
        push    bx
        push    dx
        push    es

        ; Check if already installed
        mov     ax, 6000h       ; Packet driver signature check
        xor     bx, bx
        int     60h
        cmp     ax, 0001h       ; Check for response
        je      already_installed

        ; Hook INT 60h for packet driver API
        mov     ax, 3560h       ; Get interrupt vector
        int     21h
        mov     old_int60_off, bx
        mov     old_int60_seg, es

        ; Set new interrupt vector
        push    ds
        push    cs
        pop     ds
        mov     dx, OFFSET packet_driver_handler
        mov     ax, 2560h       ; Set interrupt vector
        int     21h
        pop     ds

        clc                     ; Success
        jmp     install_done

already_installed:
        stc                     ; Error - already installed

install_done:
        pop     es
        pop     dx
        pop     bx
        pop     ax
        ret
install_tsr ENDP

;;-----------------------------------------------------------------------------
;; Packet Driver Handler Stub (in hot section)
;;-----------------------------------------------------------------------------
packet_driver_handler PROC FAR
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
        cmp     ah, 60h         ; Driver info function
        jne     pdh_not_info

        ; Return driver info
        mov     ax, 0001h       ; Version 1.0
        mov     bx, 0001h       ; Class 1 (Ethernet)
        mov     cx, 0001h       ; Type 1
        mov     dx, 0000h       ; Number 0
        jmp     pdh_done

pdh_not_info:
        ; Unknown function
        mov     ax, 0FFFFh      ; Error

pdh_done:
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
packet_driver_handler ENDP

;;-----------------------------------------------------------------------------
;; Display Memory Information
;;-----------------------------------------------------------------------------
display_memory_info PROC
        push    ax
        push    dx

        ; Display header
        mov     dx, OFFSET msg_memory_info
        mov     ah, 09h
        int     21h

        ; Display hot section size
        mov     dx, OFFSET msg_hot_size
        mov     ah, 09h
        int     21h
        mov     ax, hot_size
        call    display_size_kb

        ; Display cold section size (to be discarded)
        mov     dx, OFFSET msg_cold_size
        mov     ah, 09h
        int     21h
        mov     ax, cold_size
        call    display_size_kb

        ; Display final resident size
        mov     dx, OFFSET msg_resident_size
        mov     ah, 09h
        int     21h
        mov     ax, total_size
        call    display_size_kb

        pop     dx
        pop     ax
        ret
display_memory_info ENDP

;;-----------------------------------------------------------------------------
;; Display Size in KB
;; Input: AX = size in paragraphs
;;-----------------------------------------------------------------------------
display_size_kb PROC
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
        mov     dx, OFFSET msg_kb_suffix
        mov     ah, 09h
        int     21h

        pop     dx
        pop     cx
        pop     bx
        pop     ax
        ret
display_size_kb ENDP

;;-----------------------------------------------------------------------------
;; Display Decimal Number
;; Input: AX = number to display
;;-----------------------------------------------------------------------------
display_decimal PROC
        push    ax
        push    bx
        push    cx
        push    dx

        mov     bx, 10
        xor     cx, cx

dd_divide_loop:
        xor     dx, dx
        div     bx
        push    dx
        inc     cx
        test    ax, ax
        jnz     dd_divide_loop

dd_display_loop:
        pop     dx
        add     dl, '0'
        mov     ah, 02h
        int     21h
        loop    dd_display_loop

        pop     dx
        pop     cx
        pop     bx
        pop     ax
        ret
display_decimal ENDP

;;-----------------------------------------------------------------------------
;; Display Error Code
;; Input: AX = error code
;;-----------------------------------------------------------------------------
display_error_code PROC
        push    ax
        push    dx

        ; Display "Error code: "
        mov     dx, OFFSET msg_error_code
        mov     ah, 09h
        int     21h

        ; Display error number
        neg     ax              ; Make positive
        call    display_decimal

        ; Display newline
        mov     dx, OFFSET msg_newline
        mov     ah, 09h
        int     21h

        pop     dx
        pop     ax
        ret
display_error_code ENDP

_TEXT ENDS

;;-----------------------------------------------------------------------------
;; Data Section
;;-----------------------------------------------------------------------------
_DATA SEGMENT WORD PUBLIC 'DATA'
        ASSUME DS:_DATA

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
                    ALIGN 16
stack_bottom        db  512 dup(0)
stack_top           LABEL WORD

; Mark end of resident portion
last_resident       LABEL BYTE
                    ALIGN 16

_DATA ENDS

;;-----------------------------------------------------------------------------
;; Cold Section (discarded after initialization)
;;-----------------------------------------------------------------------------
COLD_TEXT SEGMENT WORD PUBLIC 'CODE'
        ASSUME CS:COLD_TEXT

; Cold section code will go here
; This includes all initialization-only functions

COLD_TEXT ENDS

;;-----------------------------------------------------------------------------
;; End of file
;;-----------------------------------------------------------------------------
END _main
