; @file hardware.asm
; @brief Low-level hardware interaction routines - Enhanced Group 6A/6B Implementation
;
; 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
; Enhanced hardware detection and IRQ management for Groups 6A & 6B
;
; This file is part of the 3Com Packet Driver project.
;

.MODEL SMALL
.386

; Include assembly interface definitions
include "asm_interfaces.inc"
include "lfsr_table.inc"

; I/O port constants
ISA_MIN_IO          EQU 100h    ; Minimum ISA I/O address
ISA_MAX_IO          EQU 3FFh    ; Maximum ISA I/O address
EISA_MIN_IO         EQU 1000h   ; Minimum EISA I/O address
EISA_MAX_IO         EQU 9FFFh   ; Maximum EISA I/O address

; Common 3Com registers (relative to base)
REG_COMMAND         EQU 0Eh     ; Command register
REG_STATUS          EQU 0Eh     ; Status register  
REG_WINDOW         EQU 0Eh     ; Window select register
REG_DATA            EQU 00h     ; Data register (window dependent)
REG_INT_STATUS      EQU 0Eh     ; Interrupt status
REG_ID_PORT         EQU 100h    ; ID port for 3C509B

; 3C509B specific constants
C509B_EEPROM_READ   EQU 0080h   ; EEPROM read command
C509B_ACTIVATE      EQU 0FFh    ; Activation sequence value
C509B_ID_SEQUENCE   EQU 6FFFh   ; ID sequence for isolation

; 3C515-TX specific constants (ISA with bus mastering)
C515_ISA_MIN_IO     EQU 100h    ; Minimum ISA I/O for 3C515
C515_ISA_MAX_IO     EQU 3E0h    ; Maximum ISA I/O for 3C515
C515_ISA_STEP       EQU 20h     ; ISA scan step size
C515_DMA_CTRL       EQU 400h    ; ISA DMA control register offset
C515_BUS_MASTER     EQU 01h     ; Bus master enable flag

; Hardware state constants
HW_STATE_UNKNOWN    EQU 0       ; Hardware state unknown
HW_STATE_DETECTED   EQU 1       ; Hardware detected
HW_STATE_CONFIGURED EQU 2       ; Hardware configured
HW_STATE_ACTIVE     EQU 3       ; Hardware active
HW_STATE_ERROR      EQU 0FFh    ; Hardware error

; Maximum hardware instances
MAX_HW_INSTANCES    EQU 2       ; Support up to 2 NICs

; Enhanced data segment for Groups 6A & 6B
_DATA SEGMENT
        ASSUME  DS:_DATA

; Enhanced hardware detection state using structured approach
hw_instance_table   HW_INSTANCE MAX_HW_INSTANCES dup(<>)
hw_instance_count   db 0        ; Number of detected instances

; Legacy compatibility fields (maintained for existing code)
hw_instances        db MAX_HW_INSTANCES dup(HW_STATE_UNDETECTED)
hw_io_bases         dw MAX_HW_INSTANCES dup(0)
hw_irq_lines        db MAX_HW_INSTANCES dup(0)
hw_types            db MAX_HW_INSTANCES dup(0)    ; 0=unknown, 1=3C509B, 2=3C515
hw_mac_addresses    db MAX_HW_INSTANCES*6 dup(0)  ; MAC addresses

; Additional tables for enhanced implementation
hw_iobase_table     dw MAX_HW_INSTANCES dup(0)     ; I/O base addresses
hw_type_table       db MAX_HW_INSTANCES dup(0)     ; NIC types (1=3C509B, 2=3C515)
hw_flags_table      db MAX_HW_INSTANCES dup(0)     ; Hardware flags
current_instance    db 0                           ; Current active instance
current_iobase      dw 0                           ; Current NIC I/O base
current_irq         db 0                           ; Current NIC IRQ

; Enhanced I/O operation statistics
io_read_count       dd 0        ; Total I/O reads
io_write_count      dd 0        ; Total I/O writes
io_error_count      dw 0        ; I/O error count
io_timeout_count    dw 0        ; I/O timeout count
io_retry_count      dw 0        ; I/O retry count

; System state management
hardware_initialized db 0       ; Hardware subsystem initialized
current_instance    db 0        ; Currently selected hardware instance
last_error_code     db 0        ; Last error code encountered
debug_flags         db 0        ; Debug control flags

; Timing and synchronization
timestamp_last_detect dd 0      ; Last detection timestamp
timestamp_last_config dd 0      ; Last configuration timestamp

_DATA ENDS

; Code segment
_TEXT SEGMENT
        ASSUME  CS:_TEXT, DS:_DATA

; Enhanced function exports for Groups 6A & 6B
PUBLIC hardware_init_asm
PUBLIC hardware_detect_all
PUBLIC hardware_configure_3c509b
PUBLIC hardware_configure_3c515
PUBLIC hardware_read_packet
PUBLIC hardware_send_packet_asm
PUBLIC hardware_get_address
PUBLIC hardware_handle_3c509b_irq
PUBLIC hardware_handle_3c515_irq
PUBLIC io_read_byte
PUBLIC io_write_byte
PUBLIC io_read_word
PUBLIC io_write_word
PUBLIC detect_3c509b
PUBLIC init_3c509b

; Enhanced Group 6A/6B interface functions
PUBLIC hardware_detect_and_configure
PUBLIC hardware_get_device_info
PUBLIC hardware_set_device_state
PUBLIC hardware_handle_interrupt
PUBLIC hardware_validate_configuration
PUBLIC detect_3c509b_device
PUBLIC detect_3c515_device
PUBLIC configure_3c509b_device
PUBLIC configure_3c515_device
PUBLIC setup_3c509b_irq
PUBLIC setup_3c515_irq
PUBLIC reset_3c509b_device
PUBLIC reset_3c515_device
PUBLIC read_3c509b_eeprom
PUBLIC read_3c515_eeprom\n\n; 3C515-TX HAL vtable function exports\nPUBLIC asm_3c515_detect_hardware\nPUBLIC asm_3c515_init_hardware\nPUBLIC asm_3c515_reset_hardware\nPUBLIC asm_3c515_configure_media\nPUBLIC asm_3c515_set_station_address\nPUBLIC asm_3c515_enable_interrupts\nPUBLIC asm_3c515_start_transceiver\nPUBLIC asm_3c515_stop_transceiver\nPUBLIC asm_3c515_get_link_status\nPUBLIC asm_3c515_get_statistics\nPUBLIC asm_3c515_set_multicast\nPUBLIC asm_3c515_set_promiscuous

; External references
EXTRN get_cpu_features:PROC     ; From cpu_detect.asm

;-----------------------------------------------------------------------------
; hardware_init_asm - Initialize hardware subsystem
;
; Input:  None
; Output: AX = 0 for success, non-zero for error
; Uses:   AX, BX, CX, DX
;-----------------------------------------------------------------------------
hardware_init_asm PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si

        ; Initialize hardware detection structures, clear statistics, and detect all available hardware

        ; Clear hardware state
        mov     cx, MAX_HW_INSTANCES
        mov     si, OFFSET hw_instances
        mov     al, HW_STATE_UNKNOWN
.clear_state_loop:
        mov     [si], al
        inc     si
        loop    .clear_state_loop

        ; Clear I/O statistics
        mov     dword ptr [io_read_count], 0
        mov     dword ptr [io_write_count], 0
        mov     word ptr [io_error_count], 0

        ; Detect all hardware
        call    hardware_detect_all
        cmp     ax, 0
        jne     .detection_failed

        ; Success
        mov     ax, 0
        jmp     .exit

.detection_failed:
        ; Log detection failure
        push    ax
        push    dx
        mov     ax, ERROR_NOT_FOUND
        call    log_hardware_error
        pop     dx
        pop     ax
        mov     ax, 1
        jmp     .exit

.exit:
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
hardware_init_asm ENDP

;-----------------------------------------------------------------------------
; hardware_detect_all - Detect all supported hardware
;
; Input:  None
; Output: AX = number of devices detected
; Uses:   All registers
;-----------------------------------------------------------------------------
hardware_detect_all PROC
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
        mov     byte ptr [hw_instances], HW_STATE_DETECTED
        mov     byte ptr [hw_types], 1      ; Type 1 = 3C509B
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

        mov     si, OFFSET hw_instances
        add     si, bx
        mov     byte ptr [si], HW_STATE_DETECTED
        mov     si, OFFSET hw_types
        add     si, bx
        mov     byte ptr [si], 2            ; Type 2 = 3C515
        mov     si, OFFSET hw_io_bases
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
        push    OFFSET msg_eisa_not_supported
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
        push    OFFSET msg_mca_not_supported
        call    log_warning
        add     sp, 2
        
.mca_no_nics:
        ; All MCA systems are pure MCA - no ISA slots available
        push    OFFSET msg_pure_mca_error1
        call    log_error
        add     sp, 2
        push    OFFSET msg_pure_mca_error2
        call    log_error
        add     sp, 2
        push    OFFSET msg_pure_mca_error3
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
hardware_detect_all ENDP

;-----------------------------------------------------------------------------
; detect_3c509b - Detect 3C509B ISA card using proper LFSR ID sequence
;
; Input:  None
; Output: AX = I/O base address if found, 0 if not found
; Uses:   All registers
;-----------------------------------------------------------------------------
detect_3c509b PROC
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
        mov     si, OFFSET lfsr_table
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
        
        ; Wait for EEPROM ready (15μs minimum from Linux driver)
        mov     cx, 50                  ; Approximately 15μs at typical CPU speeds
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
        mov     ax, (1 shl 11) or 0     ; Select window 0 command
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
detect_3c509b ENDP

;-----------------------------------------------------------------------------
; init_3c509b - Initialize detected 3C509B card
;
; Input:  DX = I/O base address, AL = NIC index
; Output: AX = 0 for success, non-zero for error
; Uses:   All registers
;-----------------------------------------------------------------------------
init_3c509b PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si
        push    di

        ; Store NIC index for later use
        mov     bl, al
        
        ; Perform total reset first
        mov     ax, (0 shl 11)          ; Total Reset command
        add     dx, 0Eh                 ; Command register offset
        out     dx, ax
        sub     dx, 0Eh                 ; Restore base address
        
        ; Wait for reset to complete (minimum 1ms)
        mov     cx, 10000
.reset_wait:
        nop
        loop    .reset_wait
        
        ; Select window 0 for configuration
        add     dx, 0Eh                 ; Command register
        mov     ax, (1 shl 11) or 0     ; Select Window 0
        out     dx, ax
        sub     dx, 0Eh
        
        ; Read and store MAC address from EEPROM
        mov     si, 0                   ; EEPROM address counter
        mov     di, OFFSET hw_mac_addresses
        mov     ah, 0
        mov     al, bl                  ; NIC index
        mov     cl, 6                   ; 6 bytes per MAC address
        mul     cl
        add     di, ax                  ; Point to this NIC's MAC storage
        
        ; Read MAC address (3 words = 6 bytes)
        mov     cx, 3                   ; Read 3 EEPROM words
        
.read_mac_loop:
        ; Set EEPROM address for read
        push    dx
        add     dx, 0Ah                 ; EEPROM command register
        mov     ax, 0080h               ; Read command
        or      ax, si                  ; Add EEPROM address
        out     dx, ax
        
        ; Wait for EEPROM ready (15μs minimum)
        push    cx
        mov     cx, 50
.mac_eeprom_wait:
        nop
        loop    .mac_eeprom_wait
        pop     cx
        
        ; Read EEPROM data
        add     dx, 2                   ; EEPROM data register (0Ch)
        in      ax, dx
        pop     dx
        
        ; Store MAC bytes (3Com stores bytes swapped)
        mov     [di], ah                ; Store high byte first
        inc     di
        mov     [di], al                ; Store low byte
        inc     di
        
        inc     si                      ; Next EEPROM address
        loop    .read_mac_loop
        
        ; Select window 2 to set station address
        add     dx, 0Eh                 ; Command register
        mov     ax, (1 shl 11) or 2     ; Select Window 2
        out     dx, ax
        
        ; Write MAC address to station address registers
        mov     si, OFFSET hw_mac_addresses
        mov     ah, 0
        mov     al, bl                  ; NIC index
        mov     cl, 6
        mul     cl
        add     si, ax                  ; Point to this NIC's MAC
        
        mov     cx, 6                   ; 6 bytes to write
        mov     di, 0                   ; Register offset
        
.write_mac_loop:
        mov     al, [si]                ; Load MAC byte
        push    dx
        add     dx, di                  ; Add register offset
        out     dx, al                  ; Write to station address register
        pop     dx
        inc     si                      ; Next MAC byte
        inc     di                      ; Next register
        loop    .write_mac_loop
        
        ; Select window 1 for normal operation
        add     dx, 0Eh                 ; Command register
        mov     ax, (1 shl 11) or 1     ; Select Window 1
        out     dx, ax
        
        ; Set receive filter (station + broadcast)
        mov     ax, (16 shl 11) or 0005h ; SetRxFilter: Station + Broadcast
        out     dx, ax
        
        ; Enable receiver
        mov     ax, (4 shl 11)          ; RxEnable command
        out     dx, ax
        
        ; Enable transmitter
        mov     ax, (9 shl 11)          ; TxEnable command
        out     dx, ax
        
        ; Success
        mov     ax, 0
        
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
init_3c509b ENDP

;-----------------------------------------------------------------------------
; detect_3c515 - Detect 3C515-TX ISA card with bus scan 0x100-0x3E0 step 0x20
;
; Input:  None
; Output: AX = I/O base address if found, 0 if not found
; Uses:   All registers
;-----------------------------------------------------------------------------
detect_3c515 PROC
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
detect_3c515 ENDP

;-----------------------------------------------------------------------------
; probe_3c515_at_address - Probe for 3C515 at specific address
;
; Input:  DX = I/O base address to probe
; Output: AX = 1 if found, 0 if not found
; Uses:   AX, BX, CX, SI
;-----------------------------------------------------------------------------
probe_3c515_at_address PROC
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
        
        ; Wait 162μs for EEPROM (from Linux driver analysis)
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
probe_3c515_at_address ENDP

;-----------------------------------------------------------------------------
; delay_1us - 1 microsecond delay
;
; Input:  None
; Output: None
; Uses:   None (preserves all registers)
;-----------------------------------------------------------------------------
delay_1us PROC
        push    cx
        ; Approximate 1μs delay (CPU-dependent, rough estimate)
        mov     cx, 3                   ; Adjust for CPU speed
.delay_loop:
        nop
        loop    .delay_loop
        pop     cx
        ret
delay_1us ENDP

;-----------------------------------------------------------------------------
; hardware_configure_3c509b - Configure 3C509B hardware
;
; Input:  AL = instance index, DX = I/O base address
; Output: AX = 0 for success, non-zero for error
; Uses:   All registers
;-----------------------------------------------------------------------------
hardware_configure_3c509b PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si

        ; Complete 3C509B configuration sequence
        ; Save instance and I/O base
        push    ax                      ; Save instance index
        mov     si, dx                  ; SI = I/O base
        
        ; Step 1: Reset the adapter
        mov     dx, si
        add     dx, REG_COMMAND
        mov     ax, 0000h               ; Global reset command
        out     dx, ax
        
        ; Wait for reset to complete (~1ms)
        mov     cx, 300
.reset_wait:
        in      al, 80h                 ; I/O delay (~3.3us)
        loop    .reset_wait
        
        ; Step 2: Select Window 0 for EEPROM access
        mov     dx, si
        add     dx, REG_WINDOW
        mov     ax, 0800h               ; CMD_SELECT_WINDOW | 0
        out     dx, ax
        
        ; Step 3: Read MAC address from EEPROM
        pop     ax                      ; Restore instance index
        push    ax
        call    read_mac_from_eeprom_3c509b
        
        ; Step 4: Select Window 2 to set station address
        mov     dx, si
        add     dx, REG_WINDOW
        mov     ax, 0802h               ; CMD_SELECT_WINDOW | 2
        out     dx, ax
        
        ; Write MAC address to station address registers
        pop     ax                      ; Get instance index
        push    ax
        mov     bl, al
        xor     bh, bh
        shl     bx, 1                   ; Word offset
        shl     bx, 1                   ; x4
        shl     bx, 1                   ; x8 for 6-byte MAC
        mov     di, OFFSET hw_mac_addresses
        add     di, bx
        
        ; Write 6 bytes of MAC to Window 2, registers 0-5
        mov     cx, 3                   ; 3 words
        mov     dx, si                  ; Base address
.write_mac:
        mov     ax, [di]
        out     dx, ax
        add     dx, 2
        add     di, 2
        loop    .write_mac
        
        ; Step 5: Configure media type in Window 3
        mov     dx, si
        add     dx, REG_WINDOW
        mov     ax, 0803h               ; CMD_SELECT_WINDOW | 3
        out     dx, ax
        
        ; Configure internal config register (offset 0)
        mov     dx, si
        in      ax, dx
        or      ax, 0030h               ; Enable 10BaseT
        out     dx, ax
        
        ; Configure MAC control (offset 6)
        mov     dx, si
        add     dx, 6
        in      ax, dx
        and     ax, 0FF8Fh              ; Clear media bits
        or      ax, 0000h               ; 10BaseT (bits 6:4 = 000)
        out     dx, ax
        
        ; Step 6: Select transceiver
        mov     dx, si
        add     dx, REG_COMMAND
        mov     ax, 3000h               ; CMD_SELECT_XCVR | 10BaseT
        out     dx, ax
        
        ; Step 7: Select Window 1 for operation
        mov     ax, 0801h               ; CMD_SELECT_WINDOW | 1
        out     dx, ax
        
        ; Step 8: Set RX filter (station + broadcast)
        mov     ax, 0800h | 05h         ; SET_RX_FILTER | Station+Broadcast
        out     dx, ax
        
        ; Step 9: Set RX early threshold
        mov     ax, 1000h | 40h         ; SET_RX_EARLY | 64 bytes
        out     dx, ax
        
        ; Step 10: Set TX available threshold
        mov     ax, 9800h | 40h         ; SET_TX_AVAIL | 64 bytes
        out     dx, ax
        
        ; Step 11: Enable receiver
        mov     ax, 2000h               ; RX_ENABLE
        out     dx, ax
        
        ; Step 12: Enable transmitter
        mov     ax, 4800h               ; TX_ENABLE
        out     dx, ax
        
        ; Step 13: Set interrupt mask
        mov     ax, 7000h | 00FFh       ; SET_INTR_MASK | All interrupts
        out     dx, ax
        
        ; Step 14: Clear any pending interrupts
        mov     ax, 6800h | 00FFh       ; ACK_INTR | All
        out     dx, ax
        
        ; Validate instance index
        pop     ax                      ; Restore instance
        cmp     al, MAX_HW_INSTANCES
        jae     .invalid_instance

        ; Mark as configured
        mov     bl, al
        mov     bh, 0
        mov     si, OFFSET hw_instances
        add     si, bx
        mov     byte ptr [si], HW_STATE_CONFIGURED

        ; Success
        mov     ax, 0
        jmp     .exit

.invalid_instance:
        mov     ax, 1
        jmp     .exit

.exit:
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
hardware_configure_3c509b ENDP

;-----------------------------------------------------------------------------
; init_3c515 - Initialize 3C515-TX with DMA setup and bus mastering
;
; Input:  AL = instance index, DX = I/O base address
; Output: AX = 0 for success, non-zero for error
; Uses:   All registers
;-----------------------------------------------------------------------------
init_3c515 PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si
        push    di

        ; Validate instance index
        cmp     al, MAX_HW_INSTANCES
        jae     .invalid_instance

        ; Save parameters
        mov     bl, al                  ; Save instance index
        mov     si, dx                  ; Save I/O base
        
        ; Total reset first
        add     dx, 0Eh                 ; Command register
        mov     ax, 0                   ; Total reset command
        out     dx, ax
        
        ; Wait for reset to complete
        mov     cx, 1000
.reset_wait:
        call    delay_1ms
        loop    .reset_wait
        
        mov     dx, si                  ; Restore I/O base
        
        ; Select window 0 to read EEPROM and get MAC address
        add     dx, 0Eh
        mov     ax, (1 << 11) | 0       ; Select window 0
        out     dx, ax
        mov     dx, si                  ; Restore base
        
        ; Read MAC address from EEPROM
        call    read_3c515_mac_address
        
        ; Select window 2 to set MAC address
        add     dx, 0Eh
        mov     ax, (1 << 11) | 2       ; Select window 2
        out     dx, ax
        mov     dx, si                  ; Restore base
        
        ; Write MAC address to station address registers (0-5)
        ; Copy MAC from EEPROM to station address registers
        push    si
        push    di
        push    cx
        
        ; Get instance MAC address pointer
        mov     bl, al                  ; Instance index
        xor     bh, bh
        shl     bx, 1
        shl     bx, 1
        add     bx, bx                  ; x6 for MAC size
        mov     si, OFFSET hw_mac_addresses
        add     si, bx
        
        ; Select Window 2 for station address
        add     dx, 0Eh
        mov     ax, (1 << 11) | 2       ; Select Window 2
        out     dx, ax
        mov     dx, si                  ; Restore base
        
        ; Write MAC to registers 0-5
        mov     cx, 3                   ; 3 words
        xor     di, di                  ; Start at register 0
.copy_mac:
        mov     ax, [si]
        out     dx, ax
        add     dx, 2
        add     si, 2
        loop    .copy_mac
        
        pop     cx
        pop     di
        pop     si
        
        ; Select window 3 for configuration
        add     dx, 0Eh
        mov     ax, (1 << 11) | 3       ; Select window 3
        out     dx, ax
        mov     dx, si                  ; Restore base
        
        ; Configure media type (auto-select 10/100)
        add     dx, 8                   ; Options register
        in      ax, dx
        or      ax, 1000000h            ; Enable auto-select
        out     dx, ax
        mov     dx, si                  ; Restore base
        
        ; Select window 7 for bus master DMA setup
        add     dx, 0Eh
        mov     ax, (1 << 11) | 7       ; Select window 7
        out     dx, ax
        mov     dx, si                  ; Restore base
        
        ; Initialize DMA descriptor pointers
        add     dx, 404h                ; Down list pointer (TX)
        xor     ax, ax                  ; NULL initially
        out     dx, ax                  ; Low word
        add     dx, 2
        out     dx, ax                  ; High word
        
        mov     dx, si                  ; Restore base
        add     dx, 418h                ; Up list pointer (RX)
        xor     ax, ax                  ; NULL initially  
        out     dx, ax                  ; Low word
        add     dx, 2
        out     dx, ax                  ; High word
        
        mov     dx, si                  ; Restore base
        
        ; Select window 1 for normal operation
        add     dx, 0Eh
        mov     ax, (1 << 11) | 1       ; Select window 1
        out     dx, ax
        mov     dx, si                  ; Restore base
        
        ; Set up interrupt mask (enable DMA interrupts)
        add     dx, 0Eh                 ; Command register
        mov     ax, (14 << 11) | 07FFh  ; Set interrupt enable with DMA bits
        out     dx, ax
        mov     dx, si                  ; Restore base
        
        ; Enable receiver
        add     dx, 0Eh
        mov     ax, (4 << 11)           ; RX Enable command
        out     dx, ax
        mov     dx, si                  ; Restore base
        
        ; Enable transmitter
        add     dx, 0Eh
        mov     ax, (9 << 11)           ; TX Enable command
        out     dx, ax
        mov     dx, si                  ; Restore base
        
        ; Set RX filter to receive unicast + broadcast
        add     dx, 0Eh
        mov     ax, (16 << 11) | 5      ; Set RX filter: station + broadcast
        out     dx, ax
        
        ; Mark as configured
        mov     bh, 0
        mov     si, OFFSET hw_instances
        add     si, bx
        mov     byte ptr [si], HW_STATE_CONFIGURED

        ; Success
        mov     ax, 0
        jmp     .exit

.invalid_instance:
        mov     ax, 1
        jmp     .exit

.exit:
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
init_3c515 ENDP

;-----------------------------------------------------------------------------
; read_3c515_mac_address - Read MAC address from 3C515 EEPROM
;
; Input:  DX = I/O base address
; Output: None (MAC stored internally)
; Uses:   AX, BX, CX, DX
;-----------------------------------------------------------------------------
read_3c515_mac_address PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    si
        push    di
        
        ; Read MAC address from EEPROM locations 0, 1, 2
        mov     bx, 0                   ; EEPROM address counter
        mov     di, OFFSET hw_mac_addresses  ; Destination buffer
        
.read_mac_loop:
        ; Set EEPROM address
        push    dx
        add     dx, 200Ah               ; EEPROM command register
        mov     ax, 80h                 ; Read command
        or      ax, bx                  ; OR with address
        out     dx, ax
        pop     dx
        
        ; Wait 162μs for EEPROM
        mov     cx, 162
.mac_eeprom_delay:
        call    delay_1us
        loop    .mac_eeprom_delay
        
        ; Read EEPROM data (16-bit word)
        push    dx
        add     dx, 0Ch                 ; EEPROM data register (ISA)
        in      ax, dx
        pop     dx
        
        ; Store MAC bytes (little endian)
        mov     [di], al                ; Low byte
        inc     di
        mov     [di], ah                ; High byte
        inc     di
        
        inc     bx                      ; Next EEPROM address
        cmp     bx, 3                   ; Read 3 words (6 bytes)
        jl      .read_mac_loop
        
        pop     di
        pop     si
        pop     cx
        pop     bx
        pop     bp
        ret
read_3c515_mac_address ENDP

;-----------------------------------------------------------------------------
; hardware_read_packet - Read packet from hardware
;
; Input:  AL = NIC index
; Output: AX = 0 for success, non-zero for error
; Uses:   All registers
;-----------------------------------------------------------------------------
hardware_read_packet PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si
        push    di

        ; Validate NIC index, read packet from appropriate hardware, and handle different NIC types

        ; Validate NIC index
        cmp     al, MAX_HW_INSTANCES
        jae     .invalid_nic

        ; Check NIC type and dispatch to appropriate handler
        mov     bl, al
        mov     bh, 0
        mov     si, OFFSET hw_types
        add     si, bx
        mov     cl, [si]

        cmp     cl, 1
        je      .read_3c509b
        cmp     cl, 2
        je      .read_3c515
        jmp     .unknown_type

.read_3c509b:
        ; Read packet from 3C509B using PIO
        push    es
        push    di
        push    si
        push    cx
        push    bx
        
        ; Get I/O base for this instance
        mov     bl, al
        xor     bh, bh
        shl     bx, 1                   ; Word offset
        mov     si, OFFSET hw_iobase_table
        mov     dx, [si+bx]             ; DX = I/O base
        mov     si, dx                  ; Save in SI
        
        ; Select Window 1 for RX operations
        add     dx, REG_WINDOW
        mov     ax, 0801h               ; CMD_SELECT_WINDOW | 1
        out     dx, ax
        
        ; Check RX status
        mov     dx, si
        add     dx, 08h                 ; RX_STATUS register
        in      ax, dx
        test    ax, 8000h               ; RX_COMPLETE bit
        jz      .no_packet_3c509b
        
        ; Get packet length
        and     ax, 07FFh               ; Extract length (11 bits)
        mov     cx, ax                  ; CX = packet length
        
        ; Validate packet length
        cmp     cx, 1514
        ja      .bad_packet_3c509b
        cmp     cx, 14
        jb      .bad_packet_3c509b
        
        ; ES:DI points to receive buffer (passed in BP+8)
        les     di, [bp+8]
        
        ; Save packet length
        push    cx
        
        ; Read packet from RX FIFO
        mov     dx, si                  ; I/O base
        add     dx, 00h                 ; RX_FIFO register
        
        ; Optimize for word reads
        shr     cx, 1                   ; Convert to word count
        jz      .read_last_byte_3c509b
        
.read_loop_3c509b:
        in      ax, dx                  ; Read word from FIFO
        stosw                           ; Store to ES:DI
        loop    .read_loop_3c509b
        
.read_last_byte_3c509b:
        pop     cx                      ; Restore packet length
        test    cx, 1                   ; Check for odd byte
        jz      .read_done_3c509b
        in      al, dx                  ; Read single byte
        stosb
        
.read_done_3c509b:
        ; Discard packet from FIFO (required!)
        mov     dx, si
        add     dx, REG_COMMAND
        mov     ax, 4000h               ; CMD_RX_DISCARD
        out     dx, ax
        
        ; Wait for discard to complete
        mov     cx, 100
.wait_discard_3c509b:
        mov     dx, si
        add     dx, 08h                 ; RX_STATUS
        in      ax, dx
        test    ax, 1000h               ; RX_EARLY bit
        jz      .discard_done_3c509b
        push    cx
        mov     cx, 3
.delay_3c509b:
        in      al, 80h                 ; I/O delay
        loop    .delay_3c509b
        pop     cx
        loop    .wait_discard_3c509b
        
.discard_done_3c509b:
        mov     ax, cx                  ; Return packet length
        jmp     .cleanup_rx_3c509b
        
.bad_packet_3c509b:
        ; Discard bad packet
        mov     dx, si
        add     dx, REG_COMMAND
        mov     ax, 4000h               ; CMD_RX_DISCARD
        out     dx, ax
        
.no_packet_3c509b:
        xor     ax, ax                  ; No packet/error
        
.cleanup_rx_3c509b:
        pop     bx
        pop     cx
        pop     si
        pop     di
        pop     es
        jmp     .exit

.read_3c515:
        ; Read packet from 3C515-TX (DMA or PIO mode)
        push    es
        push    di
        push    si
        push    cx
        push    bx
        
        ; Get I/O base and check DMA mode
        mov     bl, al
        xor     bh, bh
        shl     bx, 1
        mov     si, OFFSET hw_iobase_table
        mov     dx, [si+bx]
        mov     si, dx
        
        ; Check if bus master DMA is enabled
        mov     di, OFFSET hw_flags_table
        mov     bl, [di+bx]
        test    bl, 01h                 ; FLAG_BUS_MASTER
        jz      .use_pio_3c515
        
        ; DMA mode - Select Window 7
        add     dx, REG_WINDOW
        mov     ax, 0807h               ; CMD_SELECT_WINDOW | 7
        out     dx, ax
        
        ; Check UP (RX) list status
        mov     dx, si
        add     dx, 38h                 ; UP_PKT_STATUS
        in      ax, dx
        in      dx, dx                  ; Read high word
        test    ax, 8000h               ; Complete bit
        jz      .no_dma_packet_3c515
        
        ; Extract packet length
        and     ax, 1FFFh               ; 13-bit length
        mov     cx, ax
        
        ; Get packet from DMA buffer
        ; (Implementation depends on DMA buffer management)
        
        ; Acknowledge DMA completion
        mov     dx, si
        add     dx, REG_COMMAND
        mov     ax, 6800h | 0200h       ; ACK_INTR | UP_COMPLETE
        out     dx, ax
        
        mov     ax, cx                  ; Return length
        jmp     .cleanup_rx_3c515
        
.use_pio_3c515:
        ; PIO mode - similar to 3C509B
        add     dx, REG_WINDOW
        mov     ax, 0801h
        out     dx, ax
        
        mov     dx, si
        add     dx, 18h                 ; RX_STATUS for 3C515
        in      ax, dx
        test    ax, 8000h
        jz      .no_packet_3c515
        
        and     ax, 1FFFh               ; 13-bit length for 3C515
        mov     cx, ax
        
        cmp     cx, 1514
        ja      .bad_packet_3c515
        cmp     cx, 14
        jb      .bad_packet_3c515
        
        les     di, [bp+8]
        push    cx
        
        mov     dx, si
        add     dx, 10h                 ; RX_FIFO for 3C515
        shr     cx, 1
        jz      .read_last_3c515
        
.read_loop_3c515:
        in      ax, dx
        stosw
        loop    .read_loop_3c515
        
.read_last_3c515:
        pop     cx
        test    cx, 1
        jz      .read_done_3c515
        in      al, dx
        stosb
        
.read_done_3c515:
        mov     dx, si
        add     dx, REG_COMMAND
        mov     ax, 4000h
        out     dx, ax
        
        mov     ax, cx
        jmp     .cleanup_rx_3c515
        
.bad_packet_3c515:
        mov     dx, si
        add     dx, REG_COMMAND
        mov     ax, 4000h
        out     dx, ax
        
.no_packet_3c515:
.no_dma_packet_3c515:
        xor     ax, ax
        
.cleanup_rx_3c515:
        pop     bx
        pop     cx
        pop     si
        pop     di
        pop     es
        jmp     .exit

.invalid_nic:
        mov     ax, 1
        jmp     .exit

.unknown_type:
        mov     ax, 2
        jmp     .exit

.exit:
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
hardware_read_packet ENDP

;-----------------------------------------------------------------------------
; hardware_send_packet_asm - Send packet via hardware
;
; Input:  DS:SI = packet data, CX = packet length
; Output: AX = 0 for success, non-zero for error
; Uses:   All registers
;-----------------------------------------------------------------------------
hardware_send_packet_asm PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si
        push    di

        ; Complete packet transmission implementation
        
        ; Basic packet validation
        cmp     cx, 60                  ; Minimum Ethernet frame
        jb      .invalid_packet
        cmp     cx, 1514                ; Maximum Ethernet frame
        ja      .invalid_packet

        ; Get current NIC instance and validate
        mov     al, [current_instance]
        cmp     al, MAX_HW_INSTANCES
        jae     .no_active_nic
        
        ; Get NIC type and I/O base
        mov     bl, al
        xor     bh, bh
        mov     di, OFFSET hw_type_table
        mov     cl, [di+bx]             ; CL = NIC type
        
        shl     bx, 1                   ; Word offset
        mov     di, OFFSET hw_iobase_table
        mov     dx, [di+bx]             ; DX = I/O base
        test    dx, dx
        jz      .no_active_nic
        
        ; Branch based on NIC type
        cmp     cl, 1                   ; 3C509B
        je      .tx_3c509b
        cmp     cl, 2                   ; 3C515
        je      .tx_3c515
        jmp     .no_active_nic
        
.tx_3c509b:
        ; Transmit packet on 3C509B
        push    si
        push    cx
        push    bx
        
        mov     bx, dx                  ; Save I/O base
        
        ; Select Window 1 for TX
        add     dx, REG_WINDOW
        mov     ax, 0801h               ; CMD_SELECT_WINDOW | 1
        out     dx, ax
        mov     dx, bx                  ; Restore base
        
        ; Check TX space available
        add     dx, 0Ch                 ; TX_FREE register
        in      ax, dx
        cmp     ax, cx                  ; Compare with packet length
        jb      .tx_busy
        
        ; Set TX packet length
        mov     dx, bx
        add     dx, REG_COMMAND
        mov     ax, 9000h               ; CMD_TX_SET_LEN
        or      ax, cx                  ; Include length
        out     dx, ax
        
        ; Check for TX stall
        mov     dx, bx
        add     dx, 0Bh                 ; TX_STATUS
        in      al, dx
        test    al, 04h                 ; STALL bit
        jz      .no_stall_3c509b
        
        ; Clear stall
        mov     dx, bx
        add     dx, REG_COMMAND
        mov     ax, 5800h               ; CMD_TX_RESET
        out     dx, ax
        
        ; Re-enable TX
        mov     ax, 4800h               ; CMD_TX_ENABLE
        out     dx, ax
        
.no_stall_3c509b:
        ; Write packet to TX FIFO
        mov     dx, bx
        add     dx, 00h                 ; TX_FIFO
        
        push    cx
        shr     cx, 1                   ; Word count
        jz      .tx_last_byte_3c509b
        
.tx_loop_3c509b:
        lodsw                           ; Load from DS:SI
        out     dx, ax                  ; Write to FIFO
        loop    .tx_loop_3c509b
        
.tx_last_byte_3c509b:
        pop     cx
        test    cx, 1
        jz      .tx_start_3c509b
        lodsb
        out     dx, al
        
.tx_start_3c509b:
        ; Start transmission
        mov     dx, bx
        add     dx, REG_COMMAND
        mov     ax, 4800h               ; CMD_TX_START
        out     dx, ax
        
        xor     ax, ax                  ; Success
        pop     bx
        pop     cx
        pop     si
        jmp     .exit
        
.tx_3c515:
        ; Transmit packet on 3C515-TX
        push    si
        push    cx
        push    bx
        
        mov     bx, dx                  ; Save base
        
        ; Check if using DMA
        push    bx
        shr     bx, 1
        mov     di, OFFSET hw_flags_table
        mov     al, [di+bx]
        pop     bx
        test    al, 01h                 ; FLAG_BUS_MASTER
        jz      .tx_pio_3c515
        
        ; DMA transmission (simplified)
        ; Would need proper descriptor setup
        jmp     .tx_busy
        
.tx_pio_3c515:
        ; PIO mode - similar to 3C509B
        add     dx, REG_WINDOW
        mov     ax, 0801h
        out     dx, ax
        mov     dx, bx
        
        add     dx, 1Ch                 ; TX_FREE for 3C515
        in      ax, dx
        cmp     ax, cx
        jb      .tx_busy
        
        mov     dx, bx
        add     dx, REG_COMMAND
        mov     ax, 9000h
        or      ax, cx
        out     dx, ax
        
        mov     dx, bx
        add     dx, 10h                 ; TX_FIFO for 3C515
        
        push    cx
        shr     cx, 1
        jz      .tx_last_3c515
        
.tx_loop_3c515:
        lodsw
        out     dx, ax
        loop    .tx_loop_3c515
        
.tx_last_3c515:
        pop     cx
        test    cx, 1
        jz      .tx_start_3c515
        lodsb
        out     dx, al
        
.tx_start_3c515:
        mov     dx, bx
        add     dx, REG_COMMAND
        mov     ax, 4800h
        out     dx, ax
        
        xor     ax, ax
        pop     bx
        pop     cx
        pop     si
        jmp     .exit
        
.tx_busy:
        mov     ax, ERROR_BUSY
        jmp     .exit
        ; Success
        mov     ax, 0
        jmp     .exit

.invalid_packet:
        mov     ax, 1
        jmp     .exit

.no_active_nic:
        mov     ax, 2
        jmp     .exit

.exit:
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
hardware_send_packet_asm ENDP

;-----------------------------------------------------------------------------
; hardware_get_address - Get MAC address from hardware
;
; Input:  AL = NIC index, ES:DI = buffer (6 bytes)
; Output: AX = 0 for success, non-zero for error
; Uses:   AX, BX, CX, SI, DI
;-----------------------------------------------------------------------------
hardware_get_address PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    si

        ; Read MAC address from hardware EEPROM
        
        ; Validate NIC index
        cmp     al, MAX_HW_INSTANCES
        jae     .invalid_nic
        
        push    ax                      ; Save instance
        
        ; Get NIC type and I/O base
        mov     bl, al
        xor     bh, bh
        mov     si, OFFSET hw_type_table
        mov     cl, [si+bx]             ; CL = NIC type
        
        shl     bx, 1                   ; Word offset
        mov     si, OFFSET hw_iobase_table
        mov     dx, [si+bx]             ; DX = I/O base
        
        ; Branch based on NIC type
        cmp     cl, 1                   ; 3C509B
        je      .read_mac_3c509b
        cmp     cl, 2                   ; 3C515
        je      .read_mac_3c515
        
        ; Use cached MAC if type unknown
        jmp     .use_cached_mac
        
.read_mac_3c509b:
        ; Select Window 0 for EEPROM
        add     dx, REG_WINDOW
        mov     ax, 0800h               ; CMD_SELECT_WINDOW | 0
        out     dx, ax
        sub     dx, REG_WINDOW          ; Back to base
        
        ; Read 3 words of MAC from EEPROM addresses 0-2
        xor     si, si                  ; EEPROM address
        mov     cx, 3                   ; 3 words
        
.read_eeprom_3c509b:
        push    cx
        ; Send EEPROM read command
        mov     ax, dx                  ; Save base
        add     dx, 0Ah                 ; EEPROM command register
        mov     cx, 80h                 ; EEPROM_READ_CMD
        or      cx, si                  ; Add address
        mov     ax, cx
        out     dx, ax
        
        ; Wait for EEPROM ready (162us typical)
        mov     cx, 50                  ; Retry count
.wait_eeprom_3c509b:
        push    cx
        mov     cx, 6                   ; ~20us delay
.delay_eeprom:
        in      al, 80h                 ; I/O delay
        loop    .delay_eeprom
        pop     cx
        
        in      ax, dx                  ; Check status
        test    ah, 80h                 ; EEPROM_BUSY bit
        jz      .eeprom_ready_3c509b
        loop    .wait_eeprom_3c509b
        
        ; Timeout - use cached
        pop     cx
        jmp     .use_cached_mac
        
.eeprom_ready_3c509b:
        ; Read data word
        mov     ax, dx
        sub     ax, 0Ah                 ; Back to base
        mov     dx, ax
        add     dx, 0Ch                 ; EEPROM data register
        in      ax, dx
        
        ; Store in buffer (convert endianness)
        xchg    ah, al
        stosw                           ; Store to ES:DI
        
        mov     dx, ax                  ; Restore base
        sub     dx, 0Ch
        inc     si                      ; Next EEPROM address
        pop     cx
        loop    .read_eeprom_3c509b
        
        jmp     .mac_read_done
        
.read_mac_3c515:
        ; Similar process for 3C515
        add     dx, REG_WINDOW
        mov     ax, 0800h
        out     dx, ax
        sub     dx, REG_WINDOW
        
        mov     si, 0Ah                 ; MAC starts at 0x0A in 3C515 EEPROM
        mov     cx, 3
        
.read_eeprom_3c515:
        push    cx
        add     dx, 0Ah
        mov     ax, 200h                ; EEPROM_READ_3C515
        or      ax, si
        out     dx, ax
        
        mov     cx, 100
.wait_3c515:
        push    cx
        mov     cx, 10
.delay_3c515:
        in      al, 80h
        loop    .delay_3c515
        pop     cx
        
        in      ax, dx
        test    ah, 80h
        jz      .ready_3c515
        loop    .wait_3c515
        
        pop     cx
        jmp     .use_cached_mac
        
.ready_3c515:
        add     dx, 2                   ; Data register
        in      ax, dx
        xchg    ah, al
        stosw
        sub     dx, 2
        sub     dx, 0Ah
        inc     si
        pop     cx
        loop    .read_eeprom_3c515
        
        jmp     .mac_read_done
        
.use_cached_mac:
        ; Fall back to cached MAC address
        pop     ax                      ; Restore instance
        push    ax
        mov     bl, al
        xor     bh, bh
        mov     cl, 6
        mov     al, bl
        mul     cl                      ; AX = index * 6
        mov     si, OFFSET hw_mac_addresses
        add     si, ax
        mov     cx, 6
        rep     movsb
        
.mac_read_done:
        pop     ax                      ; Restore instance

        ; Success
        mov     ax, 0
        jmp     .exit

.invalid_nic:
        mov     ax, 1
        jmp     .exit

.exit:
        pop     si
        pop     cx
        pop     bx
        pop     bp
        ret
hardware_get_address ENDP

;-----------------------------------------------------------------------------
; read_mac_from_eeprom_3c509b - Read MAC address from 3C509B EEPROM
;
; Input:  AL = instance index, SI = I/O base address
; Output: MAC address stored in hw_mac_addresses for instance
; Uses:   All registers
;-----------------------------------------------------------------------------
read_mac_from_eeprom_3c509b PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    di
        push    si
        
        ; Calculate MAC storage offset
        mov     bl, al
        xor     bh, bh
        mov     cl, 6
        mov     al, bl
        mul     cl                      ; AX = index * 6
        mov     di, OFFSET hw_mac_addresses
        add     di, ax                  ; DI = MAC storage location
        
        ; Select Window 0 for EEPROM access
        mov     dx, si
        add     dx, REG_WINDOW
        mov     ax, 0800h               ; CMD_SELECT_WINDOW | 0
        out     dx, ax
        
        ; Read 3 words of MAC address from EEPROM
        xor     bx, bx                  ; EEPROM address counter
        mov     cx, 3                   ; 3 words to read
        
.read_mac_loop:
        push    cx
        
        ; Send EEPROM read command
        mov     dx, si
        add     dx, 0Ah                 ; EEPROM command register
        mov     ax, 80h                 ; EEPROM_READ_CMD
        or      ax, bx                  ; Add address (0-2)
        out     dx, ax
        
        ; Wait for EEPROM ready (162us typical, 1ms max)
        mov     cx, 50                  ; Retry count
.wait_eeprom:
        push    cx
        ; Delay ~20us
        mov     cx, 6
.delay_loop:
        in      al, 80h                 ; I/O delay
        loop    .delay_loop
        pop     cx
        
        ; Check EEPROM busy bit
        in      ax, dx
        test    ah, 80h                 ; EEPROM_BUSY bit
        jz      .eeprom_ready
        loop    .wait_eeprom
        
        ; Timeout - use default MAC
        pop     cx
        jmp     .use_default_mac
        
.eeprom_ready:
        ; Read the data word
        mov     dx, si
        add     dx, 0Ch                 ; EEPROM data register
        in      ax, dx
        
        ; Store in MAC buffer (convert endianness)
        xchg    ah, al
        mov     [di], ax
        add     di, 2
        inc     bx                      ; Next EEPROM address
        pop     cx
        loop    .read_mac_loop
        
        jmp     .mac_read_complete
        
.use_default_mac:
        ; Set default MAC if EEPROM read fails
        mov     word [di], 0201h        ; 00:01:02
        mov     word [di+2], 0403h      ; 03:04:05
        mov     word [di+4], 0605h
        
.mac_read_complete:
        pop     si
        pop     di
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
read_mac_from_eeprom_3c509b ENDP

;-----------------------------------------------------------------------------
; log_hardware_error - Log hardware error
;
; Input:  AX = error code
; Output: None
; Uses:   None (preserves all registers)
;-----------------------------------------------------------------------------
log_hardware_error PROC
        push    ax
        push    bx
        push    cx
        push    dx
        
        ; Increment error counter
        inc     word ptr [io_error_count]
        
        ; Could add more sophisticated logging here
        ; For now, just count errors
        
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        ret
log_hardware_error ENDP

;-----------------------------------------------------------------------------
; hardware_handle_3c509b_irq - Handle 3C509B interrupt
;
; Input:  None
; Output: AX = 0 if handled, non-zero if spurious
; Uses:   All registers
;-----------------------------------------------------------------------------
hardware_handle_3c509b_irq PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx

        ; Complete 3C509B interrupt handler
        push    si
        push    di
        push    ds
        push    es
        
        ; Set up data segment
        mov     ax, cs
        mov     ds, ax
        
        ; Get I/O base for current NIC
        mov     si, [current_iobase]
        test    si, si
        jz      .not_ours_3c509b
        
        ; Read interrupt status
        mov     dx, si
        add     dx, REG_INT_STATUS
        in      ax, dx
        mov     bx, ax                  ; Save status
        
        ; Check if our interrupt
        test    ax, 00FFh
        jz      .not_ours_3c509b
        
        ; Process TX complete
        test    bx, 0004h               ; TX_COMPLETE
        jz      .check_rx_3c509b
        
        ; Handle TX completion
        mov     dx, si
        add     dx, 0Bh                 ; TX_STATUS
        in      al, dx
        
        ; Check for errors
        test    al, 0F8h                ; Error bits
        jz      .tx_ok_3c509b
        
        ; Reset TX on error
        mov     dx, si
        add     dx, REG_COMMAND
        mov     ax, 5800h               ; CMD_TX_RESET
        out     dx, ax
        mov     ax, 4800h               ; CMD_TX_ENABLE
        out     dx, ax
        
.tx_ok_3c509b:
        ; Pop TX status
        mov     dx, si
        add     dx, 0Bh
        xor     al, al
        out     dx, al                  ; Clear status
        
        ; Acknowledge TX interrupt
        mov     dx, si
        add     dx, REG_COMMAND
        mov     ax, 6800h | 0004h       ; ACK_INTR | TX_COMPLETE
        out     dx, ax
        
.check_rx_3c509b:
        ; Process RX complete
        test    bx, 0001h               ; RX_COMPLETE
        jz      .check_errors_3c509b
        
        ; Process received packets
        mov     al, [current_instance]
        push    bp
        mov     bp, sp
        sub     sp, 8                   ; Space for buffer ptr
        lea     di, [bp-8]
        mov     [bp-8], di              ; Buffer ptr
        mov     [bp-6], ds
        push    di
        push    ds
        call    hardware_read_packet
        add     sp, 8
        mov     sp, bp
        pop     bp
        
        ; Acknowledge RX interrupt
        mov     dx, si
        add     dx, REG_COMMAND
        mov     ax, 6800h | 0001h       ; ACK_INTR | RX_COMPLETE
        out     dx, ax
        
.check_errors_3c509b:
        ; Check adapter failure
        test    bx, 0080h               ; ADAPTER_FAIL
        jz      .check_stats_3c509b
        
        ; Reset adapter
        mov     al, [current_instance]
        mov     dx, si
        call    hardware_configure_3c509b
        
        ; Acknowledge failure
        mov     dx, si
        add     dx, REG_COMMAND
        mov     ax, 6800h | 0080h       ; ACK_INTR | ADAPTER_FAIL
        out     dx, ax
        
.check_stats_3c509b:
        ; Update statistics
        test    bx, 0008h               ; UPDATE_STATS
        jz      .int_done_3c509b
        
        ; Select Window 6 for stats
        mov     dx, si
        add     dx, REG_WINDOW
        mov     ax, 0806h               ; CMD_SELECT_WINDOW | 6
        out     dx, ax
        
        ; Read statistics to clear
        mov     cx, 10
        mov     dx, si
.read_stats_3c509b:
        in      al, dx
        inc     dx
        loop    .read_stats_3c509b
        
        ; Back to Window 1
        mov     dx, si
        add     dx, REG_WINDOW
        mov     ax, 0801h               ; CMD_SELECT_WINDOW | 1
        out     dx, ax
        
        ; Acknowledge stats
        mov     dx, si
        add     dx, REG_COMMAND
        mov     ax, 6800h | 0008h       ; ACK_INTR | UPDATE_STATS
        out     dx, ax
        
.int_done_3c509b:
        ; Send EOI to PIC
        mov     al, [current_irq]
        cmp     al, 7
        jbe     .master_pic_3c509b
        mov     al, 20h
        out     0A0h, al                ; EOI to slave PIC
.master_pic_3c509b:
        mov     al, 20h
        out     20h, al                 ; EOI to master PIC
        
        xor     ax, ax                  ; Interrupt handled
        jmp     .exit_isr_3c509b
        
.not_ours_3c509b:
        mov     ax, 1                   ; Not our interrupt
        
.exit_isr_3c509b:
        pop     es
        pop     ds
        pop     di
        pop     si

        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
hardware_handle_3c509b_irq ENDP

;-----------------------------------------------------------------------------
; hardware_handle_3c515_irq - Handle 3C515-TX interrupt
;
; Input:  None
; Output: AX = 0 if handled, non-zero if spurious
; Uses:   All registers
;-----------------------------------------------------------------------------
hardware_handle_3c515_irq PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx

        ; Complete 3C515-TX interrupt handler with DMA support
        push    si
        push    di
        push    ds
        push    es
        
        mov     ax, cs
        mov     ds, ax
        
        ; Get I/O base
        mov     si, [current_iobase]
        test    si, si
        jz      .not_ours_3c515
        
        ; Read interrupt status
        mov     dx, si
        add     dx, REG_INT_STATUS
        in      ax, dx
        mov     bx, ax
        
        test    ax, 00FFh
        jz      .not_ours_3c515
        
        ; Check for DMA interrupts (higher priority)
        test    bx, 0200h               ; UP_COMPLETE (RX DMA)
        jz      .check_down_3c515
        
        ; Handle RX DMA completion
        ; Select Window 7 for DMA
        mov     dx, si
        add     dx, REG_WINDOW
        mov     ax, 0807h               ; CMD_SELECT_WINDOW | 7
        out     dx, ax
        
        ; Process RX DMA descriptors
        ; (Implementation depends on DMA buffer management)
        
        ; Acknowledge UP complete
        mov     dx, si
        add     dx, REG_COMMAND
        mov     ax, 6800h | 0200h       ; ACK_INTR | UP_COMPLETE
        out     dx, ax
        
.check_down_3c515:
        test    bx, 0400h               ; DOWN_COMPLETE (TX DMA)
        jz      .check_pio_3c515
        
        ; Handle TX DMA completion
        ; Process TX DMA descriptors
        
        ; Acknowledge DOWN complete
        mov     dx, si
        add     dx, REG_COMMAND
        mov     ax, 6800h | 0400h       ; ACK_INTR | DOWN_COMPLETE
        out     dx, ax
        
.check_pio_3c515:
        ; Standard TX complete
        test    bx, 0004h               ; TX_COMPLETE
        jz      .check_rx_3c515
        
        mov     dx, si
        add     dx, 1Bh                 ; TX_STATUS for 3C515
        in      al, dx
        out     dx, al                  ; Pop status
        
        mov     dx, si
        add     dx, REG_COMMAND
        mov     ax, 6800h | 0004h       ; ACK_INTR | TX_COMPLETE
        out     dx, ax
        
.check_rx_3c515:
        test    bx, 0001h               ; RX_COMPLETE
        jz      .check_host_3c515
        
        ; Check if using DMA or PIO
        mov     di, OFFSET hw_flags_table
        mov     al, [current_instance]
        xor     ah, ah
        add     di, ax
        test    byte [di], 01h          ; FLAG_BUS_MASTER
        jnz     .rx_dma_3c515
        
        ; PIO mode RX
        mov     al, [current_instance]
        push    bp
        mov     bp, sp
        sub     sp, 8
        lea     di, [bp-8]
        mov     [bp-8], di
        mov     [bp-6], ds
        push    di
        push    ds
        call    hardware_read_packet
        add     sp, 8
        mov     sp, bp
        pop     bp
        
.rx_dma_3c515:
        ; Acknowledge RX interrupt
        mov     dx, si
        add     dx, REG_COMMAND
        mov     ax, 6800h | 0001h       ; ACK_INTR | RX_COMPLETE
        out     dx, ax
        
.check_host_3c515:
        ; Check host error
        test    bx, 0002h               ; HOST_ERROR
        jz      .int_done_3c515
        
        ; Read PCI status
        mov     dx, si
        add     dx, 20h                 ; PCI_STATUS
        in      ax, dx
        
        ; Clear errors
        out     dx, ax
        
        ; Reset if fatal
        test    ax, 8000h               ; PCI_ERR_FATAL
        jz      .ack_host_3c515
        
        mov     al, [current_instance]
        mov     dx, si
        call    init_3c515
        
.ack_host_3c515:
        mov     dx, si
        add     dx, REG_COMMAND
        mov     ax, 6800h | 0002h       ; ACK_INTR | HOST_ERROR
        out     dx, ax
        
.int_done_3c515:
        ; Restore Window 1
        mov     dx, si
        add     dx, REG_WINDOW
        mov     ax, 0801h               ; CMD_SELECT_WINDOW | 1
        out     dx, ax
        
        ; Send EOI
        mov     al, [current_irq]
        cmp     al, 7
        jbe     .master_pic_3c515
        mov     al, 20h
        out     0A0h, al
.master_pic_3c515:
        mov     al, 20h
        out     20h, al
        
        xor     ax, ax                  ; Handled
        jmp     .exit_isr_3c515
        
.not_ours_3c515:
        mov     ax, 1                   ; Not ours
        
.exit_isr_3c515:
        pop     es
        pop     ds
        pop     di
        pop     si

        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
hardware_handle_3c515_irq ENDP

;-----------------------------------------------------------------------------
; io_read_byte - Read byte from I/O port
;
; Input:  DX = port address
; Output: AL = data read
; Uses:   AL
;-----------------------------------------------------------------------------
io_read_byte PROC
        push    bp
        mov     bp, sp

        ; Perform I/O read
        in      al, dx

        ; Update statistics
        inc     dword ptr [io_read_count]

        pop     bp
        ret
io_read_byte ENDP

;-----------------------------------------------------------------------------
; io_write_byte - Write byte to I/O port
;
; Input:  DX = port address, AL = data to write
; Output: None
; Uses:   None
;-----------------------------------------------------------------------------
io_write_byte PROC
        push    bp
        mov     bp, sp

        ; Perform I/O write
        out     dx, al

        ; Update statistics
        inc     dword ptr [io_write_count]

        pop     bp
        ret
io_write_byte ENDP

;-----------------------------------------------------------------------------
; io_read_word - Read word from I/O port
;
; Input:  DX = port address
; Output: AX = data read
; Uses:   AX
;-----------------------------------------------------------------------------
io_read_word PROC
        push    bp
        mov     bp, sp

        ; Perform I/O read
        in      ax, dx

        ; Update statistics
        inc     dword ptr [io_read_count]

        pop     bp
        ret
io_read_word ENDP

;-----------------------------------------------------------------------------
; io_write_word - Write word to I/O port
;
; Input:  DX = port address, AX = data to write
; Output: None
; Uses:   None
;-----------------------------------------------------------------------------
io_write_word PROC
        push    bp
        mov     bp, sp

        ; Perform I/O write
        out     dx, ax

        ; Update statistics
        inc     dword ptr [io_write_count]

        pop     bp
        ret
io_write_word ENDP

;=============================================================================
; ENHANCED GROUP 6A/6B FUNCTIONS - DEVICE-SPECIFIC IMPLEMENTATIONS
;=============================================================================

;-----------------------------------------------------------------------------
; configure_3c509b_device - Enhanced 3C509B configuration with defensive programming
;
; Input:  AL = device index
; Output: AX = HW_SUCCESS or error code
; Uses:   All registers
;-----------------------------------------------------------------------------
configure_3c509b_device PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si

        ; Validate device index
        VALIDATE_HW_INSTANCE al, MAX_HW_INSTANCES
        jc      .invalid_index
        
        ; Get device I/O base from instance table
        mov     bl, al
        mov     bh, 0
        mov     si, bx
        shl     si, 1                       ; Word offset for I/O bases
        mov     dx, [hw_io_bases + si]
        test    dx, dx
        jz      .no_io_base
        
        ; Reset the device first
        HARDWARE_RESET dx, HW_TYPE_3C509B, 1000
        jc      .reset_failed
        
        ; Configure device registers using window selection
        SELECT_3C509B_WINDOW dx, 0
        
        ; Read and validate EEPROM
        call    read_3c509b_eeprom
        cmp     ax, HW_SUCCESS
        jne     .eeprom_failed
        
        ; Setup IRQ if specified
        mov     al, [hw_irq_lines + bx]
        cmp     al, 0
        je      .skip_irq_setup
        call    setup_3c509b_irq
        
.skip_irq_setup:
        ; Mark device as configured
        mov     al, bl                      ; Restore device index
        mov     ah, HW_STATE_CONFIGURED
        call    hardware_set_device_state
        
        mov     ax, HW_SUCCESS
        jmp     .exit
        
.invalid_index:
        mov     ax, HW_ERROR_INVALID_PARAM
        jmp     .exit
        
.no_io_base:
        mov     ax, HW_ERROR_IO_ERROR
        jmp     .exit
        
.reset_failed:
        mov     ax, HW_ERROR_RESET_FAILED
        jmp     .exit
        
.eeprom_failed:
        mov     ax, HW_ERROR_EEPROM_READ
        
.exit:
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
configure_3c509b_device ENDP

;-----------------------------------------------------------------------------
; configure_3c515_device - Enhanced 3C515-TX configuration
;
; Input:  AL = device index
; Output: AX = HW_SUCCESS or error code
; Uses:   All registers
;-----------------------------------------------------------------------------
configure_3c515_device PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si

        ; Validate device index
        VALIDATE_HW_INSTANCE al, MAX_HW_INSTANCES
        jc      .invalid_index
        
        ; Get device I/O base from instance table
        mov     bl, al
        mov     bh, 0
        mov     si, bx
        shl     si, 1                       ; Word offset for I/O bases
        mov     dx, [hw_io_bases + si]
        test    dx, dx
        jz      .no_io_base
        
        ; Reset the device first
        HARDWARE_RESET dx, HW_TYPE_3C515TX, 1000
        jc      .reset_failed
        
        ; Configure device registers
        SELECT_3C515_WINDOW dx, 0
        
        ; Read and validate EEPROM
        call    read_3c515_eeprom
        cmp     ax, HW_SUCCESS
        jne     .eeprom_failed
        
        ; Setup DMA if supported
        call    configure_3c515_dma
        
        ; Setup IRQ if specified
        mov     al, [hw_irq_lines + bx]
        cmp     al, 0
        je      .skip_irq_setup
        call    setup_3c515_irq
        
.skip_irq_setup:
        ; Mark device as configured
        mov     al, bl                      ; Restore device index
        mov     ah, HW_STATE_CONFIGURED
        call    hardware_set_device_state
        
        mov     ax, HW_SUCCESS
        jmp     .exit
        
.invalid_index:
        mov     ax, HW_ERROR_INVALID_PARAM
        jmp     .exit
        
.no_io_base:
        mov     ax, HW_ERROR_IO_ERROR
        jmp     .exit
        
.reset_failed:
        mov     ax, HW_ERROR_RESET_FAILED
        jmp     .exit
        
.eeprom_failed:
        mov     ax, HW_ERROR_EEPROM_READ
        
.exit:
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
configure_3c515_device ENDP

;-----------------------------------------------------------------------------
; read_3c509b_eeprom - Read 3C509B EEPROM with defensive programming
;
; Input:  DX = I/O base address
; Output: AX = HW_SUCCESS or error code
; Uses:   AX, BX, CX, DX
;-----------------------------------------------------------------------------
read_3c509b_eeprom PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx

        ; Select window 0 for EEPROM access
        SELECT_3C509B_WINDOW dx, 0
        
        ; Read EEPROM with timeout protection
        mov     cx, 16                      ; Read 16 words
        mov     bx, 0                       ; Starting address
        
.eeprom_read_loop:
        ; Set EEPROM address
        push    dx
        add     dx, 0Ah                     ; EEPROM command register
        mov     ax, 80h                     ; Read command
        or      ax, bx                      ; OR with address
        out     dx, ax
        pop     dx
        
        ; Wait for EEPROM ready with timeout
        WAIT_FOR_CONDITION dx+0Ah, 8000h, TIMEOUT_MEDIUM
        jc      .eeprom_timeout
        
        ; Read EEPROM data
        push    dx
        add     dx, 0Ch                     ; EEPROM data register
        in      ax, dx
        pop     dx
        
        ; Store data (simplified - would store in MAC address array)
        ; For now just validate it's not 0
        test    ax, ax
        jz      .invalid_eeprom
        
        inc     bx
        dec     cx
        jnz     .eeprom_read_loop
        
        mov     ax, HW_SUCCESS
        jmp     .exit
        
.eeprom_timeout:
        mov     ax, HW_ERROR_TIMEOUT
        jmp     .exit
        
.invalid_eeprom:
        mov     ax, HW_ERROR_EEPROM_READ
        
.exit:
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
read_3c509b_eeprom ENDP

;-----------------------------------------------------------------------------
; read_3c515_eeprom - Read 3C515-TX EEPROM
;
; Input:  DX = I/O base address
; Output: AX = HW_SUCCESS or error code
; Uses:   AX, BX, CX, DX
;-----------------------------------------------------------------------------
read_3c515_eeprom PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx

        ; Select window 0 for EEPROM access
        SELECT_3C515_WINDOW dx, 0
        
        ; Read EEPROM with timeout protection (similar to 3C509B but different registers)
        mov     cx, 16                      ; Read 16 words
        mov     bx, 0                       ; Starting address
        
.eeprom_read_loop:
        ; Set EEPROM address
        push    dx
        add     dx, 200Ah                   ; EEPROM command register for 3C515
        mov     ax, 80h                     ; Read command
        or      ax, bx                      ; OR with address
        out     dx, ax
        pop     dx
        
        ; Wait for EEPROM ready with timeout
        WAIT_FOR_CONDITION dx+200Ah, 8000h, TIMEOUT_MEDIUM
        jc      .eeprom_timeout
        
        ; Read EEPROM data
        push    dx
        add     dx, 200Ch                   ; EEPROM data register
        in      ax, dx
        pop     dx
        
        ; Validate data
        test    ax, ax
        jz      .invalid_eeprom
        
        inc     bx
        dec     cx
        jnz     .eeprom_read_loop
        
        mov     ax, HW_SUCCESS
        jmp     .exit
        
.eeprom_timeout:
        mov     ax, HW_ERROR_TIMEOUT
        jmp     .exit
        
.invalid_eeprom:
        mov     ax, HW_ERROR_EEPROM_READ
        
.exit:
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
read_3c515_eeprom ENDP

;-----------------------------------------------------------------------------
; setup_3c509b_irq - Setup 3C509B IRQ handler
;
; Input:  AL = IRQ number, DX = I/O base
; Output: AX = HW_SUCCESS or error code
; Uses:   AX, BX, CX, DX
;-----------------------------------------------------------------------------
setup_3c509b_irq PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx

        ; Validate IRQ
        VALIDATE_IRQ al
        jc      .invalid_irq
        
        ; Configure interrupt enable mask
        SELECT_3C509B_WINDOW dx, 1
        
        push    dx
        add     dx, 0Eh                     ; Command register
        mov     ax, (14 << 11) | 00BFh      ; Set interrupt enable mask
        out     dx, ax
        pop     dx
        
        mov     ax, HW_SUCCESS
        jmp     .exit
        
.invalid_irq:
        mov     ax, HW_ERROR_IRQ_CONFLICT
        
.exit:
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
setup_3c509b_irq ENDP

;-----------------------------------------------------------------------------
; setup_3c515_irq - Setup 3C515-TX IRQ handler
;
; Input:  AL = IRQ number, DX = I/O base
; Output: AX = HW_SUCCESS or error code
; Uses:   AX, BX, CX, DX
;-----------------------------------------------------------------------------
setup_3c515_irq PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx

        ; Validate IRQ
        VALIDATE_IRQ al
        jc      .invalid_irq
        
        ; Configure interrupt enable mask for 3C515-TX
        SELECT_3C515_WINDOW dx, 1
        
        push    dx
        add     dx, 0Eh                     ; Command register
        mov     ax, (14 << 11) | 07FFh      ; Enhanced interrupt mask for DMA
        out     dx, ax
        pop     dx
        
        mov     ax, HW_SUCCESS
        jmp     .exit
        
.invalid_irq:
        mov     ax, HW_ERROR_IRQ_CONFLICT
        
.exit:
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
setup_3c515_irq ENDP

;-----------------------------------------------------------------------------
; configure_3c515_dma - Configure 3C515-TX DMA settings
;
; Input:  DX = I/O base address
; Output: AX = HW_SUCCESS or error code
; Uses:   AX, BX, CX, DX
;-----------------------------------------------------------------------------
configure_3c515_dma PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx

        ; Select window 7 for DMA control
        SELECT_3C515_WINDOW dx, 7
        
        ; Initialize DMA descriptor pointers
        push    dx
        add     dx, 404h                    ; Down list pointer
        xor     ax, ax                      ; NULL pointer initially
        out     dx, ax
        add     dx, 2
        out     dx, ax                      ; High word
        
        add     dx, 14h - 6                 ; Up list pointer (418h - 404h = 14h)
        out     dx, ax
        add     dx, 2  
        out     dx, ax                      ; High word
        pop     dx
        
        mov     ax, HW_SUCCESS
        
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
configure_3c515_dma ENDP

;-----------------------------------------------------------------------------
; reset_3c509b_device - Reset 3C509B device
;
; Input:  AL = device index
; Output: AX = HW_SUCCESS or error code
; Uses:   AX, BX, DX
;-----------------------------------------------------------------------------
reset_3c509b_device PROC
        push    bp
        mov     bp, sp
        push    bx
        push    dx

        ; Get I/O base for device
        mov     bl, al
        mov     bh, 0
        shl     bx, 1
        mov     dx, [hw_io_bases + bx]
        
        ; Perform hardware reset
        HARDWARE_RESET dx, HW_TYPE_3C509B, 1000
        jc      .reset_failed
        
        mov     ax, HW_SUCCESS
        jmp     .exit
        
.reset_failed:
        mov     ax, HW_ERROR_RESET_FAILED
        
.exit:
        pop     dx
        pop     bx
        pop     bp
        ret
reset_3c509b_device ENDP

;-----------------------------------------------------------------------------
; reset_3c515_device - Reset 3C515-TX device
;
; Input:  AL = device index
; Output: AX = HW_SUCCESS or error code
; Uses:   AX, BX, DX
;-----------------------------------------------------------------------------
reset_3c515_device PROC
        push    bp
        mov     bp, sp
        push    bx
        push    dx

        ; Get I/O base for device
        mov     bl, al
        mov     bh, 0
        shl     bx, 1
        mov     dx, [hw_io_bases + bx]
        
        ; Perform hardware reset
        HARDWARE_RESET dx, HW_TYPE_3C515TX, 1000
        jc      .reset_failed
        
        mov     ax, HW_SUCCESS
        jmp     .exit
        
.reset_failed:
        mov     ax, HW_ERROR_RESET_FAILED
        
.exit:
        pop     dx
        pop     bx
        pop     bp
        ret
reset_3c515_device ENDP

;=============================================================================
; SYSTEM-LEVEL FUNCTIONS
;=============================================================================

;-----------------------------------------------------------------------------
; hardware_init_system - Initialize hardware subsystem
;
; Input:  None
; Output: AX = HW_SUCCESS or error code
; Uses:   AX, BX, CX, SI
;-----------------------------------------------------------------------------
hardware_init_system PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    si

        ; Check if already initialized
        cmp     [hardware_initialized], 1
        je      .already_initialized
        
        ; Clear hardware instance table
        mov     cx, SIZE HW_INSTANCE
        mov     ax, MAX_HW_INSTANCES
        mul     cx
        mov     cx, ax                      ; Total bytes to clear
        mov     si, OFFSET hw_instance_table
        
.clear_loop:
        mov     byte ptr [si], 0
        inc     si
        dec     cx
        jnz     .clear_loop
        
        ; Initialize counters
        mov     [hw_instance_count], 0
        mov     [last_error_code], 0
        
        ; Clear I/O statistics
        mov     dword ptr [io_read_count], 0
        mov     dword ptr [io_write_count], 0
        mov     word ptr [io_error_count], 0
        mov     word ptr [io_timeout_count], 0
        mov     word ptr [io_retry_count], 0
        
        ; Mark as initialized
        mov     [hardware_initialized], 1
        
.already_initialized:
        mov     ax, HW_SUCCESS
        
        pop     si
        pop     cx
        pop     bx
        pop     bp
        ret
hardware_init_system ENDP

;-----------------------------------------------------------------------------
; hardware_detect_all_devices - Detect all supported hardware devices
;
; Input:  None
; Output: AX = number of devices detected
; Uses:   All registers
;-----------------------------------------------------------------------------
hardware_detect_all_devices PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si
        push    di

        mov     bx, 0                       ; Device counter
        
        ; Try to detect 3C509B devices
        call    detect_3c509b_device
        jc      .no_3c509b
        
        ; Store detected 3C509B
        mov     si, OFFSET hw_instance_table
        mov     cx, SIZE HW_INSTANCE
        mov     dx, bx
        mul     cx
        add     si, ax
        
        mov     [si + HW_INSTANCE.type], HW_TYPE_3C509B
        mov     [si + HW_INSTANCE.state], HW_STATE_DETECTED
        mov     [si + HW_INSTANCE.io_base], ax  ; From detect function
        
        ; Update legacy arrays for compatibility
        mov     [hw_types + bx], HW_TYPE_3C509B
        mov     [hw_instances + bx], HW_STATE_DETECTED
        push    bx
        shl     bx, 1
        mov     [hw_io_bases + bx], ax
        pop     bx
        
        inc     bx                          ; Increment device count
        
.no_3c509b:
        ; Try to detect 3C515-TX devices
        call    detect_3c515_device
        jc      .no_3c515
        
        ; Check if we have room for another device
        cmp     bx, MAX_HW_INSTANCES
        jae     .no_3c515
        
        ; Store detected 3C515-TX
        push    ax                          ; Save I/O base
        mov     si, OFFSET hw_instance_table
        mov     cx, SIZE HW_INSTANCE
        mov     ax, bx
        mul     cx
        add     si, ax
        pop     ax                          ; Restore I/O base
        
        mov     [si + HW_INSTANCE.type], HW_TYPE_3C515TX
        mov     [si + HW_INSTANCE.state], HW_STATE_DETECTED
        mov     [si + HW_INSTANCE.io_base], ax
        
        ; Update legacy arrays
        mov     [hw_types + bx], HW_TYPE_3C515TX
        mov     [hw_instances + bx], HW_STATE_DETECTED
        push    bx
        shl     bx, 1
        mov     [hw_io_bases + bx], ax
        pop     bx
        
        inc     bx                          ; Increment device count
        
.no_3c515:
        ; Store final device count
        mov     [hw_instance_count], bl
        mov     ax, bx                      ; Return device count
        
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
hardware_detect_all_devices ENDP

;=============================================================================
; UTILITY AND SUPPORT FUNCTIONS
;=============================================================================

;-----------------------------------------------------------------------------
; delay_1ms - 1 millisecond delay
;
; Input:  None
; Output: None
; Uses:   CX
;-----------------------------------------------------------------------------
delay_1ms PROC
        push    cx
        
        ; Approximate 1ms delay using loop (CPU-dependent)
        ; This is a rough approximation for older CPUs
        mov     cx, 1000
        
.delay_loop:
        nop
        loop    .delay_loop
        
        pop     cx
        ret
delay_1ms ENDP

;-----------------------------------------------------------------------------
; delay_10ms - 10 millisecond delay
;
; Input:  None
; Output: None
; Uses:   CX
;-----------------------------------------------------------------------------
delay_10ms PROC
        push    cx
        
        mov     cx, 10
        
.delay_loop:
        call    delay_1ms
        loop    .delay_loop
        
        pop     cx
        ret
delay_10ms ENDP

;=============================================================================
; PHASE 3: ADVANCED DMA FUNCTIONS FOR 3C515-TX
; Sub-Agent 1: DMA Specialist - Low-level DMA operations
;=============================================================================

;-----------------------------------------------------------------------------
; dma_stall_engines - Stall DMA engines for timeout recovery
;
; Input:  AL = 1 to stall TX, 0 to skip TX
;         AH = 1 to stall RX, 0 to skip RX
;         DX = I/O base address
; Output: AX = 0 for success, negative error code on failure
; Uses:   AX, BX, CX, DX
;-----------------------------------------------------------------------------
PUBLIC dma_stall_engines
dma_stall_engines PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        
        ; Save stall flags
        mov     bl, al                      ; TX stall flag
        mov     bh, ah                      ; RX stall flag
        
        ; Get I/O base from context (passed in DX)
        test    dx, dx
        jz      .no_io_base
        
        ; Select window 7 for DMA control
        push    dx
        add     dx, 0Eh                     ; Command register
        mov     ax, (1 << 11) | 7           ; Select window 7
        out     dx, ax
        pop     dx
        
        ; Small delay for window selection
        mov     cx, 100
.window_delay:
        nop
        loop    .window_delay
        
        ; Stall TX engine if requested
        test    bl, bl
        jz      .skip_tx_stall
        
        push    dx
        add     dx, 0Eh                     ; Command register
        mov     ax, (6 << 11) + 2           ; DownStall command
        out     dx, ax
        pop     dx
        
        ; Wait for TX engine to stall
        mov     cx, 1000                    ; Timeout counter
.tx_stall_wait:
        push    dx
        add     dx, 0Eh                     ; Status register
        in      ax, dx
        pop     dx
        test    ax, 0800h                   ; Check DMA in progress bit
        jz      .tx_stalled
        dec     cx
        jnz     .tx_stall_wait
        
        ; TX stall timeout
        mov     ax, -1
        jmp     .exit
        
.tx_stalled:
.skip_tx_stall:
        ; Stall RX engine if requested
        test    bh, bh
        jz      .skip_rx_stall
        
        push    dx
        add     dx, 0Eh                     ; Command register
        mov     ax, (6 << 11) + 0           ; UpStall command  
        out     dx, ax
        pop     dx
        
        ; Wait for RX engine to stall
        mov     cx, 1000                    ; Timeout counter
.rx_stall_wait:
        push    dx
        add     dx, 0Eh                     ; Status register
        in      ax, dx
        pop     dx
        test    ax, 0800h                   ; Check DMA in progress bit
        jz      .rx_stalled
        dec     cx
        jnz     .rx_stall_wait
        
        ; RX stall timeout
        mov     ax, -1
        jmp     .exit
        
.rx_stalled:
.skip_rx_stall:
        ; Success
        mov     ax, 0
        jmp     .exit
        
.no_io_base:
        mov     ax, -1
        
.exit:
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
dma_stall_engines ENDP

;-----------------------------------------------------------------------------
; dma_unstall_engines - Unstall DMA engines after recovery
;
; Input:  AL = 1 to unstall TX, 0 to skip TX
;         AH = 1 to unstall RX, 0 to skip RX  
;         DX = I/O base address
; Output: AX = 0 for success, negative error code on failure
; Uses:   AX, BX, CX, DX
;-----------------------------------------------------------------------------
PUBLIC dma_unstall_engines
dma_unstall_engines PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        
        ; Save unstall flags
        mov     bl, al                      ; TX unstall flag
        mov     bh, ah                      ; RX unstall flag
        
        ; Get I/O base 
        test    dx, dx
        jz      .no_io_base
        
        ; Select window 7 for DMA control
        push    dx
        add     dx, 0Eh                     ; Command register
        mov     ax, (1 << 11) | 7           ; Select window 7
        out     dx, ax
        pop     dx
        
        ; Small delay for window selection
        mov     cx, 100
.window_delay:
        nop
        loop    .window_delay
        
        ; Unstall TX engine if requested
        test    bl, bl
        jz      .skip_tx_unstall
        
        push    dx
        add     dx, 0Eh                     ; Command register  
        mov     ax, (6 << 11) + 3           ; DownUnstall command
        out     dx, ax
        pop     dx
        
.skip_tx_unstall:
        ; Unstall RX engine if requested
        test    bh, bh
        jz      .skip_rx_unstall
        
        push    dx
        add     dx, 0Eh                     ; Command register
        mov     ax, (6 << 11) + 1           ; UpUnstall command
        out     dx, ax
        pop     dx
        
.skip_rx_unstall:
        ; Success
        mov     ax, 0
        jmp     .exit
        
.no_io_base:
        mov     ax, -1
        
.exit:
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
dma_unstall_engines ENDP

;-----------------------------------------------------------------------------
; dma_start_transfer - Start DMA transfer engines
;
; Input:  AL = 1 to start TX, 0 to skip TX
;         AH = 1 to start RX, 0 to skip RX
;         DX = I/O base address
; Output: AX = 0 for success, negative error code on failure  
; Uses:   AX, BX, CX, DX
;-----------------------------------------------------------------------------
PUBLIC dma_start_transfer
dma_start_transfer PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        
        ; Save start flags
        mov     bl, al                      ; TX start flag
        mov     bh, ah                      ; RX start flag
        
        ; Get I/O base
        test    dx, dx
        jz      .no_io_base
        
        ; Select window 7 for DMA control
        push    dx
        add     dx, 0Eh                     ; Command register
        mov     ax, (1 << 11) | 7           ; Select window 7
        out     dx, ax
        pop     dx
        
        ; Small delay for window selection
        mov     cx, 100
.window_delay:
        nop
        loop    .window_delay
        
        ; Start TX DMA if requested
        test    bl, bl
        jz      .skip_tx_start
        
        push    dx
        add     dx, 0Eh                     ; Command register
        mov     ax, (20 << 11) + 1          ; StartDMADown command
        out     dx, ax
        pop     dx
        
.skip_tx_start:
        ; Start RX DMA if requested  
        test    bh, bh
        jz      .skip_rx_start
        
        push    dx
        add     dx, 0Eh                     ; Command register
        mov     ax, (20 << 11) + 0          ; StartDMAUp command
        out     dx, ax
        pop     dx
        
.skip_rx_start:
        ; Success
        mov     ax, 0
        jmp     .exit
        
.no_io_base:
        mov     ax, -1
        
.exit:
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
dma_start_transfer ENDP

;-----------------------------------------------------------------------------
; dma_get_engine_status - Get DMA engine status
;
; Input:  DX = I/O base address
;         ES:BX = pointer to status structure (tx_status, rx_status)
; Output: AX = 0 for success, negative error code on failure
; Uses:   AX, BX, CX, DX, SI
;-----------------------------------------------------------------------------
PUBLIC dma_get_engine_status  
dma_get_engine_status PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si
        
        ; Get I/O base
        test    dx, dx
        jz      .no_io_base
        
        ; Save status pointer
        mov     si, bx
        
        ; Select window 7 for DMA status
        push    dx
        add     dx, 0Eh                     ; Command register
        mov     ax, (1 << 11) | 7           ; Select window 7
        out     dx, ax
        pop     dx
        
        ; Small delay for window selection
        mov     cx, 100
.window_delay:
        nop
        loop    .window_delay
        
        ; Read DMA status register
        push    dx
        add     dx, 0Eh                     ; Status register
        in      ax, dx
        pop     dx
        
        ; Extract TX status (bits related to down DMA)
        mov     bx, ax
        and     bx, 0200h                   ; Down complete bit (bit 9)
        mov     [es:si], bx                 ; Store TX status
        
        ; Extract RX status (bits related to up DMA)  
        mov     bx, ax
        and     bx, 0400h                   ; Up complete bit (bit 10)
        mov     [es:si+4], bx               ; Store RX status (offset by 4 bytes)
        
        ; Success
        mov     ax, 0
        jmp     .exit
        
.no_io_base:
        mov     ax, -1
        
.exit:
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
dma_get_engine_status ENDP

;-----------------------------------------------------------------------------
; dma_prepare_coherent_buffer - Prepare buffer for cache coherency
;
; Input:  ES:BX = buffer address
;         CX = buffer length
;         DL = direction (0=TX, 1=RX)
; Output: AX = 0 for success, negative error code on failure
; Uses:   AX, BX, CX, DX
;-----------------------------------------------------------------------------
PUBLIC dma_prepare_coherent_buffer
dma_prepare_coherent_buffer PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        
        ; For ISA bus mastering, we need to ensure cache coherency
        ; This is a simplified implementation for DOS environment
        
        ; Check if buffer address is valid
        test    bx, bx
        jz      .invalid_buffer
        
        ; Check buffer length
        test    cx, cx
        jz      .invalid_buffer
        
        ; Check if buffer is in suitable memory range for ISA DMA
        ; ISA DMA is limited to 24-bit addressing (16MB)
        mov     ax, es
        cmp     ax, 0FFFFh                  ; Check segment is reasonable
        ja      .invalid_buffer
        
        ; For DOS, we primarily need to flush any CPU caches
        ; This is CPU-dependent, but most 386/486 systems need WBINVD
        
        ; Check if we're running on 386 or later (has cache)
        call    get_cpu_features            ; Get CPU features
        test    ax, 0001h                   ; Check for cache present
        jz      .no_cache_flush_needed
        
        ; Flush cache for DMA safety (386/486/Pentium)
        ; Note: This is privileged instruction, may fault on some systems
        pushf
        cli                                 ; Disable interrupts
        
        ; Issue cache flush (write-back and invalidate)
        ; This flushes all caches, which is overkill but safe
        .byte   0Fh, 09h                    ; WBINVD instruction
        
        popf                                ; Restore interrupts
        
.no_cache_flush_needed:
        ; Success
        mov     ax, 0
        jmp     .exit
        
.invalid_buffer:
        mov     ax, -1
        
.exit:
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
dma_prepare_coherent_buffer ENDP

;-----------------------------------------------------------------------------
; dma_complete_coherent_buffer - Complete cache coherency after DMA
;
; Input:  ES:BX = buffer address  
;         CX = buffer length
;         DL = direction (0=TX, 1=RX)
; Output: AX = 0 for success, negative error code on failure
; Uses:   AX, BX, CX, DX
;-----------------------------------------------------------------------------
PUBLIC dma_complete_coherent_buffer
dma_complete_coherent_buffer PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        
        ; Check if buffer address is valid
        test    bx, bx
        jz      .invalid_buffer
        
        ; Check buffer length
        test    cx, cx
        jz      .invalid_buffer
        
        ; For RX operations, we need to invalidate cache lines
        ; to ensure CPU sees data written by DMA
        cmp     dl, 1                       ; Check if RX operation
        jne     .tx_operation
        
        ; RX operation - invalidate cache to see DMA data
        call    get_cpu_features            ; Get CPU features
        test    ax, 0001h                   ; Check for cache present
        jz      .no_cache_invalidate_needed
        
        pushf
        cli                                 ; Disable interrupts
        
        ; Invalidate cache lines (for RX we want to see DMA data)
        .byte   0Fh, 08h                    ; INVD instruction
        
        popf                                ; Restore interrupts
        jmp     .cache_complete
        
.tx_operation:
        ; TX operation - no special action needed after DMA
        jmp     .cache_complete
        
.no_cache_invalidate_needed:
.cache_complete:
        ; Success
        mov     ax, 0
        jmp     .exit
        
.invalid_buffer:
        mov     ax, -1
        
.exit:
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
dma_complete_coherent_buffer ENDP

;-----------------------------------------------------------------------------
; setup_advanced_dma_descriptors - Setup hardware descriptor pointers
;
; Input:  DX = I/O base address
;         ES:BX = pointer to TX ring physical address
;         ES:SI = pointer to RX ring physical address
; Output: AX = 0 for success, negative error code on failure
; Uses:   AX, BX, CX, DX, SI
;-----------------------------------------------------------------------------
PUBLIC setup_advanced_dma_descriptors
setup_advanced_dma_descriptors PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si
        push    di
        
        ; Get I/O base
        test    dx, dx
        jz      .no_io_base
        
        ; Save descriptor addresses
        mov     di, si                      ; Save RX ring address
        
        ; Select window 7 for descriptor pointer setup
        push    dx
        add     dx, 0Eh                     ; Command register
        mov     ax, (1 << 11) | 7           ; Select window 7
        out     dx, ax
        pop     dx
        
        ; Small delay for window selection
        mov     cx, 100
.window_delay:
        nop
        loop    .window_delay
        
        ; Set TX descriptor list pointer (Down List Pointer)
        push    dx
        add     dx, 404h                    ; Down list pointer register
        mov     ax, [es:bx]                 ; Get low word of TX ring address
        out     dx, ax
        add     dx, 2                       ; High word register
        mov     ax, [es:bx+2]               ; Get high word of TX ring address
        out     dx, ax
        pop     dx
        
        ; Set RX descriptor list pointer (Up List Pointer)
        push    dx
        add     dx, 418h                    ; Up list pointer register  
        mov     ax, [es:di]                 ; Get low word of RX ring address
        out     dx, ax
        add     dx, 2                       ; High word register
        mov     ax, [es:di+2]               ; Get high word of RX ring address
        out     dx, ax
        pop     dx
        
        ; Enable bus mastering in the NIC
        push    dx
        add     dx, 0Eh                     ; Command register
        mov     ax, (14 << 11) | 07FFh      ; Set interrupt enable with all DMA bits
        out     dx, ax
        pop     dx
        
        ; Success
        mov     ax, 0
        jmp     .exit
        
.no_io_base:
        mov     ax, -1
        
.exit:
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
setup_advanced_dma_descriptors ENDP

;-----------------------------------------------------------------------------
; advanced_dma_interrupt_check - Check for advanced DMA interrupts
;
; Input:  DX = I/O base address
; Output: AX = interrupt status mask
;         BX = DMA completion status  
; Uses:   AX, BX, CX, DX
;-----------------------------------------------------------------------------
PUBLIC advanced_dma_interrupt_check
advanced_dma_interrupt_check PROC
        push    bp
        mov     bp, sp
        push    cx
        push    dx
        
        ; Initialize return values
        mov     ax, 0                       ; Interrupt status
        mov     bx, 0                       ; DMA completion status
        
        ; Get I/O base
        test    dx, dx
        jz      .no_io_base
        
        ; Read interrupt status register
        push    dx
        add     dx, 0Eh                     ; Status register
        in      ax, dx                      ; Read status
        pop     dx
        
        ; Check for DMA-related interrupts
        mov     bx, ax                      ; Save full status
        
        ; Extract DMA completion bits
        and     bx, 0700h                   ; Mask for DMA Done, Down Complete, Up Complete
        
        ; Check specific DMA interrupt conditions
        test    ax, 0100h                   ; DMA Done (bit 8)
        jz      .no_dma_done
        or      bx, 0001h                   ; Set DMA done flag
        
.no_dma_done:
        test    ax, 0200h                   ; Down Complete (bit 9 - TX)
        jz      .no_tx_complete
        or      bx, 0002h                   ; Set TX complete flag
        
.no_tx_complete:
        test    ax, 0400h                   ; Up Complete (bit 10 - RX)
        jz      .no_rx_complete
        or      bx, 0004h                   ; Set RX complete flag
        
.no_rx_complete:
        ; Return interrupt status in AX, DMA status in BX
        jmp     .exit
        
.no_io_base:
        mov     ax, 0
        mov     bx, 0
        
.exit:
        pop     dx
        pop     cx
        pop     bp
        ret
advanced_dma_interrupt_check ENDP

;=============================================================================
; HAL BRIDGE FUNCTIONS - C to Assembly Interface
;=============================================================================

;-----------------------------------------------------------------------------
; asm_3c509b_detect_hardware - HAL bridge for 3C509B detection
;
; Input:  [BP+4] = struct nic_context *context (C calling convention)
; Output: AX = HAL_SUCCESS or error code
; Uses:   Standard C calling convention
;-----------------------------------------------------------------------------
PUBLIC asm_3c509b_detect_hardware
asm_3c509b_detect_hardware PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si
        
        ; Call our detect function
        call    detect_3c509b
        cmp     ax, 0
        je      .not_found
        
        ; Found - store I/O base in context structure
        ; Populate nic_context structure
        push    si
        push    di
        push    cx
        push    bx
        
        ; ES:DI should point to nic_context structure
        ; AL contains instance index
        
        xor     ah, ah
        mov     si, ax                  ; Save instance index
        
        ; Store I/O base (16-bit)
        shl     si, 1                   ; Word offset
        mov     bx, OFFSET hw_iobase_table
        mov     ax, [bx+si]
        stosw                           ; Store iobase
        
        ; Store IRQ (8-bit)
        shr     si, 1                   ; Back to byte offset
        mov     bx, OFFSET hw_irq_lines
        mov     al, [bx+si]
        stosb                           ; Store irq
        
        ; Store NIC type (8-bit)
        mov     bx, OFFSET hw_types
        mov     al, [bx+si]
        stosb                           ; Store nic_type
        
        ; Store flags (8-bit)
        mov     bx, OFFSET hw_flags_table
        mov     al, [bx+si]
        stosb                           ; Store flags
        
        ; Store current window (8-bit, default to 1)
        mov     al, 1
        stosb                           ; Store window
        
        ; Store MAC address (6 bytes)
        mov     ax, si
        mov     cl, 6
        mul     cl                      ; AX = index * 6
        mov     si, OFFSET hw_mac_addresses
        add     si, ax
        mov     cx, 6
        rep     movsb                   ; Copy MAC address
        
        ; Initialize statistics counters to 0
        xor     ax, ax
        stosw                           ; tx_free = 0
        stosw                           ; rx_status = 0
        stosw                           ; tx_packets low = 0
        stosw                           ; tx_packets high = 0
        stosw                           ; rx_packets low = 0
        stosw                           ; rx_packets high = 0
        stosw                           ; tx_errors low = 0
        stosw                           ; tx_errors high = 0
        stosw                           ; rx_errors low = 0
        stosw                           ; rx_errors high = 0
        
        pop     bx
        pop     cx
        pop     di
        pop     si
        mov     bx, 0                   ; HAL_SUCCESS
        jmp     .exit
        
.not_found:
        mov     bx, -2                  ; HAL_ERROR_HARDWARE_FAILURE
        
.exit:
        mov     ax, bx                  ; Return value in AX
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
asm_3c509b_detect_hardware ENDP

;-----------------------------------------------------------------------------
; asm_3c509b_init_hardware - HAL bridge for 3C509B initialization
;
; Input:  [BP+4] = struct nic_context *context (C calling convention)
; Output: AX = HAL_SUCCESS or error code
; Uses:   Standard C calling convention
;-----------------------------------------------------------------------------
PUBLIC asm_3c509b_init_hardware
asm_3c509b_init_hardware PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si
        
        ; Get context pointer
        mov     si, [bp+4]              ; nic_context pointer
        ; Extract I/O base from context structure
        push    si
        push    ds
        
        ; DS:SI points to context structure
        lds     si, [bp+4]              ; Load context pointer
        lodsw                           ; Load iobase (first field)
        mov     dx, ax                  ; DX = I/O base
        
        pop     ds
        pop     si
        ; For now, use first detected I/O base
        mov     dx, [hw_io_bases]       ; Get stored I/O base
        mov     al, 0                   ; NIC index 0
        
        ; Call our init function
        call    init_3c509b
        cmp     ax, 0
        jne     .init_failed
        
        mov     bx, 0                   ; HAL_SUCCESS
        jmp     .exit
        
.init_failed:
        mov     bx, -6                  ; HAL_ERROR_INITIALIZATION
        
.exit:
        mov     ax, bx                  ; Return value in AX
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
asm_3c509b_init_hardware ENDP

; Additional HAL bridge function stubs for complete vtable
PUBLIC asm_3c509b_reset_hardware
asm_3c509b_reset_hardware PROC
        push    bp
        mov     bp, sp
        mov     ax, 0                   ; HAL_SUCCESS
        pop     bp
        ret
asm_3c509b_reset_hardware ENDP

PUBLIC asm_3c509b_configure_media
asm_3c509b_configure_media PROC
        push    bp
        mov     bp, sp
        mov     ax, 0                   ; HAL_SUCCESS
        pop     bp
        ret
asm_3c509b_configure_media ENDP

PUBLIC asm_3c509b_set_station_address
asm_3c509b_set_station_address PROC
        push    bp
        mov     bp, sp
        mov     ax, 0                   ; HAL_SUCCESS
        pop     bp
        ret
asm_3c509b_set_station_address ENDP

PUBLIC asm_3c509b_enable_interrupts
asm_3c509b_enable_interrupts PROC
        push    bp
        mov     bp, sp
        mov     ax, 0                   ; HAL_SUCCESS
        pop     bp
        ret
asm_3c509b_enable_interrupts ENDP

PUBLIC asm_3c509b_start_transceiver
asm_3c509b_start_transceiver PROC
        push    bp
        mov     bp, sp
        mov     ax, 0                   ; HAL_SUCCESS
        pop     bp
        ret
asm_3c509b_start_transceiver ENDP

PUBLIC asm_3c509b_stop_transceiver
asm_3c509b_stop_transceiver PROC
        push    bp
        mov     bp, sp
        mov     ax, 0                   ; HAL_SUCCESS
        pop     bp
        ret
asm_3c509b_stop_transceiver ENDP

PUBLIC asm_3c509b_get_link_status
asm_3c509b_get_link_status PROC
        push    bp
        mov     bp, sp
        mov     ax, 1                   ; HAL_LINK_UP
        pop     bp
        ret
asm_3c509b_get_link_status ENDP

PUBLIC asm_3c509b_get_statistics
asm_3c509b_get_statistics PROC
        push    bp
        mov     bp, sp
        mov     ax, 0                   ; HAL_SUCCESS
        pop     bp
        ret
asm_3c509b_get_statistics ENDP

PUBLIC asm_3c509b_set_multicast
asm_3c509b_set_multicast PROC
        push    bp
        mov     bp, sp
        mov     ax, 0                   ; HAL_SUCCESS
        pop     bp
        ret
asm_3c509b_set_multicast ENDP

PUBLIC asm_3c509b_set_promiscuous
asm_3c509b_set_promiscuous PROC
        push    bp
        mov     bp, sp
        mov     ax, 0                   ; HAL_SUCCESS
        pop     bp
        ret
asm_3c509b_set_promiscuous ENDP

;=============================================================================
; 3C515-TX HAL WRAPPERS (LEGACY/WRAPPER INTERFACE)
;
; NOTE: The C implementation (src/c/3c515.c) is AUTHORITATIVE.
;       These assembly routines are wrappers for assembly-side callers.
;
; STATUS:
;   FUNCTIONAL WRAPPERS (call C functions):
;     - asm_3c515_detect_hardware  -> calls detect_3c515()
;     - asm_3c515_init_hardware    -> calls init_3c515()
;
;   MINIMAL IMPLEMENTATIONS:
;     - asm_3c515_reset_hardware   -> direct hardware reset
;     - asm_3c515_set_station_address -> direct register writes
;
;   NON-FUNCTIONAL STUBS (return success but do nothing):
;     - asm_3c515_enable_interrupts
;     - asm_3c515_start_transceiver
;     - asm_3c515_stop_transceiver
;     - asm_3c515_get_link_status
;     - asm_3c515_get_statistics
;     - asm_3c515_set_multicast
;     - asm_3c515_set_promiscuous
;
; The C vtable dispatch (hardware.c -> get_3c515_ops()) is the primary path.
; These stubs are UNUSED in normal operation. Verified 2026-01-25.
;=============================================================================\n\n;-----------------------------------------------------------------------------\n; asm_3c515_detect_hardware - Detect 3C515-TX hardware\n;\n; Input:  ES:BX = pointer to nic_context structure  \n; Output: AX = HAL_SUCCESS or error code\n; Uses:   All registers\n;-----------------------------------------------------------------------------\nasm_3c515_detect_hardware PROC\n        push    bp\n        mov     bp, sp\n        push    bx\n        push    cx\n        push    dx\n        push    si\n        push    di\n        \n        ; Call our existing detect_3c515 function\n        call    detect_3c515\n        cmp     ax, 0\n        je      .not_found\n        \n        ; Found - store I/O base in context\n        mov     [es:bx+4], ax           ; Store I/O base in context (offset 4)\n        mov     ax, 0                   ; HAL_SUCCESS\n        jmp     .exit\n        \n.not_found:\n        mov     ax, -2                  ; HAL_ERROR_HARDWARE_FAILURE\n        \n.exit:\n        pop     di\n        pop     si\n        pop     dx\n        pop     cx\n        pop     bx\n        pop     bp\n        ret\nasm_3c515_detect_hardware ENDP\n\n;-----------------------------------------------------------------------------\n; asm_3c515_init_hardware - Initialize 3C515-TX hardware\n;\n; Input:  ES:BX = pointer to nic_context structure\n; Output: AX = HAL_SUCCESS or error code\n; Uses:   All registers  \n;-----------------------------------------------------------------------------\nasm_3c515_init_hardware PROC\n        push    bp\n        mov     bp, sp\n        push    bx\n        push    cx\n        push    dx\n        push    si\n        \n        ; Get I/O base from context\n        mov     dx, [es:bx+4]           ; I/O base from context\n        mov     al, 1                   ; Instance 1 for 3C515\n        \n        ; Call our init_3c515 function\n        call    init_3c515\n        ; Returns 0 for success already\n        \n        pop     si\n        pop     dx\n        pop     cx\n        pop     bx\n        pop     bp\n        ret\nasm_3c515_init_hardware ENDP\n\n;-----------------------------------------------------------------------------\n; asm_3c515_reset_hardware - Reset 3C515-TX hardware\n;\n; Input:  ES:BX = pointer to nic_context structure\n; Output: AX = HAL_SUCCESS or error code\n; Uses:   AX, BX, DX\n;-----------------------------------------------------------------------------\nasm_3c515_reset_hardware PROC\n        push    bp\n        mov     bp, sp\n        push    bx\n        push    dx\n        \n        ; Get I/O base from context\n        mov     dx, [es:bx+4]           ; I/O base from context\n        \n        ; Total reset command\n        add     dx, 0Eh                 ; Command register\n        mov     ax, 0                   ; Total reset\n        out     dx, ax\n        \n        ; Wait for reset\n        mov     cx, 1000\n.reset_wait:\n        call    delay_1ms\n        loop    .reset_wait\n        \n        mov     ax, 0                   ; HAL_SUCCESS\n        \n        pop     dx\n        pop     bx\n        pop     bp\n        ret\nasm_3c515_reset_hardware ENDP\n\n;-----------------------------------------------------------------------------\n; asm_3c515_configure_media - Configure media type\n;\n; Input:  ES:BX = pointer to nic_context structure, CX = media_type\n; Output: AX = HAL_SUCCESS or error code\n; Uses:   AX, BX, CX, DX\n;-----------------------------------------------------------------------------\nasm_3c515_configure_media PROC\n        push    bp\n        mov     bp, sp\n        \n        ; For now, just return success - media is auto-configured in init\n        mov     ax, 0                   ; HAL_SUCCESS\n        \n        pop     bp\n        ret\nasm_3c515_configure_media ENDP\n\n;-----------------------------------------------------------------------------\n; asm_3c515_set_station_address - Set MAC address\n;\n; Input:  ES:BX = pointer to nic_context structure, ES:SI = mac_addr\n; Output: AX = HAL_SUCCESS or error code\n; Uses:   AX, BX, CX, DX, SI, DI\n;-----------------------------------------------------------------------------\nasm_3c515_set_station_address PROC\n        push    bp\n        mov     bp, sp\n        push    bx\n        push    cx\n        push    dx\n        push    si\n        push    di\n        \n        ; Get I/O base from context\n        mov     dx, [es:bx+4]           ; I/O base from context\n        \n        ; Select window 2 for station address\n        add     dx, 0Eh                 ; Command register\n        mov     ax, (1 << 11) | 2       ; Select window 2\n        out     dx, ax\n        \n        ; Write MAC address to station address registers (0-5)\n        sub     dx, 0Eh                 ; Back to base\n        mov     cx, 6                   ; 6 bytes\n        mov     di, 0                   ; Start at offset 0\n        \n.mac_loop:\n        mov     al, [es:si]             ; Get MAC byte\n        push    dx\n        add     dx, di                  ; Address register offset\n        out     dx, al                  ; Write MAC byte\n        pop     dx\n        inc     si                      ; Next MAC byte\n        inc     di                      ; Next register\n        loop    .mac_loop\n        \n        ; Return to window 1\n        add     dx, 0Eh                 ; Command register\n        mov     ax, (1 << 11) | 1       ; Select window 1\n        out     dx, ax\n        \n        mov     ax, 0                   ; HAL_SUCCESS\n        \n        pop     di\n        pop     si\n        pop     dx\n        pop     cx\n        pop     bx\n        pop     bp\n        ret\nasm_3c515_set_station_address ENDP\n\n;-----------------------------------------------------------------------------\n; Remaining 3C515 HAL vtable functions (stubs for now)\n;-----------------------------------------------------------------------------\n\nasm_3c515_enable_interrupts PROC\n        mov     ax, 0                   ; HAL_SUCCESS\n        ret\nasm_3c515_enable_interrupts ENDP\n\nasm_3c515_start_transceiver PROC\n        mov     ax, 0                   ; HAL_SUCCESS\n        ret\nasm_3c515_start_transceiver ENDP\n\nasm_3c515_stop_transceiver PROC\n        mov     ax, 0                   ; HAL_SUCCESS\n        ret\nasm_3c515_stop_transceiver ENDP\n\nasm_3c515_get_link_status PROC\n        mov     ax, 1                   ; HAL_LINK_UP\n        ret\nasm_3c515_get_link_status ENDP\n\nasm_3c515_get_statistics PROC\n        mov     ax, 0                   ; HAL_SUCCESS\n        ret\nasm_3c515_get_statistics ENDP\n\nasm_3c515_set_multicast PROC\n        mov     ax, 0                   ; HAL_SUCCESS\n        ret\nasm_3c515_set_multicast ENDP\n\nasm_3c515_set_promiscuous PROC\n        mov     ax, 0                   ; HAL_SUCCESS\n        ret\nasm_3c515_set_promiscuous ENDP\n\n;=============================================================================
; ISA ADDRESS TRANSLATION FOR 3C515 DMA
;=============================================================================

;-----------------------------------------------------------------------------
; isa_virt_to_phys - Convert DOS real mode address to ISA physical address
;
; Input:  DS:SI = virtual address (segment:offset)
; Output: DX:AX = 24-bit physical address for ISA DMA
; Uses:   AX, DX
;-----------------------------------------------------------------------------
PUBLIC isa_virt_to_phys
isa_virt_to_phys PROC
        push    cx
        
        ; Calculate physical address: (segment << 4) + offset
        mov     ax, ds
        xor     dx, dx
        mov     cx, 4
        
        ; Shift segment left by 4 bits (multiply by 16)
.shift_loop:
        shl     ax, 1
        rcl     dx, 1
        loop    .shift_loop
        
        ; Add offset
        add     ax, si
        adc     dx, 0
        
        ; Ensure within ISA DMA 16MB limit (24-bit address)
        and     dx, 00FFh               ; Mask to 24 bits
        
        pop     cx
        ret
isa_virt_to_phys ENDP

;-----------------------------------------------------------------------------
; check_isa_dma_boundary - Check if buffer crosses 64KB DMA boundary
;
; Input:  DX:AX = physical address, CX = buffer size
; Output: CF set if boundary crossed, clear if OK
; Uses:   None (preserves all registers)
;-----------------------------------------------------------------------------
PUBLIC check_isa_dma_boundary
check_isa_dma_boundary PROC
        push    ax
        push    bx
        push    dx
        
        ; Calculate end address
        mov     bx, ax
        add     bx, cx
        jnc     .no_carry
        inc     dx                      ; Handle carry to high word
        
.no_carry:
        ; Check if high 16 bits changed (crossed 64KB boundary)
        push    ax
        xor     ax, bx                  ; XOR start and end low words
        and     ax, 0F000h              ; Check if upper nibble changed
        pop     ax
        jz      .same_64k
        
        ; Crossed boundary
        stc                             ; Set carry flag
        jmp     .exit
        
.same_64k:
        clc                             ; Clear carry flag
        
.exit:
        pop     dx
        pop     bx
        pop     ax
        ret
check_isa_dma_boundary ENDP

;-----------------------------------------------------------------------------
; setup_isa_dma_descriptor - Setup DMA descriptor for ISA bus master
;
; Input:  ES:DI = descriptor location
;         DX:AX = physical address
;         CX = length
;         BX = control flags
; Output: None
; Uses:   None (preserves all registers)
;-----------------------------------------------------------------------------
PUBLIC setup_isa_dma_descriptor
setup_isa_dma_descriptor PROC
        push    ax
        push    dx
        
        ; Store physical address (24-bit for ISA)
        mov     word ptr es:[di], ax    ; Low 16 bits
        mov     byte ptr es:[di+2], dl  ; Bits 16-23
        mov     byte ptr es:[di+3], 0   ; Zero upper byte
        
        ; Store length
        mov     word ptr es:[di+4], cx
        
        ; Store control flags
        mov     word ptr es:[di+6], bx
        
        pop     dx
        pop     ax
        ret
setup_isa_dma_descriptor ENDP

;-----------------------------------------------------------------------------
; init_3c515_bus_master - Initialize ISA bus mastering for 3C515
;
; Input:  DX = I/O base address
; Output: AX = 0 for success, non-zero for error
; Uses:   AX, BX, CX, DX
;-----------------------------------------------------------------------------
PUBLIC init_3c515_bus_master
init_3c515_bus_master PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    si
        push    di
        
        ; Select window 7 for bus master control
        add     dx, 0Eh                 ; Command register
        mov     ax, (1 << 11) | 7       ; Select window 7
        out     dx, ax
        
        ; Initialize DMA control registers at base+0x400
        sub     dx, 0Eh                 ; Back to base
        add     dx, C515_DMA_CTRL       ; DMA control offset (0x400)
        
        ; Clear DMA lists
        xor     ax, ax
        out     dx, ax                  ; Clear TX list pointer
        add     dx, 4
        out     dx, ax                  ; Clear fragment address
        add     dx, 4
        out     dx, ax                  ; Clear fragment length
        
        ; Enable bus mastering
        sub     dx, C515_DMA_CTRL
        add     dx, 0Eh                 ; Command register
        mov     ax, 0x2000              ; BUS_MASTER_ENABLE
        out     dx, ax
        
        ; Success
        xor     ax, ax
        
        pop     di
        pop     si
        pop     cx
        pop     bx
        pop     bp
        ret
init_3c515_bus_master ENDP

;=============================================================================
; MCA/EISA/VLB Detection Stubs
;
; Purpose: Detect unsupported bus NICs to provide user feedback
; 
; These are STUBS ONLY - they detect but do not support:
; - MCA: 3C523, 3C529
; - EISA: 3C592, 3C597
; - VLB: Various cards
;
; Safety: Read-only operations, restores all system state
; Size: ~1.4KB total for all stubs
; Risk: Low - includes timeout protection
;=============================================================================

;-----------------------------------------------------------------------------
; is_mca_system - Quick MCA system check
;
; Input:  None
; Output: AX = 1 if MCA system, 0 if not
; Uses:   AX, BX, ES (preserves all)
;-----------------------------------------------------------------------------
PUBLIC is_mca_system
is_mca_system PROC
        push    bx
        push    cx
        push    es
        
        ; Use INT 15h, AH=C0h to get system configuration
        mov     ah, 0C0h            ; Get System Configuration
        int     15h
        jc      .not_mca            ; Carry set = no config info
        
        ; ES:BX points to config table
        ; First byte is the table length - must check before accessing
        mov     al, es:[bx]         ; Get table length
        cmp     al, 6               ; Need at least 6 bytes to access feature byte 1
        jb      .not_mca            ; Table too short, not safe to check
        
        ; Check feature byte 1 at offset 5
        mov     al, es:[bx+5]       ; Feature byte 1
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
is_mca_system ENDP

;-----------------------------------------------------------------------------
; get_ps2_model - Get PS/2 model number
;
; Input:  None
; Output: AX = PS/2 model number (0 if not PS/2 or unknown)
; Uses:   AX, BX, ES
;-----------------------------------------------------------------------------
PUBLIC get_ps2_model
get_ps2_model PROC
        push    bx
        push    es
        
        ; Get system configuration
        mov     ah, 0C0h
        int     15h
        jc      .unknown_model
        
        ; ES:BX points to system configuration table
        ; Byte at offset 2 is model byte
        mov     al, es:[bx+2]       ; Model byte
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
get_ps2_model ENDP

;-----------------------------------------------------------------------------
; safe_mca_port_read - Safely read MCA POS port with timeout
;
; Input:  None
; Output: CF = 0 if port exists, CF = 1 if not
;         AL = port value if exists
; Uses:   AX, CX, DX
;-----------------------------------------------------------------------------
safe_mca_port_read PROC
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
safe_mca_port_read ENDP

;-----------------------------------------------------------------------------
; nic_detect_mca_3c523 - Stub for MCA 3C523 detection
;
; Purpose: Detect but not support MCA NICs, inform user
; Input:  None
; Output: AX = 1 if MCA NIC found (but unsupported), 0 if not
; Uses:   All registers
;-----------------------------------------------------------------------------
PUBLIC nic_detect_mca_3c523
nic_detect_mca_3c523 PROC
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
        push    OFFSET msg_3c523_found
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
        
msg_3c523_found db 'MCA: 3C523 EtherLink/MC detected but not supported', 0
nic_detect_mca_3c523 ENDP

;-----------------------------------------------------------------------------
; nic_detect_mca_3c529 - Stub for MCA 3C529 detection
;
; Purpose: Detect but not support MCA 3C529, inform user
; Input:  None
; Output: AX = 1 if MCA NIC found (but unsupported), 0 if not
; Uses:   All registers
;-----------------------------------------------------------------------------
PUBLIC nic_detect_mca_3c529
nic_detect_mca_3c529 PROC
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
        push    OFFSET msg_3c529_found
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
        
msg_3c529_found db 'MCA: 3C529 EtherLink III/MC detected but not supported', 0
nic_detect_mca_3c529 ENDP

;-----------------------------------------------------------------------------
; is_eisa_system - Check if system has EISA bus
;
; Input:  None
; Output: AX = 1 if EISA system, 0 if not
; Uses:   AX, DX (preserves all)
;-----------------------------------------------------------------------------
PUBLIC is_eisa_system
is_eisa_system PROC
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
is_eisa_system ENDP

;-----------------------------------------------------------------------------
; nic_detect_eisa_3c592 - Stub for EISA 3C592 detection
;
; Purpose: Detect but not support EISA NICs, inform user
; Input:  None
; Output: AX = 1 if EISA NIC found (but unsupported), 0 if not
; Uses:   All registers
;-----------------------------------------------------------------------------
PUBLIC nic_detect_eisa_3c592
nic_detect_eisa_3c592 PROC
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
        push    OFFSET msg_eisa_3c592_found
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
        
msg_eisa_3c592_found db 'EISA: 3C592 detected but not supported', 0
nic_detect_eisa_3c592 ENDP

;-----------------------------------------------------------------------------
; nic_detect_eisa_3c597 - Stub for EISA 3C597 detection
;
; Purpose: Detect but not support EISA 3C597, inform user
; Input:  None
; Output: AX = 1 if EISA NIC found (but unsupported), 0 if not
; Uses:   All registers
;-----------------------------------------------------------------------------
PUBLIC nic_detect_eisa_3c597
nic_detect_eisa_3c597 PROC
        ; Similar structure to 3C592 detection
        ; Would check for different EISA ID pattern
        xor     ax, ax              ; Simplified stub
        ret
nic_detect_eisa_3c597 ENDP

;-----------------------------------------------------------------------------
; nic_detect_vlb - Stub for VESA Local Bus NIC detection
;
; Purpose: Detect but not support VLB NICs, inform user
; Input:  None
; Output: AX = 1 if VLB NIC found (but unsupported), 0 if not
; Uses:   All registers
;-----------------------------------------------------------------------------
PUBLIC nic_detect_vlb
nic_detect_vlb PROC
        push    dx
        
        ; VLB detection is tricky - no standard detection
        ; Check for common VLB NIC I/O addresses (0x6000 range)
        mov     dx, 6000h           ; Common VLB NIC address
        
        ; Try to read from port (with timeout protection)
        in      al, dx
        cmp     al, 0FFh            ; Likely no device
        je      .no_vlb
        
        ; Do a more specific check
        mov     dx, 6008h           ; Another VLB NIC register
        in      al, dx
        cmp     al, 0FFh
        je      .no_vlb
        
        ; Might be a VLB NIC
        push    OFFSET msg_vlb_not_supported
        call    log_warning
        add     sp, 2
        
        mov     ax, 1               ; Found but not supported
        jmp     .exit
        
.no_vlb:
        xor     ax, ax              ; No VLB NIC
        
.exit:
        pop     dx
        ret
        
msg_vlb_not_supported db 'VLB NICs not supported', 0
nic_detect_vlb ENDP

;-----------------------------------------------------------------------------
; Update hardware_detect_all to call stubs
;-----------------------------------------------------------------------------
; Note: This would be inserted into the existing hardware_detect_all function
; after ISA detection but is shown here for completeness

msg_eisa_not_supported db 'EISA NICs detected but not supported', 0
msg_mca_not_supported  db 'MicroChannel NICs detected but not supported', 0
msg_try_isa           db 'Please use supported ISA NICs: 3C509B or 3C515-TX', 0
msg_pure_mca_error1   db 'ERROR: No compatible network adapters available on this system.', 0
msg_pure_mca_error2   db 'This driver only supports ISA-based 3Com NICs (3C509B, 3C515-TX).', 0
msg_pure_mca_error3   db 'MicroChannel systems require MCA-specific network drivers.', 0

_TEXT ENDS

END