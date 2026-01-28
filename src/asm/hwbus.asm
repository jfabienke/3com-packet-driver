;=============================================================================
; hwbus.asm - Unsupported Bus Detection (MCA/EISA/VLB)
;
; Purpose: Detect NICs on unsupported bus types and inform users
;
; This module provides DETECTION ONLY for:
; - MCA (Micro Channel): 3C523 EtherLink/MC, 3C529 EtherLink III/MC
; - EISA: 3C592, 3C597
; - VLB (VESA Local Bus): Generic probe
;
; When detected, users are informed that only ISA NICs (3C509B, 3C515-TX)
; are supported by this driver.
;
; Safety: Read-only detection, restores all system state
; This is a COLD section module (discarded after driver load)
;
; Created: 2026-01-25 from hardware.asm modularization
; Last modified: 2026-01-25 11:52:00
;=============================================================================

        bits    16
        cpu     386

;-----------------------------------------------------------------------------
; Includes
;-----------------------------------------------------------------------------
; C symbol naming bridge (maps C symbols to _symbol)
%include "csym.inc"

%include "asm_interfaces.inc"

;-----------------------------------------------------------------------------
; External references
;-----------------------------------------------------------------------------
        extern  log_warning
        extern  log_error

;-----------------------------------------------------------------------------
; Global exports
;-----------------------------------------------------------------------------
        global  is_mca_system
        global  get_ps2_model
        global  safe_mca_port_read
        global  nic_detect_mca_3c523
        global  nic_detect_mca_3c529
        global  is_eisa_system
        global  nic_detect_eisa_3c592
        global  nic_detect_eisa_3c597
        global  nic_detect_vlb

;=============================================================================
; CODE SECTION - COLD (discarded after load)
;=============================================================================
segment COLD_TEXT class=CODE public use16

;-----------------------------------------------------------------------------
; is_mca_system - Quick MCA system check
;
; Input:  None
; Output: AX = 1 if MCA system, 0 if not
; Uses:   AX, BX, ES (preserves all)
;-----------------------------------------------------------------------------
is_mca_system:
        push    bx
        push    cx
        push    es

        ; Use INT 15h, AH=C0h to get system configuration
        mov     ah, 0C0h            ; Get System Configuration
        int     15h
        jc      .not_mca            ; Carry set = no config info

        ; ES:BX points to config table
        ; First byte is the table length - must check before accessing
        mov     al, [es:bx]         ; Get table length
        cmp     al, 6               ; Need at least 6 bytes to access feature byte 1
        jb      .not_mca            ; Table too short, not safe to check

        ; Check feature byte 1 at offset 5
        mov     al, [es:bx+5]       ; Feature byte 1
        test    al, 02h             ; Bit 1 = MicroChannel
        jz      .not_mca

        ; MCA system detected
        mov     ax, 1
        jmp     .exit

.not_mca:
        xor     ax, ax              ; Not MCA

.exit:
        pop     es
        pop     cx
        pop     bx
        ret
;; end is_mca_system

;-----------------------------------------------------------------------------
; get_ps2_model - Get PS/2 model number
;
; Input:  None
; Output: AX = PS/2 model number (0 if not PS/2 or unknown)
; Uses:   AX, BX, ES
;-----------------------------------------------------------------------------
get_ps2_model:
        push    bx
        push    es

        ; Get system configuration
        mov     ah, 0C0h
        int     15h
        jc      .unknown_model

        ; ES:BX points to system configuration table
        ; Byte at offset 2 is model byte
        mov     al, [es:bx+2]       ; Model byte
        mov     ah, 0               ; Clear high byte

        ; Check for known PS/2 MCA models
        cmp     al, 50h             ; Model 50
        je      .valid_model
        cmp     al, 56h             ; Model 56
        je      .valid_model
        cmp     al, 57h             ; Model 57
        je      .valid_model
        cmp     al, 60h             ; Model 60
        je      .valid_model
        cmp     al, 70h             ; Model 70
        je      .valid_model
        cmp     al, 76h             ; Model 76
        je      .valid_model
        cmp     al, 77h             ; Model 77
        je      .valid_model
        cmp     al, 80h             ; Model 80
        je      .valid_model
        cmp     al, 85h             ; Model 85
        je      .valid_model
        cmp     al, 90h             ; Model 90
        je      .valid_model
        cmp     al, 95h             ; Model 95
        je      .valid_model

        ; Unknown or non-PS/2 model
        jmp     .unknown_model

.valid_model:
        ; Return model number in AX
        jmp     .exit

.unknown_model:
        xor     ax, ax              ; Return 0 for unknown

.exit:
        pop     es
        pop     bx
        ret
;; end get_ps2_model

;-----------------------------------------------------------------------------
; safe_mca_port_read - Safely read MCA POS port with timeout
;
; Input:  None
; Output: CF = 0 if port exists, CF = 1 if not
;         AL = port value if exists
; Uses:   AX, CX, DX
;-----------------------------------------------------------------------------
safe_mca_port_read:
        push    cx
        push    dx

        mov     cx, 3               ; Retry count

.retry:
        mov     dx, 96h             ; POS control port

        ; Check if port responds
        in      al, dx
        cmp     al, 0FFh            ; Non-existent port?
        jne     .port_exists

        loop    .retry

        ; Port doesn't exist
        stc                         ; Set carry = error
        jmp     .exit

.port_exists:
        clc                         ; Clear carry = success

.exit:
        pop     dx
        pop     cx
        ret
;; end safe_mca_port_read

;-----------------------------------------------------------------------------
; nic_detect_mca_3c523 - Stub for MCA 3C523 detection
;
; Purpose: Detect but not support MCA NICs, inform user
; Input:  None
; Output: AX = 1 if MCA NIC found (but unsupported), 0 if not
; Uses:   All registers
;-----------------------------------------------------------------------------
nic_detect_mca_3c523:
        push    bx
        push    cx
        push    dx
        push    si

        ; Save original POS state
        mov     dx, 96h
        in      al, dx
        push    ax                  ; Save original state

        ; Quick check if this is even an MCA system
        call    is_mca_system
        or      ax, ax
        jz      .no_mca_nic         ; Not MCA system, skip

        ; Scan MCA slots for 3C523
        xor     cx, cx              ; Start with slot 0
.scan_loop:
        cmp     cx, 8               ; Validate slot number
        jae     .no_mca_nic         ; Prevent overflow

        mov     dx, 96h
        mov     al, cl
        and     al, 07h             ; Mask to valid slot range
        or      al, 08h             ; Enable POS for slot
        out     dx, al

        ; Small delay for POS enable
        push    cx
        mov     cx, 10
.delay1:
        nop
        loop    .delay1
        pop     cx

        ; Read adapter ID
        mov     dx, 100h
        in      al, dx              ; ID low byte
        mov     ah, al
        inc     dx
        in      al, dx              ; ID high byte

        cmp     ax, 6042h           ; 3C523 adapter ID
        je      .found_3c523

        ; Disable POS and try next slot
        push    cx
        mov     dx, 96h
        xor     al, al
        out     dx, al

        ; Small delay
        mov     cx, 10
.delay2:
        nop
        loop    .delay2
        pop     cx

        inc     cx
        cmp     cx, 8               ; 8 slots max
        jb      .scan_loop

.no_mca_nic:
        xor     ax, ax              ; No MCA NIC found
        jmp     .exit

.found_3c523:
        ; Disable POS
        mov     dx, 96h
        xor     al, al
        out     dx, al

        ; Log detection message
        push    cx                  ; Save slot number
        push    msg_3c523_found
        call    log_warning
        add     sp, 4

        mov     ax, 1               ; Found but not supported

.exit:
        ; Restore original POS state
        pop     bx                  ; Get saved state
        mov     dx, 96h
        mov     al, bl
        out     dx, al

        pop     si
        pop     dx
        pop     cx
        pop     bx
        ret
;; end nic_detect_mca_3c523

;-----------------------------------------------------------------------------
; nic_detect_mca_3c529 - Stub for MCA 3C529 detection
;
; Purpose: Detect but not support MCA 3C529, inform user
; Input:  None
; Output: AX = 1 if MCA NIC found (but unsupported), 0 if not
; Uses:   All registers
;-----------------------------------------------------------------------------
nic_detect_mca_3c529:
        push    bx
        push    cx
        push    dx
        push    si

        ; Save original POS state
        mov     dx, 96h
        in      al, dx
        push    ax                  ; Save original state

        ; Quick check if this is even an MCA system
        call    is_mca_system
        or      ax, ax
        jz      .no_mca_nic         ; Not MCA system, skip

        ; Scan MCA slots for 3C529
        xor     cx, cx              ; Start with slot 0
.scan_loop:
        cmp     cx, 8               ; Validate slot number
        jae     .no_mca_nic         ; Prevent overflow

        mov     dx, 96h
        mov     al, cl
        and     al, 07h             ; Mask to valid slot range
        or      al, 08h             ; Enable POS for slot
        out     dx, al

        ; Small delay for POS enable
        push    cx
        mov     cx, 10
.delay1:
        nop
        loop    .delay1
        pop     cx

        ; Read adapter ID
        mov     dx, 100h
        in      al, dx              ; ID low byte
        mov     ah, al
        inc     dx
        in      al, dx              ; ID high byte

        cmp     ax, 627Ch           ; 3C529 adapter ID
        je      .found_3c529

        ; Disable POS and try next slot
        push    cx
        mov     dx, 96h
        xor     al, al
        out     dx, al

        ; Small delay
        mov     cx, 10
.delay2:
        nop
        loop    .delay2
        pop     cx

        inc     cx
        cmp     cx, 8               ; 8 slots max
        jb      .scan_loop

.no_mca_nic:
        xor     ax, ax              ; No MCA NIC found
        jmp     .exit

.found_3c529:
        ; Disable POS
        mov     dx, 96h
        xor     al, al
        out     dx, al

        ; Log detection message
        push    cx                  ; Save slot number
        push    msg_3c529_found
        call    log_warning
        add     sp, 4

        mov     ax, 1               ; Found but not supported

.exit:
        ; Restore original POS state
        pop     bx                  ; Get saved state
        mov     dx, 96h
        mov     al, bl
        out     dx, al

        pop     si
        pop     dx
        pop     cx
        pop     bx
        ret
;; end nic_detect_mca_3c529

;-----------------------------------------------------------------------------
; is_eisa_system - Check if system has EISA bus
;
; Input:  None
; Output: AX = 1 if EISA system, 0 if not
; Uses:   AX, DX (preserves all)
;-----------------------------------------------------------------------------
is_eisa_system:
        push    dx

        ; Check for EISA by reading the "EISA" signature from slot 0
        ; EISA systems have ASCII "EISA" at ports 0xC80-0xC83
        ; This is 286-compatible (no 32-bit instructions)

        mov     dx, 0C80h           ; EISA slot 0 ID port
        in      al, dx              ; Read first byte
        cmp     al, 'E'             ; Check for 'E'
        jne     .not_eisa

        inc     dx                  ; Port 0xC81
        in      al, dx              ; Read second byte
        cmp     al, 'I'             ; Check for 'I'
        jne     .not_eisa

        inc     dx                  ; Port 0xC82
        in      al, dx              ; Read third byte
        cmp     al, 'S'             ; Check for 'S'
        jne     .not_eisa

        inc     dx                  ; Port 0xC83
        in      al, dx              ; Read fourth byte
        cmp     al, 'A'             ; Check for 'A'
        jne     .not_eisa

        ; Found "EISA" signature - this is an EISA system
        mov     ax, 1               ; EISA system
        jmp     .exit

.not_eisa:
        xor     ax, ax              ; Not EISA

.exit:
        pop     dx
        ret
;; end is_eisa_system

;-----------------------------------------------------------------------------
; nic_detect_eisa_3c592 - Stub for EISA 3C592 detection
;
; Purpose: Detect but not support EISA NICs, inform user
; Input:  None
; Output: AX = 1 if EISA NIC found (but unsupported), 0 if not
; Uses:   All registers
;-----------------------------------------------------------------------------
nic_detect_eisa_3c592:
        push    bx
        push    cx
        push    dx

        ; Quick check if this is even an EISA system
        call    is_eisa_system
        or      ax, ax
        jz      .no_eisa_nic

        ; Scan EISA slots 1-15 (slot 0 is motherboard)
        mov     cx, 1               ; Start with slot 1
.scan_loop:
        ; Calculate EISA ID port for this slot
        mov     dx, cx
        shl     dx, 12              ; Slot * 0x1000
        add     dx, 0C80h           ; EISA ID register offset

        ; Read EISA adapter ID (32-bit)
        in      eax, dx

        ; Check for 3C592 ID (TCM5920)
        ; EISA IDs are encoded, but we can check for patterns
        cmp     eax, 0FFFFFFFFh     ; Empty slot
        je      .next_slot

        ; Check if this looks like a 3Com card
        ; 3Com EISA prefix is "TCM" encoded
        and     eax, 0FFFFFFh       ; Mask upper byte
        cmp     eax, 204D4354h      ; "TCM " pattern (simplified check)
        jne     .next_slot

        ; Found potential 3Com EISA card
        push    cx
        push    msg_eisa_3c592_found
        call    log_warning
        add     sp, 4

        mov     ax, 1               ; Found but not supported
        jmp     .exit

.next_slot:
        inc     cx
        cmp     cx, 16              ; Check slots 1-15
        jb      .scan_loop

.no_eisa_nic:
        xor     ax, ax              ; No EISA NIC found

.exit:
        pop     dx
        pop     cx
        pop     bx
        ret
;; end nic_detect_eisa_3c592

;-----------------------------------------------------------------------------
; nic_detect_eisa_3c597 - Detect EISA 3C597 (Fast EtherLink)
;
; Purpose: Detect but not support EISA 3C597, inform user
; Input:  None
; Output: AX = 1 if EISA NIC found (but unsupported), 0 if not
; Uses:   All registers
;
; Note: 3C597 is the 100Mbps Fast EtherLink EISA card
;       EISA ID: TCM5970 (encoded as per EISA spec)
;-----------------------------------------------------------------------------
nic_detect_eisa_3c597:
        push    bx
        push    cx
        push    dx

        ; Quick check if this is even an EISA system
        call    is_eisa_system
        or      ax, ax
        jz      .no_eisa_nic

        ; Scan EISA slots 1-15 (slot 0 is motherboard)
        mov     cx, 1               ; Start with slot 1
.scan_loop:
        ; Calculate EISA ID port for this slot
        mov     dx, cx
        shl     dx, 12              ; Slot * 0x1000
        add     dx, 0C80h           ; EISA ID register offset

        ; Read EISA adapter ID (32-bit)
        in      eax, dx

        ; Check for empty slot
        cmp     eax, 0FFFFFFFFh
        je      .next_slot

        ; Check for 3C597 ID pattern (TCM5970 encoded)
        ; 3Com uses manufacturer code 0x6D50 ("TCM")
        and     eax, 0FFFFh         ; Check manufacturer ID
        cmp     ax, 506Dh           ; 3Com EISA manufacturer code
        jne     .next_slot

        ; Found potential 3Com EISA card - check product
        mov     dx, cx
        shl     dx, 12
        add     dx, 0C82h           ; Product ID offset
        in      ax, dx

        ; 3C597 product codes: 5970, 5971, 5972
        cmp     ax, 5970h
        je      .found_3c597
        cmp     ax, 5971h
        je      .found_3c597
        cmp     ax, 5972h
        je      .found_3c597

.next_slot:
        inc     cx
        cmp     cx, 16              ; Check slots 1-15
        jb      .scan_loop

.no_eisa_nic:
        xor     ax, ax              ; No EISA NIC found
        jmp     .exit

.found_3c597:
        push    cx
        push    msg_eisa_3c597_found
        call    log_warning
        add     sp, 4

        mov     ax, 1               ; Found but not supported

.exit:
        pop     dx
        pop     cx
        pop     bx
        ret
;; end nic_detect_eisa_3c597

;-----------------------------------------------------------------------------
; nic_detect_vlb - VESA Local Bus NIC detection (heuristic)
;
; Purpose: Attempt to detect VLB NICs and inform user if found
; Input:  None
; Output: AX = 1 if potential VLB NIC found, 0 if not
; Uses:   AX, DX
;
; Note: VLB has no standard enumeration mechanism like MCA/EISA.
;       This uses heuristic port probing at common VLB NIC addresses.
;       False positives are possible but harmless (just a warning).
;       3Com did not make VLB NICs, so this catches other vendors.
;-----------------------------------------------------------------------------
nic_detect_vlb:
        push    dx

        ; VLB NICs typically use I/O addresses in 0x6000-0x6FFF range
        ; or high ISA addresses. No standard detection exists.

        ; Probe common VLB NIC base addresses
        mov     dx, 6100h           ; Common VLB NIC address
        in      al, dx
        cmp     al, 0FFh            ; 0xFF = no device
        je      .try_next

        ; Secondary check at offset +8 (common status register)
        mov     dx, 6108h
        in      al, dx
        cmp     al, 0FFh
        je      .try_next

        ; Both ports responded - likely a VLB device
        jmp     .found_vlb

.try_next:
        ; Try alternate VLB address range
        mov     dx, 6200h
        in      al, dx
        cmp     al, 0FFh
        je      .no_vlb

        mov     dx, 6208h
        in      al, dx
        cmp     al, 0FFh
        je      .no_vlb

.found_vlb:
        ; Potential VLB NIC detected
        push    msg_vlb_not_supported
        call    log_warning
        add     sp, 2

        mov     ax, 1               ; Found something, not supported
        jmp     .exit

.no_vlb:
        xor     ax, ax              ; No VLB NIC detected

.exit:
        pop     dx
        ret
;; end nic_detect_vlb

;=============================================================================
; DATA SECTION - COLD (discarded after load)
;=============================================================================
segment COLD_DATA class=DATA public use16

;-----------------------------------------------------------------------------
; Message strings
;-----------------------------------------------------------------------------
msg_3c523_found:        db 'MCA: 3C523 EtherLink/MC detected - not supported', 0
msg_3c529_found:        db 'MCA: 3C529 EtherLink III/MC detected - not supported', 0
msg_eisa_3c592_found:   db 'EISA: 3C592 EtherLink III detected - not supported', 0
msg_eisa_3c597_found:   db 'EISA: 3C597 Fast EtherLink detected - not supported', 0
msg_vlb_not_supported:  db 'VLB: Possible VLB NIC detected - not supported', 0
msg_try_isa:            db 'Supported NICs: 3C509B (ISA) or 3C515-TX (ISA)', 0

;=============================================================================
; End of hwbus.asm
;=============================================================================
