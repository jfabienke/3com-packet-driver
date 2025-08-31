# Packet Driver Specification Reference

## Overview
The Packet Driver Specification defines a standard interface for DOS network drivers using software interrupt 60h-80h.

## Standard Interrupt Vectors
- **INT 60h-7Fh**: Available for packet drivers
- **INT 60h**: Commonly used default
- **Multiple Drivers**: Use different interrupt numbers

## Function Codes

### Basic Functions
- **AH=01h**: driver_info - Get driver information
- **AH=02h**: access_type - Register packet type
- **AH=03h**: release_type - Unregister packet type  
- **AH=04h**: send_pkt - Transmit packet
- **AH=05h**: terminate - Unload driver
- **AH=06h**: get_address - Get hardware address

### Extended Functions  
- **AH=14h**: as_send_pkt - Send packet (async)
- **AH=15h**: set_rcv_mode - Set receive mode
- **AH=16h**: get_rcv_mode - Get receive mode
- **AH=17h**: set_multicast_list - Set multicast addresses
- **AH=18h**: get_multicast_list - Get multicast addresses
- **AH=19h**: get_statistics - Get driver statistics
- **AH=1Ah**: set_address - Set hardware address

## Function Interfaces

### driver_info (AH=01h)
Input: 
- AL = 0xFF (high performance driver check)
Output:
- CF = 0 if successful
- BX = Version (BCD format)
- CH = Class (1=Ethernet, 6=SLIP, etc.)
- CL = Type  
- DX = Number of interfaces
- DS:SI = Driver name (ASCIIZ)

### access_type (AH=02h)
Input:
- AL = Interface number
- BX = Packet type (0=all, 0800h=IP, 0806h=ARP)
- DL = Packet type length
- DS:SI = Packet type template
- ES:DI = Receiver function address
Output:
- CF = 0 if successful
- AX = Handle for this packet type

### send_pkt (AH=04h)
Input:
- CX = Length of packet
- DS:SI = Buffer address
- ES:DI = Destination address (NULL for broadcasts)
Output:
- CF = 0 if successful
- CF = 1 if error, DH = error code

## Error Codes
- **1**: Bad handle
- **2**: No class
- **3**: No type  
- **4**: No number
- **5**: Bad type
- **6**: No multicast available
- **8**: Can't send
- **9**: Can't set hardware address
- **10**: Can't reset interface
- **11**: Bad mode
- **12**: No space

## Receive Handler Interface
```asm
; Packet receive callback
; Input: AX = Handle, CX = Length, DS:SI = Packet buffer
; Must preserve all registers except AX
receive_handler:
    push bx
    push cx
    push dx
    ; Process packet data at DS:SI, length CX
    pop dx
    pop cx  
    pop bx
    retf    ; Far return
```

## Installation Signature
```asm
; Check for packet driver at interrupt vector
mov ax, 1234h        ; Test signature
int 60h              ; Call packet driver
cmp ax, 'PK'         ; Check for 'PK' signature
jne not_installed
```

## Memory Management
- **Receive Buffers**: Driver manages internal buffers
- **Transmit Buffers**: Application provides buffer
- **Callback Stack**: Driver switches to internal stack

## DOS Integration Notes
- Use DPMI for protected mode compatibility
- Handle Ctrl-C and Ctrl-Break properly  
- Support DOS box environments (Windows/OS2)
- Implement proper cleanup on program termination