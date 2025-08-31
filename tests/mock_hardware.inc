; mock_hardware.inc
; Mock hardware responses for both NIC types - Assembly include file
; 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
;
; This file provides comprehensive hardware simulation responses for both
; 3C509B (ISA with ID sequence) and 3C515-TX (PCI-like with DMA) NICs.
; Includes realistic register values, EEPROM contents, and state transitions.

%ifndef MOCK_HARDWARE_INC
%define MOCK_HARDWARE_INC

; Mock device configuration constants
MOCK_MAX_DEVICES        equ 8
MOCK_EEPROM_SIZE        equ 256
MOCK_PACKET_BUFFER_SIZE equ 1600
MOCK_MAX_PACKETS        equ 32

; Mock device types
MOCK_TYPE_NONE          equ 0
MOCK_TYPE_3C509B        equ 1
MOCK_TYPE_3C515         equ 2

; Mock device states
MOCK_STATE_DISABLED     equ 0
MOCK_STATE_DETECTED     equ 1
MOCK_STATE_ACTIVATED    equ 2
MOCK_STATE_CONFIGURED   equ 3
MOCK_STATE_OPERATIONAL  equ 4
MOCK_STATE_ERROR        equ 5

; 3C509B specific constants
C509B_ID_PORT           equ 0x100       ; ID port address
C509B_ACTIVATION_SEQ    equ 0xFF        ; Activation sequence pattern
C509B_CONTENTION_DELAY  equ 162         ; Contention resolution delay
C509B_PRODUCT_ID        equ 0x5090      ; 3C509B product ID
C509B_MFG_ID            equ 0x6D50      ; 3Com manufacturer ID

; 3C515 specific constants  
C515_DEVICE_ID          equ 0x5150      ; 3C515 device ID
C515_VENDOR_ID          equ 0x10B7      ; 3Com vendor ID
C515_DMA_THRESHOLD      equ 128         ; DMA threshold
C515_FIFO_SIZE          equ 2048        ; FIFO buffer size

; Register window definitions (3C509B/3C515 shared concept)
WINDOW_SETUP            equ 0           ; Setup/configuration window
WINDOW_OPERATING        equ 1           ; Operating registers
WINDOW_STATION_ADDR     equ 2           ; Station address window  
WINDOW_FIFO             equ 3           ; FIFO registers
WINDOW_DIAGNOSTICS      equ 4           ; Diagnostic registers
WINDOW_RESULTS          equ 5           ; Result registers
WINDOW_STATISTICS       equ 6           ; Statistics window
WINDOW_BUS_MASTER       equ 7           ; Bus master window (3C515 only)

; Mock EEPROM layouts
;
; 3C509B EEPROM Layout:
EEPROM_3C509B_MFG_ID    equ 0           ; Manufacturer ID
EEPROM_3C509B_PROD_ID   equ 1           ; Product ID  
EEPROM_3C509B_ADDR_CFG  equ 8           ; Address configuration
EEPROM_3C509B_RESOURCE  equ 9           ; Resource configuration
EEPROM_3C509B_OEM_ADDR0 equ 10          ; OEM station address word 0
EEPROM_3C509B_OEM_ADDR1 equ 11          ; OEM station address word 1
EEPROM_3C509B_OEM_ADDR2 equ 12          ; OEM station address word 2
EEPROM_3C509B_CHECKSUM  equ 15          ; EEPROM checksum

; 3C515 EEPROM Layout:
EEPROM_3C515_VENDOR_ID  equ 0           ; Vendor ID
EEPROM_3C515_DEVICE_ID  equ 1           ; Device ID
EEPROM_3C515_SUBSYS_VEN equ 2           ; Subsystem vendor ID
EEPROM_3C515_SUBSYS_DEV equ 3           ; Subsystem device ID
EEPROM_3C515_ADDR0      equ 4           ; Station address word 0
EEPROM_3C515_ADDR1      equ 5           ; Station address word 1
EEPROM_3C515_ADDR2      equ 6           ; Station address word 2
EEPROM_3C515_CAPABILITIES equ 7         ; Capabilities word
EEPROM_3C515_CHECKSUM   equ 15          ; EEPROM checksum

; Mock register structures
struc MockDevice
    .type               resb 1          ; Device type (MOCK_TYPE_*)
    .state              resb 1          ; Current state
    .io_base            resw 1          ; I/O base address
    .irq                resb 1          ; IRQ line
    .dma_channel        resb 1          ; DMA channel (3C515 only)
    .current_window     resb 1          ; Current register window
    .mac_address        resb 6          ; MAC address
    .link_status        resb 1          ; Link status (0=down, 1=up)
    .link_speed         resb 1          ; Link speed (10/100)
    .duplex_mode        resb 1          ; Duplex mode (0=half, 1=full)
    .promiscuous        resb 1          ; Promiscuous mode flag
    
    ; Register simulation
    .command_reg        resw 1          ; Command register
    .status_reg         resw 1          ; Status register
    .int_mask_reg       resw 1          ; Interrupt mask register
    .int_status_reg     resw 1          ; Interrupt status register
    .tx_status_reg      resw 1          ; TX status register
    .rx_status_reg      resw 1          ; RX status register
    .tx_free_reg        resw 1          ; TX free bytes register
    .rx_bytes_reg       resw 1          ; RX bytes available register
    
    ; EEPROM simulation
    .eeprom_data        resw MOCK_EEPROM_SIZE/2  ; EEPROM contents
    .eeprom_addr        resb 1          ; Current EEPROM address
    .eeprom_busy        resb 1          ; EEPROM busy flag
    
    ; DMA simulation (3C515 only)
    .dma_tx_desc        resd 1          ; TX descriptor address
    .dma_rx_desc        resd 1          ; RX descriptor address
    .dma_status         resw 1          ; DMA status register
    .dma_command        resw 1          ; DMA command register
    
    ; Packet queues
    .tx_queue_head      resb 1          ; TX queue head pointer
    .tx_queue_tail      resb 1          ; TX queue tail pointer
    .rx_queue_head      resb 1          ; RX queue head pointer
    .rx_queue_tail      resb 1          ; RX queue tail pointer
    .tx_packets         resb MOCK_PACKET_BUFFER_SIZE * MOCK_MAX_PACKETS
    .rx_packets         resb MOCK_PACKET_BUFFER_SIZE * MOCK_MAX_PACKETS
    .packet_lengths     resw MOCK_MAX_PACKETS * 2  ; TX and RX packet lengths
    
    ; Statistics
    .tx_packet_count    resd 1          ; Transmitted packets
    .rx_packet_count    resd 1          ; Received packets
    .tx_byte_count      resd 1          ; Transmitted bytes
    .rx_byte_count      resd 1          ; Received bytes
    .tx_error_count     resd 1          ; TX errors
    .rx_error_count     resd 1          ; RX errors
    .collision_count    resd 1          ; Collisions
    .interrupt_count    resd 1          ; Interrupts generated
    
    ; Error injection
    .error_inject_mask  resw 1          ; Error injection mask
    .error_trigger_count resw 1         ; Operations until error trigger
    .error_type         resb 1          ; Type of error to inject
endstruc

; Mock framework state structure
struc MockFramework
    .device_count       resb 1          ; Number of active devices
    .logging_enabled    resb 1          ; Logging flag
    .global_time        resd 1          ; Global timestamp counter
    .devices            resb MockDevice_size * MOCK_MAX_DEVICES
endstruc

; Global mock framework instance
mock_framework istruc MockFramework
    at MockFramework.device_count,      db 0
    at MockFramework.logging_enabled,   db 1
    at MockFramework.global_time,       dd 0
iend

; Default 3C509B configuration
default_3c509b_config:
    istruc MockDevice
        at MockDevice.type,             db MOCK_TYPE_3C509B
        at MockDevice.state,            db MOCK_STATE_DISABLED
        at MockDevice.io_base,          dw 0x300
        at MockDevice.irq,              db 10
        at MockDevice.dma_channel,      db 0
        at MockDevice.current_window,   db WINDOW_SETUP
        at MockDevice.mac_address,      db 0x00, 0x60, 0x8C, 0x12, 0x34, 0x56
        at MockDevice.link_status,      db 1
        at MockDevice.link_speed,       db 10
        at MockDevice.duplex_mode,      db 0
        at MockDevice.promiscuous,      db 0
        ; Initialize key registers
        at MockDevice.status_reg,       dw 0x0000
        at MockDevice.tx_free_reg,      dw 1536
        at MockDevice.rx_bytes_reg,     dw 0
    iend

; Default 3C515 configuration  
default_3c515_config:
    istruc MockDevice
        at MockDevice.type,             db MOCK_TYPE_3C515
        at MockDevice.state,            db MOCK_STATE_DISABLED
        at MockDevice.io_base,          dw 0x200
        at MockDevice.irq,              db 11
        at MockDevice.dma_channel,      db 5
        at MockDevice.current_window,   db WINDOW_SETUP
        at MockDevice.mac_address,      db 0x00, 0x60, 0x8C, 0xAB, 0xCD, 0xEF
        at MockDevice.link_status,      db 1
        at MockDevice.link_speed,       db 100
        at MockDevice.duplex_mode,      db 1
        at MockDevice.promiscuous,      db 0
        ; Initialize key registers
        at MockDevice.status_reg,       dw 0x0000
        at MockDevice.tx_free_reg,      dw C515_FIFO_SIZE
        at MockDevice.rx_bytes_reg,     dw 0
    iend

; Mock response generation macros
;
; Generate 3C509B ID sequence response
%macro MOCK_3C509B_ID_RESPONSE 1
    push ax
    push cx
    
    ; Simulate ID sequence contention resolution
    mov cx, C509B_CONTENTION_DELAY
%%id_delay_loop:
    dec cx
    jnz %%id_delay_loop
    
    ; Set response based on device index %1
    mov al, %1
    or al, 0x80                 ; Set activation bit
    
    pop cx
    pop ax
%endmacro

; Generate EEPROM read response for 3C509B
%macro MOCK_3C509B_EEPROM_READ 2
    ; %1 = device pointer, %2 = EEPROM address
    push bx
    push si
    
    mov si, %1
    mov bl, %2
    and bl, 0x0F                ; Limit to valid EEPROM range
    shl bl, 1                   ; Convert to word offset
    
    ; Load EEPROM data
    movzx bx, bl
    mov ax, [si + MockDevice.eeprom_data + bx]
    
    pop si
    pop bx
%endmacro

; Generate EEPROM read response for 3C515
%macro MOCK_3C515_EEPROM_READ 2
    ; %1 = device pointer, %2 = EEPROM address
    push bx
    push si
    
    mov si, %1
    mov bl, %2
    and bl, 0x0F                ; Limit to valid EEPROM range
    shl bl, 1                   ; Convert to word offset
    
    ; Load EEPROM data
    movzx bx, bl
    mov ax, [si + MockDevice.eeprom_data + bx]
    
    pop si
    pop bx
%endmacro

; Update device statistics
%macro MOCK_UPDATE_STATS 3
    ; %1 = device pointer, %2 = stat field offset, %3 = increment
    push eax
    push ebx
    
    mov ebx, %1
    mov eax, [ebx + %2]
    add eax, %3
    mov [ebx + %2], eax
    
    pop ebx
    pop eax
%endmacro

; Simulate register window selection
%macro MOCK_SELECT_WINDOW 2
    ; %1 = device pointer, %2 = window number
    push si
    push ax
    
    mov si, %1
    mov al, %2
    and al, 7                   ; Limit to valid windows (0-7)
    mov [si + MockDevice.current_window], al
    
    pop ax
    pop si
%endmacro

; Generate interrupt based on device state
%macro MOCK_GENERATE_INTERRUPT 2
    ; %1 = device pointer, %2 = interrupt type
    push si
    push ax
    
    mov si, %1
    
    ; Update interrupt status register
    mov ax, [si + MockDevice.int_status_reg]
    or ax, %2
    mov [si + MockDevice.int_status_reg], ax
    
    ; Increment interrupt counter
    inc dword [si + MockDevice.interrupt_count]
    
    ; Set global interrupt flag if not masked
    mov ax, [si + MockDevice.int_mask_reg]
    test ax, %2
    jz %%interrupt_masked
    
    ; Interrupt is enabled - would trigger system interrupt here
    ; In real implementation, this would call interrupt handler
    
%%interrupt_masked:
    pop ax
    pop si
%endmacro

; Simulate packet transmission
%macro MOCK_TRANSMIT_PACKET 3
    ; %1 = device pointer, %2 = packet data pointer, %3 = packet length
    push si
    push di
    push cx
    push ax
    
    mov si, %1
    
    ; Check if TX queue has space
    mov al, [si + MockDevice.tx_queue_tail]
    inc al
    and al, MOCK_MAX_PACKETS - 1
    cmp al, [si + MockDevice.tx_queue_head]
    je %%tx_queue_full
    
    ; Get TX queue tail slot
    movzx ax, byte [si + MockDevice.tx_queue_tail]
    mov cx, MOCK_PACKET_BUFFER_SIZE
    mul cx
    add ax, MockDevice.tx_packets
    add ax, si
    mov di, ax
    
    ; Copy packet to TX queue
    mov si, %2
    mov cx, %3
    cmp cx, MOCK_PACKET_BUFFER_SIZE
    jbe %%tx_copy_ok
    mov cx, MOCK_PACKET_BUFFER_SIZE
%%tx_copy_ok:
    rep movsb
    
    ; Store packet length
    mov si, %1
    movzx ax, byte [si + MockDevice.tx_queue_tail]
    shl ax, 1
    add ax, MockDevice.packet_lengths
    add ax, si
    mov di, ax
    mov ax, cx
    mov [di], ax
    
    ; Update TX queue tail
    inc byte [si + MockDevice.tx_queue_tail]
    and byte [si + MockDevice.tx_queue_tail], MOCK_MAX_PACKETS - 1
    
    ; Update statistics
    MOCK_UPDATE_STATS si, MockDevice.tx_packet_count, 1
    MOCK_UPDATE_STATS si, MockDevice.tx_byte_count, cx
    
    ; Generate TX complete interrupt after delay
    MOCK_GENERATE_INTERRUPT si, 0x0004  ; TX complete interrupt
    
    jmp %%tx_done
    
%%tx_queue_full:
    ; Set error condition
    mov si, %1
    or word [si + MockDevice.status_reg], 0x0080  ; TX error bit
    MOCK_UPDATE_STATS si, MockDevice.tx_error_count, 1
    
%%tx_done:
    pop ax
    pop cx
    pop di
    pop si
%endmacro

; Simulate packet reception
%macro MOCK_RECEIVE_PACKET 1
    ; %1 = device pointer
    push si
    push ax
    
    mov si, %1
    
    ; Check if RX queue has packets
    mov al, [si + MockDevice.rx_queue_head]
    cmp al, [si + MockDevice.rx_queue_tail]
    je %%no_rx_packets
    
    ; Update RX queue head (consume packet)
    inc byte [si + MockDevice.rx_queue_head]
    and byte [si + MockDevice.rx_queue_head], MOCK_MAX_PACKETS - 1
    
    ; Update RX bytes available register
    movzx ax, byte [si + MockDevice.rx_queue_head]
    shl ax, 1
    add ax, MockDevice.packet_lengths + MOCK_MAX_PACKETS * 2
    add ax, si
    mov ax, [ax]  ; Get packet length
    mov [si + MockDevice.rx_bytes_reg], ax
    
    ; Update statistics
    MOCK_UPDATE_STATS si, MockDevice.rx_packet_count, 1
    MOCK_UPDATE_STATS si, MockDevice.rx_byte_count, ax
    
    ; Generate RX complete interrupt
    MOCK_GENERATE_INTERRUPT si, 0x0008  ; RX complete interrupt
    
    jmp %%rx_done
    
%%no_rx_packets:
    ; No packets available
    mov word [si + MockDevice.rx_bytes_reg], 0
    
%%rx_done:
    pop ax
    pop si
%endmacro

; Initialize default EEPROM contents
%macro INIT_3C509B_EEPROM 1
    ; %1 = device pointer
    push si
    push ax
    push bx
    
    mov si, %1
    add si, MockDevice.eeprom_data
    
    ; Manufacturer ID
    mov ax, C509B_MFG_ID
    mov [si + EEPROM_3C509B_MFG_ID * 2], ax
    
    ; Product ID
    mov ax, C509B_PRODUCT_ID
    mov [si + EEPROM_3C509B_PROD_ID * 2], ax
    
    ; Station address from device configuration
    mov bx, %1
    mov ax, [bx + MockDevice.mac_address]
    mov [si + EEPROM_3C509B_OEM_ADDR0 * 2], ax
    mov ax, [bx + MockDevice.mac_address + 2]
    mov [si + EEPROM_3C509B_OEM_ADDR1 * 2], ax
    mov ax, [bx + MockDevice.mac_address + 4]
    mov [si + EEPROM_3C509B_OEM_ADDR2 * 2], ax
    
    ; Address configuration (I/O base and IRQ)
    mov ax, [bx + MockDevice.io_base]
    shr ax, 4                   ; Convert to configuration format
    mov bl, [bx + MockDevice.irq]
    shl bl, 12                  ; IRQ in upper bits
    or ax, bx
    mov [si + EEPROM_3C509B_ADDR_CFG * 2], ax
    
    ; Resource configuration
    mov ax, 0x6000              ; Default resource configuration
    mov [si + EEPROM_3C509B_RESOURCE * 2], ax
    
    ; Calculate checksum (simple XOR checksum)
    mov ax, 0
    mov bx, 0
%%checksum_loop:
    xor ax, [si + bx]
    add bx, 2
    cmp bx, EEPROM_3C509B_CHECKSUM * 2
    jl %%checksum_loop
    
    xor ax, 0x67                ; Checksum constant
    mov [si + EEPROM_3C509B_CHECKSUM * 2], ax
    
    pop bx
    pop ax
    pop si
%endmacro

%macro INIT_3C515_EEPROM 1
    ; %1 = device pointer
    push si
    push ax
    push bx
    
    mov si, %1
    add si, MockDevice.eeprom_data
    
    ; Vendor ID
    mov ax, C515_VENDOR_ID
    mov [si + EEPROM_3C515_VENDOR_ID * 2], ax
    
    ; Device ID
    mov ax, C515_DEVICE_ID
    mov [si + EEPROM_3C515_DEVICE_ID * 2], ax
    
    ; Subsystem vendor ID
    mov ax, C515_VENDOR_ID
    mov [si + EEPROM_3C515_SUBSYS_VEN * 2], ax
    
    ; Subsystem device ID
    mov ax, 0x5150
    mov [si + EEPROM_3C515_SUBSYS_DEV * 2], ax
    
    ; Station address from device configuration
    mov bx, %1
    mov ax, [bx + MockDevice.mac_address]
    mov [si + EEPROM_3C515_ADDR0 * 2], ax
    mov ax, [bx + MockDevice.mac_address + 2]
    mov [si + EEPROM_3C515_ADDR1 * 2], ax
    mov ax, [bx + MockDevice.mac_address + 4]
    mov [si + EEPROM_3C515_ADDR2 * 2], ax
    
    ; Capabilities
    mov ax, 0x4020              ; 100Mbps, full-duplex capable
    mov [si + EEPROM_3C515_CAPABILITIES * 2], ax
    
    ; Calculate checksum
    mov ax, 0
    mov bx, 0
%%checksum_loop:
    xor ax, [si + bx]
    add bx, 2
    cmp bx, EEPROM_3C515_CHECKSUM * 2
    jl %%checksum_loop
    
    xor ax, 0x67                ; Checksum constant
    mov [si + EEPROM_3C515_CHECKSUM * 2], ax
    
    pop bx
    pop ax
    pop si
%endmacro

; Function prototypes (for external assembly functions)

; Initialize mock device with specified configuration
; Input: AL = device type, BX = I/O base, CL = IRQ
; Output: AX = device index, or -1 if failed
EXTERN mock_device_create

; Destroy mock device
; Input: AL = device index
EXTERN mock_device_destroy

; Set device MAC address
; Input: AL = device index, SI = MAC address pointer
EXTERN mock_device_set_mac

; Set device link status
; Input: AL = device index, BL = status (0/1), CL = speed (10/100)
EXTERN mock_device_set_link

; Inject packet for reception
; Input: AL = device index, SI = packet data, CX = packet length
EXTERN mock_packet_inject

; Extract transmitted packet
; Input: AL = device index, DI = buffer, CX = buffer size
; Output: CX = packet length, or 0 if no packet
EXTERN mock_packet_extract

; Generate mock interrupt
; Input: AL = device index, BL = interrupt type
EXTERN mock_generate_interrupt

; Clear all mock interrupts
; Input: AL = device index
EXTERN mock_clear_interrupts

; Read mock I/O port
; Input: DX = port address
; Output: AL/AX/EAX = port value
EXTERN mock_inb
EXTERN mock_inw
EXTERN mock_inl

; Write mock I/O port
; Input: DX = port address, AL/AX/EAX = value
EXTERN mock_outb
EXTERN mock_outw
EXTERN mock_outl

%endif ; MOCK_HARDWARE_INC