; @file hwdet.asm
; @brief Cold detection functions for 3Com NIC hardware
;
; 3Com Packet Driver - Hardware Detection Module (COLD section)
; Contains detection routines that are discarded after driver initialization.
;
; Extracted from hardware.asm during modularization.
;
; Created: 2026-01-25 from hardware.asm modularization
; Last modified: 2026-01-25 11:32:55
;
; This is a COLD section module - discarded after load.

bits 16
cpu 386

; C symbol naming bridge (maps C symbols to _symbol)
%include "csym.inc"

; Tell asm_interfaces.inc we're defining hardware symbols here
%define HARDWARE_MODULE_DEFINING

; Include assembly interface definitions
%include "asm_interfaces.inc"
%include "lfsr_table.inc"

;-----------------------------------------------------------------------------
; Global exports
;-----------------------------------------------------------------------------
global hardware_detect_all
global detect_3c509b
global detect_3c515
global detect_3c509b_device     ; Alias expected by hwinit.asm and hwcoord.asm
global detect_3c515_device      ; Alias expected by hwinit.asm and hwcoord.asm
global probe_3c515_at_address
global delay_1us

;-----------------------------------------------------------------------------
; External references - logging functions
;-----------------------------------------------------------------------------
extern log_warning
extern log_error

;-----------------------------------------------------------------------------
; External references - bus detection stubs from hwbus.asm
;-----------------------------------------------------------------------------
extern nic_detect_eisa_3c592
extern nic_detect_eisa_3c597
extern is_mca_system
extern nic_detect_mca_3c523
extern nic_detect_mca_3c529
extern nic_detect_vlb

;-----------------------------------------------------------------------------
; External references - data
;-----------------------------------------------------------------------------
extern hw_instances
extern hw_types
extern hw_io_bases

;-----------------------------------------------------------------------------
; Local constants (from hardware.asm)
;-----------------------------------------------------------------------------
C515_ISA_MIN_IO     EQU 100h    ; Minimum ISA I/O for 3C515
C515_ISA_MAX_IO     EQU 3E0h    ; Maximum ISA I/O for 3C515
C515_ISA_STEP       EQU 20h     ; ISA scan step size

;-----------------------------------------------------------------------------
; COLD section - discarded after driver initialization
;-----------------------------------------------------------------------------
segment COLD_TEXT class=CODE public use16

;-----------------------------------------------------------------------------
; Message strings for detection warnings/errors
;-----------------------------------------------------------------------------
msg_eisa_not_supported: db 'EISA NICs detected but not supported', 0
msg_mca_not_supported:  db 'MicroChannel NICs detected but not supported', 0

msg_pure_mca_error1:   db 'ERROR: No compatible network adapters available on this system.', 0
msg_pure_mca_error2:   db 'This driver only supports ISA-based 3Com NICs (3C509B, 3C515-TX).', 0
msg_pure_mca_error3:   db 'MicroChannel systems require MCA-specific network drivers.', 0

;-----------------------------------------------------------------------------
; hardware_detect_all - Detection orchestrator
;
; Comprehensive hardware detection - scan ISA and EISA buses for 3C509B
; and 3C515-TX cards. Also checks for unsupported bus types (EISA, MCA, VLB)
; and warns user accordingly.
;
; Input:  None
; Output: AX = number of devices detected
; Uses:   All registers
;-----------------------------------------------------------------------------
hardware_detect_all:
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si
        push    di

        ; Comprehensive hardware detection - scan ISA and EISA buses for 3C509B and 3C515-TX cards

        mov     bx, 0               ; Device counter

        ; Try to detect 3C509B (ISA bus)
        call    detect_3c509b
        cmp     ax, 0
        je      .no_3c509b

        ; Found 3C509B - store in first slot
        mov     byte [hw_instances], HW_STATE_DETECTED
        mov     byte [hw_types], 1      ; Type 1 = 3C509B
        mov     [hw_io_bases], ax           ; Store I/O base
        inc     bx

.no_3c509b:
        ; Try to detect 3C515-TX (ISA bus with bus mastering)
        call    detect_3c515
        cmp     ax, 0
        je      .no_3c515

        ; Found 3C515 - store in second slot (if available)
        cmp     bx, MAX_HW_INSTANCES
        jae     .no_more_slots

        mov     si, hw_instances
        add     si, bx
        mov     byte [si], HW_STATE_DETECTED
        mov     si, hw_types
        add     si, bx
        mov     byte [si], 2            ; Type 2 = 3C515
        mov     si, hw_io_bases
        shl     bx, 1                       ; Convert to word offset
        add     si, bx
        mov     [si], ax                    ; Store I/O base
        shr     bx, 1                       ; Restore byte offset
        inc     bx

.no_more_slots:
.no_3c515:
        ; Check for unsupported bus NICs and warn user
        push    bx                          ; Save device count

        ; Check for EISA NICs (stub)
        call    nic_detect_eisa_3c592
        mov     cx, ax
        call    nic_detect_eisa_3c597
        or      ax, cx
        jz      .check_mca
        push    msg_eisa_not_supported
        call    log_warning
        add     sp, 2

.check_mca:
        ; Check if this is an MCA system
        call    is_mca_system
        or      ax, ax
        jz      .check_vlb          ; Not MCA, skip MCA checks

        ; It's an MCA system - check for MCA NICs
        call    nic_detect_mca_3c523
        mov     cx, ax
        call    nic_detect_mca_3c529
        or      ax, cx
        jz      .mca_no_nics        ; No MCA NICs found

        ; MCA NICs found - warn user
        push    msg_mca_not_supported
        call    log_warning
        add     sp, 2

.mca_no_nics:
        ; All MCA systems are pure MCA - no ISA slots available
        push    msg_pure_mca_error1
        call    log_error
        add     sp, 2
        push    msg_pure_mca_error2
        call    log_error
        add     sp, 2
        push    msg_pure_mca_error3
        call    log_error
        add     sp, 2

        ; Set device count to 0 and exit early
        pop     bx                  ; Restore saved device count
        xor     ax, ax              ; Return 0 devices
        jmp     .early_exit

.check_vlb:
        ; Check for VLB NICs (stub)
        call    nic_detect_vlb
        or      ax, ax
        jz      .done_checking
        ; VLB already logs its own message

.done_checking:
        pop     bx                          ; Restore device count

        ; Return number of devices detected
        mov     ax, bx

.early_exit:
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
;; end hardware_detect_all

;-----------------------------------------------------------------------------
; detect_3c509b - Detect 3C509B ISA card using proper LFSR ID sequence
;
; Uses the standard 3Com isolation protocol to detect 3C509B cards on the
; ISA bus. Sends a 255-byte LFSR sequence to isolate cards, then reads
; the EEPROM to verify 3Com signature and retrieve I/O base configuration.
;
; Input:  None
; Output: AX = I/O base address if found, 0 if not found
; Uses:   All registers
;-----------------------------------------------------------------------------
detect_3c509b:
detect_3c509b_device:           ; Alias for callers expecting _device suffix
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si
        push    di

        ; Perform 3C509B ID sequence using LFSR table
        ; This is the standard 3Com isolation protocol

        ; Reset all cards to known state
        mov     dx, 0110h               ; Standard ID port
        mov     al, 0C0h                ; Global reset command
        out     dx, al

        ; Small delay after reset
        mov     cx, 1000
.reset_delay:
        loop    .reset_delay

        ; Send ID sequence to isolate cards
        ; Use LFSR sequence: 255 bytes starting with 0xFF
        mov     al, 0                   ; Start with 0x00
        out     dx, al
        mov     al, 0                   ; Second 0x00
        out     dx, al

        ; Send 255-byte LFSR sequence
        mov     si, lfsr_table
        mov     cx, LFSR_TABLE_SIZE     ; 255 bytes

.id_sequence_loop:
        lodsb                           ; Load byte from LFSR table
        out     dx, al                  ; Send to ID port
        loop    .id_sequence_loop

        ; Reset tag for first card detection
        mov     al, 0D0h                ; Set tag register command
        out     dx, al

        ; Read EEPROM address 7 to check for 3Com signature
        mov     al, 87h                 ; Read EEPROM location 7
        out     dx, al

        ; Wait for EEPROM ready (15us minimum from Linux driver)
        mov     cx, 50                  ; Approximately 15us at typical CPU speeds
.eeprom_wait1:
        nop
        loop    .eeprom_wait1

        ; Read the data bit by bit (3C509B protocol)
        mov     cx, 16                  ; 16 bits
        mov     bx, 0                   ; Accumulator

.read_eeprom_loop:
        shl     bx, 1                   ; Shift accumulator left
        in      al, dx                  ; Read from ID port
        and     al, 01h                 ; Mask to get LSB
        or      bl, al                  ; OR into accumulator
        loop    .read_eeprom_loop

        ; Check for 3Com product ID (0x6D50)
        cmp     bx, 6D50h
        jne     .not_found

        ; Found 3Com card - read I/O base configuration
        mov     al, 88h                 ; Read EEPROM location 8 (I/O config)
        out     dx, al

        ; Wait for EEPROM ready
        mov     cx, 50
.eeprom_wait2:
        nop
        loop    .eeprom_wait2

        ; Read I/O base configuration
        mov     cx, 16
        mov     bx, 0

.read_iobase_loop:
        shl     bx, 1
        in      al, dx
        and     al, 01h
        or      bl, al
        loop    .read_iobase_loop

        ; Calculate I/O base address
        ; Bottom 5 bits give the base, top bits give interface type
        and     bx, 001Fh               ; Mask to get base address bits
        shl     bx, 4                   ; Multiply by 16
        add     bx, 0200h               ; Add base offset

        ; Activate the card at this I/O address
        mov     al, bl                  ; Low byte of I/O base
        shr     al, 4                   ; Shift to get address code
        or      al, 0E0h                ; Add activate command
        out     dx, al

        ; Small delay after activation
        mov     cx, 2000
.activate_delay:
        loop    .activate_delay

        ; Verify the card responds at calculated address
        mov     dx, bx                  ; Use calculated I/O base
        add     dx, 0Eh                 ; Command/Status register

        ; Select window 0 for identification
        mov     ax, (1 << 11) | 0       ; Select window 0 command
        out     dx, ax

        ; Small delay for window selection
        mov     cx, 100
.window_delay:
        loop    .window_delay

        ; Read from the card to verify presence
        in      ax, dx
        ; If we get all 1s, card is not there
        cmp     ax, 0FFFFh
        je      .not_found

        ; Success - return I/O base address
        mov     ax, bx
        jmp     .exit

.not_found:
        mov     ax, 0                   ; Return 0 if not found

.exit:
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
;; end detect_3c509b

;-----------------------------------------------------------------------------
; detect_3c515 - ISA bus scan for 3C515-TX
;
; Scans ISA bus from 0x100 to 0x3E0 in steps of 0x20 to detect 3C515-TX
; cards. The 3C515 is an ISA card with bus mastering capability.
;
; Input:  None
; Output: AX = I/O base address if found, 0 if not found
; Uses:   BX, CX, DX, SI
;-----------------------------------------------------------------------------
detect_3c515:
detect_3c515_device:            ; Alias for callers expecting _device suffix
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si

        ; Scan ISA bus from 0x100 to 0x3E0 in steps of 0x20
        ; 3C515 is an ISA card, NOT PCI!
        mov     dx, C515_ISA_MIN_IO     ; Start at 0x100
        mov     cx, ((C515_ISA_MAX_IO - C515_ISA_MIN_IO) / C515_ISA_STEP) + 1

.scan_loop:
        ; Try current I/O address
        call    probe_3c515_at_address
        cmp     ax, 0
        jne     .found

        ; Move to next address (step 0x20)
        add     dx, 20h
        loop    .scan_loop

        ; Not found
        mov     ax, 0
        jmp     .exit

.found:
        ; Return the I/O base where card was found
        mov     ax, dx

.exit:
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
;; end detect_3c515

;-----------------------------------------------------------------------------
; probe_3c515_at_address - Probe for 3C515 at specific address
;
; Probes a specific I/O address to determine if a 3C515-TX card is present.
; Reads EEPROM to verify product ID and 3Com manufacturer ID.
;
; Input:  DX = I/O base address to probe
; Output: AX = 1 if found, 0 if not found
; Uses:   AX, BX, CX, SI
;-----------------------------------------------------------------------------
probe_3c515_at_address:
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    si
        push    dx

        ; Select window 0 for EEPROM access
        push    dx
        add     dx, 0Eh                 ; Command register
        mov     ax, (1 << 11) | 0       ; Select window 0
        out     dx, ax
        pop     dx

        ; Small delay for window selection
        mov     cx, 100
.delay1:
        nop
        loop    .delay1

        ; Try to read EEPROM to identify 3C515-TX
        ; 3C515 uses different EEPROM access at base+0x0A in Window 0
        push    dx
        add     dx, 0Ah                 ; EEPROM command register (ISA offset)
        mov     ax, 80h | 3             ; Read command for ModelID location
        out     dx, ax
        pop     dx

        ; Wait 162us for EEPROM (from Linux driver analysis)
        mov     cx, 162
.eeprom_delay:
        call    delay_1us               ; 1 microsecond delay
        loop    .eeprom_delay

        ; Read EEPROM data
        push    dx
        add     dx, 0Ch                 ; EEPROM data register (ISA offset)
        in      ax, dx
        pop     dx

        ; Check for 3C515-TX product ID (0x5051 with revision mask)
        and     ax, 0F0FFh              ; Mask off revision nibble
        cmp     ax, 5051h               ; 3C515-TX product ID
        je      .found_3c515

        ; Not found
        mov     ax, 0
        jmp     .exit

.found_3c515:
        ; Verify it's really a 3C515 by checking 3Com ID
        push    dx
        add     dx, 200Ah               ; EEPROM command register
        mov     ax, 80h | 7             ; Read EtherLink3ID location
        out     dx, ax
        pop     dx

        ; Wait for EEPROM
        mov     cx, 162
.eeprom_delay2:
        call    delay_1us
        loop    .eeprom_delay2

        ; Read 3Com ID
        push    dx
        add     dx, 200Ch               ; EEPROM data register
        in      ax, dx
        pop     dx

        cmp     ax, 6D50h               ; 3Com manufacturer ID
        jne     .not_3com

        ; Found valid 3C515-TX
        mov     ax, 1
        jmp     .exit

.not_3com:
        mov     ax, 0

.exit:
        pop     dx
        pop     si
        pop     cx
        pop     bx
        pop     bp
        ret
;; end probe_3c515_at_address

;-----------------------------------------------------------------------------
; delay_1us - 1 microsecond delay
;
; Provides an approximate 1 microsecond delay. This is CPU-dependent and
; serves as a rough estimate suitable for EEPROM timing requirements.
;
; Input:  None
; Output: None
; Uses:   None (preserves all registers)
;-----------------------------------------------------------------------------
delay_1us:
        push    cx
        ; Approximate 1us delay (CPU-dependent, rough estimate)
        mov     cx, 3                   ; Adjust for CPU speed
.delay_loop:
        nop
        loop    .delay_loop
        pop     cx
        ret
;; end delay_1us
