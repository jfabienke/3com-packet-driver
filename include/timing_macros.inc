; timing_macros.inc - CPU-specific timing delay macros
; Provides accurate timing delays for different CPU types and bus speeds
; Essential for hardware programming and protocol timing requirements
;
; Generated for 3Com Packet Driver - Groups 6A & 6B implementation
; Supports: 80286+, 80386+, 80486+, Pentium+, various bus speeds

%ifndef TIMING_MACROS_INC
%define TIMING_MACROS_INC

; CPU type detection constants (from cpu_detect.h integration)
CPU_8086           equ 0
CPU_80286          equ 1  
CPU_80386          equ 2
CPU_80486          equ 3
CPU_PENTIUM        equ 4
CPU_PENTIUM_PRO    equ 5

; Bus speed detection constants
BUS_SPEED_8MHZ     equ 8
BUS_SPEED_10MHZ    equ 10
BUS_SPEED_12MHZ    equ 12
BUS_SPEED_16MHZ    equ 16
BUS_SPEED_20MHZ    equ 20
BUS_SPEED_25MHZ    equ 25
BUS_SPEED_33MHZ    equ 33
BUS_SPEED_40MHZ    equ 40
BUS_SPEED_50MHZ    equ 50
BUS_SPEED_66MHZ    equ 66

; Standard timing requirements (microseconds)
EEPROM_ACCESS_TIME     equ 10      ; EEPROM read/write delay
NIC_RESET_TIME         equ 1000    ; NIC reset recovery time  
PIO_SETUP_TIME         equ 1       ; PIO setup time
DMA_SETUP_TIME         equ 5       ; DMA setup time
IRQ_SETTLE_TIME        equ 2       ; IRQ line settling time
BUS_SETTLE_TIME        equ 1       ; Bus transaction settling

; Timing calibration table - cycles per microsecond by CPU type and speed
; Format: [CPU_TYPE][BUS_SPEED_INDEX] = cycles per microsecond
align 2
cpu_cycles_per_us_table:
    ; 80286 timing (approximate)
    db 8, 10, 12, 16, 20, 25, 33, 40, 50, 66  ; 8MHz to 66MHz
    ; 80386 timing (more accurate)  
    db 8, 10, 12, 16, 20, 25, 33, 40, 50, 66  ; 8MHz to 66MHz
    ; 80486 timing (pipeline effects)
    db 6, 8, 9, 12, 15, 19, 25, 30, 38, 50    ; Adjusted for pipeline
    ; Pentium timing (superscalar)
    db 4, 5, 6, 8, 10, 12, 16, 20, 25, 33     ; Adjusted for superscalar
    ; Pentium Pro timing (out-of-order)
    db 3, 4, 5, 6, 8, 10, 13, 16, 20, 26      ; Adjusted for OOO execution

; Port I/O timing constants
; Different systems require different I/O delays
IO_DELAY_NONE      equ 0    ; No delay needed (modern systems)
IO_DELAY_SHORT     equ 1    ; Short delay (fast systems)
IO_DELAY_MEDIUM    equ 2    ; Medium delay (typical systems)
IO_DELAY_LONG      equ 3    ; Long delay (slow/old systems)

; I/O delay calibration by CPU speed
align 2
io_delay_type_table:
    db IO_DELAY_LONG,   IO_DELAY_LONG,   IO_DELAY_MEDIUM  ; 8, 10, 12 MHz
    db IO_DELAY_MEDIUM, IO_DELAY_SHORT,  IO_DELAY_SHORT   ; 16, 20, 25 MHz
    db IO_DELAY_SHORT,  IO_DELAY_NONE,   IO_DELAY_NONE    ; 33, 40, 50 MHz
    db IO_DELAY_NONE                                       ; 66+ MHz

; Macro: Basic microsecond delay
%macro DELAY_US 1
    ; Delay for specified microseconds
    ; Input: %1 = microseconds to delay
    ; Uses: AX, BX (preserved by pushing/popping)
    push    ax
    push    bx
    
    ; Get CPU type and speed
    call    get_cpu_type        ; Returns type in AL
    call    get_cpu_speed       ; Returns speed in BX
    
    ; Calculate delay loops needed
    mov     ah, al              ; Save CPU type
    call    calc_delay_loops    ; Input: AH=type, BX=speed, %1=microseconds
    
    ; Execute delay loop
    call    execute_delay_loop  ; Input: BX=loop count
    
    pop     bx
    pop     ax
%endmacro

; Macro: Port I/O delay (adaptive)
%macro IO_DELAY 0
    ; Adaptive I/O delay based on CPU speed
    ; Uses: AL (preserved)
    push    ax
    call    get_io_delay_type
    cmp     al, IO_DELAY_NONE
    je      %%no_delay
    cmp     al, IO_DELAY_SHORT
    je      %%short_delay
    cmp     al, IO_DELAY_MEDIUM  
    je      %%medium_delay
    
    ; Long delay
    nop
    nop
%%medium_delay:
    nop
%%short_delay:    
    nop
%%no_delay:
    pop     ax
%endmacro

; Macro: EEPROM access delay
%macro EEPROM_DELAY 0
    DELAY_US EEPROM_ACCESS_TIME
%endmacro

; Macro: NIC reset delay
%macro NIC_RESET_DELAY 0
    DELAY_US NIC_RESET_TIME
%endmacro

; Macro: DMA setup delay
%macro DMA_SETUP_DELAY 0
    DELAY_US DMA_SETUP_TIME
%endmacro

; Macro: IRQ settling delay
%macro IRQ_SETTLE_DELAY 0
    DELAY_US IRQ_SETTLE_TIME
%endmacro

; Macro: High precision port read with timing
%macro TIMED_PORT_READ 2
    ; Read port with proper timing
    ; Input: %1 = port, %2 = destination
    IO_DELAY
    mov     dx, %1
    in      %2, dx
    IO_DELAY
%endmacro

; Macro: High precision port write with timing  
%macro TIMED_PORT_WRITE 2
    ; Write port with proper timing
    ; Input: %1 = port, %2 = value
    IO_DELAY
    mov     dx, %1
    mov     al, %2
    out     dx, al
    IO_DELAY
%endmacro

; Macro: Burst I/O delay (for block transfers)
%macro BURST_IO_DELAY 0
    ; Minimal delay for burst I/O operations
    push    ax
    call    get_cpu_speed
    cmp     bx, BUS_SPEED_25MHZ
    jge     %%no_delay
    nop                         ; Only delay on slower systems
%%no_delay:
    pop     ax
%endmacro

; Macro: Network timing delay (protocol-specific)
%macro NET_TIMING_DELAY 1
    ; Network protocol timing delay
    ; Input: %1 = protocol timing requirement in us
%if %1 <= 1
    IO_DELAY
%elif %1 <= 5
    IO_DELAY
    IO_DELAY  
    nop
%else
    DELAY_US %1
%endif
%endmacro

; Macro: Adaptive loop delay based on CPU detection
%macro ADAPTIVE_DELAY 2
    ; Adaptive delay that scales with CPU performance
    ; Input: %1 = base delay (us), %2 = scaling factor
    push    ax
    push    bx
    
    call    get_cpu_type
    cmp     al, CPU_80286
    je      %%delay_286
    cmp     al, CPU_80386  
    je      %%delay_386
    cmp     al, CPU_80486
    je      %%delay_486
    
    ; Pentium+ - minimal delay
    DELAY_US (%1 / 4)
    jmp     %%done
    
%%delay_486:
    DELAY_US (%1 / 2)
    jmp     %%done
    
%%delay_386:
    DELAY_US (%1 * 3 / 4)
    jmp     %%done
    
%%delay_286:
    DELAY_US %1
    
%%done:
    pop     bx
    pop     ax
%endmacro

; Critical section timing macros
%macro BEGIN_CRITICAL_TIMING 0
    ; Enter timing-critical section
    pushf
    cli                         ; Disable interrupts
    ; Additional timing setup could go here
%endmacro

%macro END_CRITICAL_TIMING 0
    ; Exit timing-critical section  
    popf                        ; Restore interrupt state
%endmacro

; Hardware-specific timing constants for 3Com NICs
; 3C509B specific timing
C509B_CMD_TIME         equ 10      ; Command execution time
C509B_FIFO_TIME        equ 2       ; FIFO access time
C509B_RESET_TIME       equ 1000    ; Reset recovery time

; 3C515-TX specific timing  
C515_DMA_SETUP_TIME    equ 5       ; DMA descriptor setup
C515_BUSMASTER_TIME    equ 10      ; Bus mastering setup
C515_RESET_TIME        equ 2000    ; Reset recovery time

; NIC-specific timing macros
%macro C509B_CMD_DELAY 0
    DELAY_US C509B_CMD_TIME
%endmacro

%macro C509B_FIFO_DELAY 0
    DELAY_US C509B_FIFO_TIME  
%endmacro

%macro C515_DMA_DELAY 0
    DELAY_US C515_DMA_SETUP_TIME
%endmacro

%macro C515_BUSMASTER_DELAY 0
    DELAY_US C515_BUSMASTER_TIME
%endmacro

; External function prototypes (to be implemented in cpu_detect.c)
extern get_cpu_type             ; Returns CPU type in AL
extern get_cpu_speed            ; Returns CPU speed in BX (MHz)
extern get_io_delay_type        ; Returns I/O delay type in AL
extern calc_delay_loops         ; Calculate delay loops for timing
extern execute_delay_loop       ; Execute calibrated delay loop
extern calibrate_timing         ; One-time timing calibration

%endif ; TIMING_MACROS_INC