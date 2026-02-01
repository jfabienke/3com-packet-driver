; @file main.asm
; @brief Assembly entry point and initialization
;
; 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
;
; This file is part of the 3Com Packet Driver project.
;
; Converted to NASM syntax - 2026-01-25

bits 16
cpu 386

; C symbol naming bridge (maps C symbols to symbol_)
%include "csym.inc"

; DOS Function Constants
DOS_FUNC_TSR        equ 31h     ; DOS function for TSR
DOS_FUNC_GET_VEC    equ 35h     ; DOS function to get interrupt vector
DOS_FUNC_SET_VEC    equ 25h     ; DOS function to set interrupt vector

; Interrupt Constants
PACKET_INT          equ 60h     ; Packet Driver interrupt
IRQ_BASE            equ 08h     ; Base IRQ interrupt number
DOS_IDLE_INT        equ 28h     ; DOS idle interrupt

; Return Codes
SUCCESS             equ 0       ; Success return code
ERROR_GENERAL       equ 1       ; General error code
ERROR_ALREADY_LOADED equ 2      ; Driver already loaded
ERROR_CPU_UNSUPPORTED equ 3     ; CPU not supported
ERROR_HARDWARE      equ 4       ; Hardware error
ERROR_MEMORY        equ 5       ; Memory allocation error

; Installation Signature
INSTALL_CHECK_AX    equ 1234h   ; Installation check signature
INSTALL_RESP_AX     equ 5678h   ; Installation response signature
DRIVER_MAGIC        equ 'PD'    ; Driver magic bytes

; Data segment
section .data

; Driver state variables
driver_loaded:       db 0        ; Driver loaded flag
tsr_size:           dw 0        ; TSR resident size in paragraphs
original_vectors:   times 8 dd 0 ; Storage for original interrupt vectors
original_int28:     dd 0        ; Storage for original INT 28h vector
init_complete:      db 0        ; Initialization complete flag
installed_irq:      db 0        ; Installed hardware IRQ number
nic_base_addr:      dw 0        ; NIC base I/O address

; Driver identification strings
driver_signature:   db 'PKT DRVR', 0   ; Driver signature
version_string:     db '3Com Packet Driver v1.0', 0Dh, 0Ah, 0
install_msg:        db '3Com Packet Driver installed successfully', 0Dh, 0Ah, 0
already_loaded_msg: db 'Driver already loaded', 0Dh, 0Ah, 0
memory_error_msg:   db 'Memory optimization initialization failed', 0Dh, 0Ah, 0
defensive_error_msg: db 'Defensive programming initialization failed', 0Dh, 0Ah, 0
cpu_error_msg:      db 'CPU detection failed', 0Dh, 0Ah, 0
hardware_error_msg: db 'Hardware initialization failed', 0Dh, 0Ah, 0

; Integration status variables
vector_stolen_flag:  db 0        ; Flag indicating if interrupt vector was stolen
hardware_irq_count:  db 0        ; Number of installed hardware IRQs
hardware_irq_error_msg: db 'Warning: Hardware IRQ installation failed', 0Dh, 0Ah, 0

; Installation check variables
magic_signature:    dw DRIVER_MAGIC  ; Magic signature for installation check
install_vector:     dd 0             ; Storage for packet driver vector

; Code segment
section .text

; External function declarations
extern cpu_detect_main      ; From cpu_detect.asm
extern packet_api_init      ; From packet_api.asm
extern nic_irq_init         ; From nic_irq.asm
extern hardware_init_asm    ; From hardware.asm
extern pnp_detect_all       ; From pnp.asm

; Hardware integration functions
extern hardware_get_detected_nics     ; Get detected NIC information
extern install_hardware_irq           ; Install IRQ for specific NIC
extern restore_all_hardware_irqs      ; Restore all hardware IRQs
extern packet_api_dispatcher          ; Full packet driver API

; Diagnostic and logging functions
extern log_vector_ownership_warning   ; Log vector ownership issues
extern log_hardware_irq_restore_warning ; Log IRQ restore warnings

; DOS integration functions
extern dos_idle_background_processing ; Background tasks during DOS idle

; Defensive programming integration
extern defensive_init       ; From defensive_integration.asm
extern defensive_shutdown   ; From defensive_integration.asm
extern safe_restore_vector  ; From defensive_integration.asm
extern check_vector_ownership ; From defensive_integration.asm

; Public function exports
global driver_entry
global tsr_main
global install_interrupts
global uninstall_interrupts
global check_driver_loaded
global packet_handler
global dos_idle_handler

;-----------------------------------------------------------------------------
; driver_entry - Main driver entry point
; Called when driver is loaded from CONFIG.SYS or command line
;
; Input:  None
; Output: AX = 0 for success, non-zero for error
; Uses:   All registers
;-----------------------------------------------------------------------------
driver_entry:
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si
        push    di
        push    ds
        push    es

        ; Set up data segment (CS=DS in flat model)
        push    cs
        pop     ax
        mov     ds, ax

        ; Check if driver already loaded
        call    check_driver_loaded
        cmp     ax, 0
        jne     .already_loaded

        ; Initialize memory optimization FIRST
        call    initialize_memory_optimization
        cmp     ax, 0
        jne     .memory_error

        ; Try UMB allocation before continuing
        call    attempt_umb_loading
        ; Continue regardless of UMB result - we can fall back to conventional

        ; Initialize defensive programming systems (critical for safety)
        call    defensive_init
        cmp     ax, 0
        jne     .defensive_error

        ; Perform CPU detection (critical - must support 286+)
        call    cpu_detect_main
        cmp     ax, 0
        jne     .cpu_error

        ; Initialize hardware subsystems
        call    hardware_init_asm
        cmp     ax, 0
        jne     .hardware_error

        ; Detect PnP hardware (non-fatal if fails)
        call    pnp_detect_all
        ; Continue regardless of PnP detection result

        ; Initialize packet API
        call    packet_api_init
        cmp     ax, 0
        jne     .api_error

        ; Initialize interrupt handling
        call    nic_irq_init
        cmp     ax, 0
        jne     .irq_error

        ; Install interrupt vectors
        call    install_interrupts
        cmp     ax, 0
        jne     .install_error

        ; Mark driver as loaded
        mov     byte [driver_loaded], 1
        mov     byte [init_complete], 1

        ; Display installation success message
        mov     dx, install_msg
        call    print_string

        ; Calculate TSR size and go resident
        call    tsr_main

        ; Success - this point should not be reached after TSR
        mov     ax, SUCCESS
        jmp     .exit

.already_loaded:
        mov     dx, already_loaded_msg
        call    print_string
        mov     ax, ERROR_ALREADY_LOADED
        jmp     .exit

.memory_error:
        mov     dx, memory_error_msg
        call    print_string
        mov     ax, ERROR_MEMORY
        jmp     .exit

.defensive_error:
        mov     dx, defensive_error_msg
        call    print_string
        mov     ax, ERROR_GENERAL
        jmp     .exit

.cpu_error:
        mov     dx, cpu_error_msg
        call    print_string
        mov     ax, ERROR_CPU_UNSUPPORTED
        jmp     .exit

.hardware_error:
        mov     dx, hardware_error_msg
        call    print_string
        mov     ax, ERROR_HARDWARE
        jmp     .exit

.api_error:
        ; Call uninstall to clean up any partial installation
        call    uninstall_interrupts
        mov     ax, ERROR_GENERAL
        jmp     .exit

.irq_error:
        ; Call uninstall to clean up any partial installation
        call    uninstall_interrupts
        mov     ax, ERROR_GENERAL
        jmp     .exit

.install_error:
        ; Installation failed - clean up
        call    uninstall_interrupts
        mov     ax, ERROR_GENERAL
        jmp     .exit

.install_irq_error:
        ; Hardware IRQ installation failed - continue with packet driver only
        mov     dx, hardware_irq_error_msg
        call    print_string
        ; Continue - packet driver can still work without hardware IRQs
        jmp     install_interrupts.hardware_irq_install_complete

.exit:
        pop     es
        pop     ds
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
;; end driver_entry

;-----------------------------------------------------------------------------
; tsr_main - Make driver resident and calculate TSR size
;
; Input:  None
; Output: Does not return (terminates and stays resident)
; Uses:   All registers
;-----------------------------------------------------------------------------
tsr_main:
        push    bp
        mov     bp, sp
        push    ax
        push    bx
        push    cx
        push    dx

        ; Use optimized memory calculation for resident size
        call    calculate_optimized_tsr_size
        mov     [tsr_size], ax

        ; If we have UMB, relocate before going resident
        call    finalize_umb_relocation

        ; Release discardable initialization memory
        call    release_init_memory

        ; Terminate and stay resident with optimized size
        mov     ah, DOS_FUNC_TSR
        mov     al, SUCCESS                 ; Exit code
        mov     dx, [tsr_size]             ; Optimized size in paragraphs
        int     21h                        ; DOS function call

        ; This point should never be reached
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        pop     bp
        ret
;; end tsr_main

;-----------------------------------------------------------------------------
; install_interrupts - Install interrupt vectors
;
; Input:  None
; Output: AX = 0 for success, non-zero for error
; Uses:   AX, BX, CX, DX, ES
;-----------------------------------------------------------------------------
install_interrupts:
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    es
        push    si
        push    di

        ; Save original packet driver interrupt vector (INT 60h)
        mov     ah, DOS_FUNC_GET_VEC
        mov     al, PACKET_INT
        int     21h
        ; ES:BX now contains original vector
        mov     word [original_vectors], bx
        mov     word [original_vectors+2], es

        ; Install our packet driver interrupt handler
        mov     ah, DOS_FUNC_SET_VEC
        mov     al, PACKET_INT
        mov     dx, packet_handler
        push    ds
        push    cs
        pop     ds
        int     21h
        pop     ds

        ; Save original DOS idle interrupt vector (INT 28h)
        mov     ah, DOS_FUNC_GET_VEC
        mov     al, DOS_IDLE_INT
        int     21h
        ; ES:BX now contains original vector
        mov     word [original_int28], bx
        mov     word [original_int28+2], es

        ; Install our DOS idle interrupt handler for background processing
        mov     ah, DOS_FUNC_SET_VEC
        mov     al, DOS_IDLE_INT
        mov     dx, dos_idle_handler
        push    ds
        push    cs
        pop     ds
        int     21h
        pop     ds

        ; Store installed vector for signature check
        mov     word [install_vector], packet_handler
        mov     word [install_vector+2], cs

        ; Install hardware interrupt handler for detected NICs
        ; Query hardware layer for detected NICs and their IRQ assignments
        call    hardware_get_detected_nics  ; Returns count in AX, info in hardware table
        cmp     ax, 0
        je      .no_hardware_irqs          ; No NICs detected, skip IRQ setup

        ; Install IRQ handlers for each detected NIC
        mov     cx, ax                      ; CX = number of NICs
        mov     si, 0                       ; SI = NIC index
.install_irq_loop:
        push    cx
        push    si
        call    install_hardware_irq        ; Install IRQ for NIC at index SI
        pop     si
        pop     cx
        cmp     ax, 0
        jne     .install_irq_error          ; IRQ installation failed
        inc     si
        dec     cx
        jnz     .install_irq_loop

.no_hardware_irqs:
.hardware_irq_install_complete:

        mov     ax, SUCCESS

        pop     di
        pop     si
        pop     es
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret

.install_irq_error:
        ; Jump to error handler defined in driver_entry
        jmp     driver_entry.install_irq_error
;; end install_interrupts

;-----------------------------------------------------------------------------
; uninstall_interrupts - Restore original interrupt vectors
;
; Input:  None
; Output: AX = 0 for success, non-zero for error
; Uses:   AX, BX, CX, DX, ES
;-----------------------------------------------------------------------------
uninstall_interrupts:
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    es
        push    ds

        ; Restore our data segment first (CS=DS in flat model)
        push    cs
        pop     ax
        mov     ds, ax

        ; Use safe vector restoration instead of blind restoration
        ; This checks if we still own the vector before restoring it
        mov     al, PACKET_INT          ; Interrupt number
        mov     dx, cs                  ; Our handler segment
        ; Get actual packet handler offset from our installed handler
        mov     bx, packet_handler   ; Our handler offset
        mov     cx, word [original_vectors+2]  ; Original vector segment
        mov     si, word [original_vectors]    ; Original vector offset
        call    safe_restore_vector
        jc      .vector_not_restored    ; Vector was not ours - cannot restore safely
        jmp     .vector_restore_complete

.vector_not_restored:
        ; Vector was stolen by another TSR - we cannot safely restore it
        ; This is not necessarily an error - the other TSR should chain to us
        ; Log vector ownership event for diagnostics
        call    log_vector_ownership_warning
        ; Set a warning flag but continue with shutdown
        mov     byte [vector_stolen_flag], 1

.vector_restore_complete:
        ; Shutdown defensive programming systems
        call    defensive_shutdown

        ; Restore DOS idle interrupt vector (INT 28h) first
        mov     ah, DOS_FUNC_SET_VEC
        mov     al, DOS_IDLE_INT
        mov     dx, word [original_int28]       ; Original vector offset
        push    ds
        mov     ds, word [original_int28+2]     ; Original vector segment
        int     21h
        pop     ds

        ; Restore hardware interrupt vectors for all installed NICs
        call    restore_all_hardware_irqs
        cmp     ax, 0
        jne     .hardware_irq_restore_failed
        jmp     .hardware_irq_restore_complete

.hardware_irq_restore_failed:
        ; Log but don't fail shutdown - vectors may have been chained
        call    log_hardware_irq_restore_warning

.hardware_irq_restore_complete:
        ; Clear installation signature
        mov     word [install_vector], 0
        mov     word [install_vector+2], 0

        mov     ax, SUCCESS

        pop     ds
        pop     es
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
;; end uninstall_interrupts

;-----------------------------------------------------------------------------
; check_driver_loaded - Check if driver is already loaded
;
; Input:  None
; Output: AX = 0 if not loaded, non-zero if already loaded
; Uses:   AX, BX, CX, DX, ES
;-----------------------------------------------------------------------------
check_driver_loaded:
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    es

        ; Get current packet driver interrupt vector
        mov     ah, DOS_FUNC_GET_VEC
        mov     al, PACKET_INT
        int     21h

        ; Check if vector points to valid memory (not 0:0)
        mov     ax, es
        or      ax, bx
        jz      .not_loaded     ; Vector is 0:0, definitely not loaded

        ; Try installation check using packet driver signature
        mov     ax, INSTALL_CHECK_AX
        int     PACKET_INT

        ; Check for expected response
        cmp     ax, INSTALL_RESP_AX
        jne     .not_loaded

        ; Driver appears to be loaded
        mov     ax, ERROR_ALREADY_LOADED
        jmp     .exit

.not_loaded:
        mov     ax, SUCCESS

.exit:
        pop     es
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
;; end check_driver_loaded

;-----------------------------------------------------------------------------
; packet_handler - Packet Driver API interrupt handler (INT 60h)
;
; Input:  AH = Function code, other registers per function
; Output: Per function specification
; Uses:   Per function specification
;-----------------------------------------------------------------------------
packet_handler: ;; FAR procedure
        ; Check for installation signature request
        cmp     ax, INSTALL_CHECK_AX
        jne     .not_install_check

        ; Return installation signature
        mov     ax, INSTALL_RESP_AX
        iret

.not_install_check:
        ; Dispatch to full packet driver API implementation
        ; Save all registers for API call
        push    ax
        push    bx
        push    cx
        push    dx
        push    si
        push    di
        push    bp
        push    ds
        push    es

        ; Set up data segment
        push    cs
        pop     ds

        ; Call packet API dispatcher
        call    packet_api_dispatcher       ; Handles all packet driver functions

        ; Restore registers
        pop     es
        pop     ds
        pop     bp
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        ; Keep AX for return value
        add     sp, 2                       ; Skip saved AX

        ; Return with appropriate flags
        iret
;; end packet_handler

;-----------------------------------------------------------------------------
; dos_idle_handler - DOS idle interrupt handler (INT 28h)
;
; This handler is called during DOS idle periods to allow background processing
; We use this for packet reception and driver maintenance tasks.
;
; Input:  None (called by DOS during idle periods)
; Output: None
; Uses:   None (preserves all registers)
;-----------------------------------------------------------------------------
dos_idle_handler: ;; FAR procedure
        ; Save all registers for background processing
        push    ax
        push    bx
        push    cx
        push    dx
        push    si
        push    di
        push    bp
        push    ds
        push    es

        ; Set up our data segment
        push    cs
        pop     ds

        ; Perform background driver tasks during DOS idle time
        call    dos_idle_background_processing

        ; Restore all registers
        pop     es
        pop     ds
        pop     bp
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     ax

        ; Chain to original INT 28h handler
        pushf                           ; Simulate interrupt call
        call far [original_int28]

        iret
;; end dos_idle_handler

;-----------------------------------------------------------------------------
; Memory optimization support functions
;-----------------------------------------------------------------------------

;-----------------------------------------------------------------------------
; attempt_umb_loading - Try to allocate UMB for TSR
;
; Input:  None
; Output: Updates UMB status variables
; Uses:   AX, BX, CX, DX
;-----------------------------------------------------------------------------
attempt_umb_loading:
        push    bp
        mov     bp, sp
        push    cx

        ; Calculate required TSR size first
        call    calculate_optimized_tsr_size
        mov     cx, ax                      ; CX = required paragraphs

        ; Attempt UMB allocation
        call    attempt_umb_allocation
        jc      .no_umb

        ; Success - UMB allocated
        jmp     short .exit

.no_umb:
        ; UMB allocation failed - will use conventional memory
        ; This is not an error, just less optimal

.exit:
        pop     cx
        pop     bp
        ret
;; end attempt_umb_loading

;-----------------------------------------------------------------------------
; calculate_optimized_tsr_size - Calculate minimal resident size
;
; Input:  None
; Output: AX = size in paragraphs
; Uses:   AX, BX, CX
;-----------------------------------------------------------------------------
calculate_optimized_tsr_size:
        push    bp
        mov     bp, sp
        push    bx
        push    cx

        ; Calculate resident code size only (exclude init segments)
        ; In a full implementation, this would use symbols from
        ; tsr_memory_opt.asm to get exact resident boundaries

        ; For now, use a conservative estimate
        ; Resident code: ~2KB, Resident data: ~1KB, Stack: 512 bytes
        mov     ax, 2048 + 1024 + 512       ; Conservative resident size

        ; Add PSP size (256 bytes)
        add     ax, 256

        ; Round up to paragraphs
        add     ax, 15
        shr     ax, 4                       ; Convert to paragraphs

        ; Minimum safety margin
        add     ax, 4                       ; Add 4 paragraphs (64 bytes) safety

        pop     cx
        pop     bx
        pop     bp
        ret
;; end calculate_optimized_tsr_size

;-----------------------------------------------------------------------------
; finalize_umb_relocation - Complete UMB relocation if available
;
; Input:  None
; Output: None
; Uses:   AX, BX, CX
;-----------------------------------------------------------------------------
finalize_umb_relocation:
        push    bp
        mov     bp, sp
        push    ax
        push    bx
        push    cx

        ; Check if UMB was allocated
        mov     ax, [umb_segment]           ; This would come from tsr_common
        cmp     ax, 0
        jz      .no_umb

        ; UMB is available - in a full implementation, would:
        ; 1. Copy resident code/data to UMB
        ; 2. Update interrupt vectors to point to UMB
        ; 3. Set TSR size to minimal (just PSP in conventional memory)

        ; For now, just note that UMB is available
        ; The TSR size calculation already accounts for this

.no_umb:
        ; Using conventional memory - normal TSR operation

        pop     cx
        pop     bx
        pop     ax
        pop     bp
        ret
;; end finalize_umb_relocation

;-----------------------------------------------------------------------------
; release_init_memory - Release discardable initialization memory
;
; Input:  None
; Output: None
; Uses:   AX, BX
;-----------------------------------------------------------------------------
release_init_memory:
        push    bp
        mov     bp, sp
        push    ax
        push    bx

        ; Use DOS function 4Ah to resize memory block
        ; This releases everything above the resident size
        mov     ah, 4Ah                     ; Modify allocated memory blocks
        mov     bx, [tsr_size]             ; New size in paragraphs
        int     21h                        ; Release excess memory
        jc      .resize_failed

        ; Successfully released init memory
        jmp     short .exit

.resize_failed:
        ; Memory resize failed - not critical, continue anyway
        ; The init memory will just stay allocated

.exit:
        pop     bx
        pop     ax
        pop     bp
        ret
;; end release_init_memory

;-----------------------------------------------------------------------------
; External function references for memory optimization
;-----------------------------------------------------------------------------
extern initialize_memory_optimization   ; From tsr_memory_opt.asm
extern attempt_umb_allocation           ; From tsr_common.asm
extern umb_segment                      ; From tsr_common.asm data

;-----------------------------------------------------------------------------
; print_string - Print null-terminated string to console
;
; Input:  DS:DX = Pointer to null-terminated string
; Output: None
; Uses:   AX, DX
;-----------------------------------------------------------------------------
print_string:
        push    ax
        push    si
        push    bx

        mov     si, dx
.loop:
        lodsb                   ; Load byte from DS:SI into AL
        or      al, al         ; Check for null terminator
        jz      .done

        ; Print character using DOS function 02h
        mov     dl, al
        mov     ah, 02h
        int     21h
        jmp     .loop

.done:
        pop     bx
        pop     si
        pop     ax
        ret
;; end print_string

; Marker for end of resident code
tsr_end:
        nop     ; End marker for TSR size calculation
