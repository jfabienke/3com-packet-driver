# API Reference

Complete technical reference for the 3Com Packet Driver API implementation.

## Table of Contents

1. [API Overview](#api-overview)
2. [Standard Packet Driver Functions](#standard-packet-driver-functions)
3. [Extended Functions](#extended-functions)
4. [Data Structures](#data-structures)
5. [Error Codes](#error-codes)
6. [Programming Examples](#programming-examples)
7. [Assembly Interface](#assembly-interface)
8. [C Interface](#c-interface)
9. [Advanced Features](#advanced-features)
10. [Compatibility Notes](#compatibility-notes)

## API Overview

The 3Com Packet Driver implements the Packet Driver Specification v1.11 with additional enhancements for multi-homing, load balancing, and advanced networking features.

### Interrupt Vector

**INT 60h** - Primary packet driver interrupt
- **Interrupt Number**: 0x60 (default, configurable)
- **Calling Convention**: Standard DOS function call
- **Register Usage**: AH=function, AL=subfunction, other registers as specified

### Function Categories

1. **Basic Functions (01h-07h)**: Core packet driver operations
2. **Extended Functions (14h-19h)**: Enhanced packet driver features  
3. **Advanced Functions (20h-29h)**: Multi-NIC and QoS features
4. **Diagnostic Functions (F0h-FFh)**: Testing and debugging

### Entry Point Detection

```assembly
; Check if packet driver is loaded
mov ah, 1                  ; Function 1: Driver Info
mov dl, 0                  ; Interface 0
int 60h                    ; Call packet driver
jc not_loaded              ; CF set = driver not loaded
; Driver is loaded, BX = version
```

## Standard Packet Driver Functions

### Function 01h: Driver Information

**Purpose**: Get driver version and capabilities

**Input**:
- AH = 01h
- AL = 00h (reserved)
- DL = Interface number (0-based)

**Output**:
- CF = 0 if successful, 1 if error
- BH = Driver major version
- BL = Driver minor version  
- CH = Driver class (1 = Ethernet)
- CL = Driver type (1 = 3Com)
- DX = Driver flags
- DS:SI = Driver name string (ASCIIZ)

**Example**:
```assembly
mov ah, 1                  ; Function 1
mov dl, 0                  ; Interface 0
int 60h                    ; Call driver
jc error                   ; Check for error
; BX contains version (e.g., 0x0100 = v1.0)
```

**Driver Flags (DX)**:
- Bit 0: High performance mode available
- Bit 1: Extended functions supported
- Bit 2: Multi-interface capable
- Bit 3: Load balancing supported
- Bit 4: QoS features available
- Bit 8: Statistics collection enabled
- Bit 9: Error recovery available

### Function 02h: Access Type

**Purpose**: Register packet type and receive handler

**Input**:
- AH = 02h
- AL = Interface number (0-based)
- BX = Packet type (or FFFFh for all types)
- DL = Packet class (1 = Ethernet)
- CX = Length of type (2 for Ethernet)
- DS:SI = Pointer to packet type
- ES:DI = Receive handler address

**Output**:
- CF = 0 if successful, 1 if error
- AX = Handle (0-based) if successful, error code if failed

**Packet Types**:
- 0x0800: IP (Internet Protocol)
- 0x0806: ARP (Address Resolution Protocol)
- 0x8137: IPX (Internetwork Packet Exchange)
- 0xFFFF: All packet types (promiscuous)

**Example**:
```assembly
; Register for IP packets
mov ah, 2                  ; Function 2: Access Type
mov al, 0                  ; Interface 0
mov bx, 0x0800            ; IP packet type
mov dl, 1                  ; Ethernet class
mov cx, 2                  ; Type length
mov si, offset ip_type     ; Point to type
mov di, offset rx_handler  ; Receive handler
push ds
pop es                     ; ES:DI = handler
int 60h                    ; Call driver
jc error                   ; Check error
mov [ip_handle], ax        ; Save handle
```

**Receive Handler Convention**:
```assembly
rx_handler:
    ; Entry: AX = handle
    ;        CX = packet length
    ;        DS:SI = packet data
    ; Must preserve all registers except AX
    pushf
    push bx
    push cx
    push dx
    push si
    push di
    push ds
    push es
    
    ; Process packet here
    
    pop es
    pop ds
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    popf
    retf                   ; Far return
```

### Function 03h: Release Type

**Purpose**: Unregister packet type handler

**Input**:
- AH = 03h
- BX = Handle (from Function 02h)

**Output**:
- CF = 0 if successful, 1 if error
- AX = Error code if failed

**Example**:
```assembly
mov ah, 3                  ; Function 3: Release Type
mov bx, [ip_handle]        ; Handle to release
int 60h                    ; Call driver
jc error                   ; Check error
```

### Function 04h: Send Packet

**Purpose**: Transmit a packet

**Input**:
- AH = 04h
- CX = Packet length (14-1514 bytes)
- DS:SI = Packet data
- ES:DI = Optional completion handler

**Output**:
- CF = 0 if successful, 1 if error
- AX = Error code if failed

**Packet Format**:
```
Offset  Size  Description
0       6     Destination MAC address
6       6     Source MAC address (filled by driver)
12      2     Ethernet type/length
14      n     Packet data
```

**Example**:
```assembly
; Send IP packet
mov ah, 4                  ; Function 4: Send Packet
mov cx, 60                 ; Packet length
mov si, offset packet_data ; Packet buffer
xor di, di                 ; No completion handler
int 60h                    ; Send packet
jc error                   ; Check error
```

### Function 05h: Terminate

**Purpose**: Terminate driver (unload)

**Input**:
- AH = 05h
- BX = Handle (must be valid handle)

**Output**:
- CF = 0 if successful, 1 if error
- AX = Error code if failed

**Note**: Only works if driver was loaded from AUTOEXEC.BAT, not CONFIG.SYS

### Function 06h: Get Address

**Purpose**: Get hardware (MAC) address

**Input**:
- AH = 06h
- BX = Handle
- CX = Buffer length (must be >= 6)
- ES:DI = Buffer for address

**Output**:
- CF = 0 if successful, 1 if error
- CX = Actual address length (6 for Ethernet)
- ES:DI = Buffer filled with MAC address

**Example**:
```assembly
mov ah, 6                  ; Function 6: Get Address
mov bx, [handle]           ; Valid handle
mov cx, 6                  ; Buffer size
mov di, offset mac_addr    ; Buffer
push ds
pop es                     ; ES:DI = buffer
int 60h                    ; Call driver
jc error                   ; Check error
; mac_addr now contains 6-byte MAC address
```

### Function 07h: Reset Interface

**Purpose**: Reset network interface

**Input**:
- AH = 07h
- BX = Handle

**Output**:
- CF = 0 if successful, 1 if error
- AX = Error code if failed

**Effects**:
- Resets network hardware
- Clears all statistics
- Reinitializes buffers
- May cause brief network interruption

## Extended Functions

### Function 14h: Set Receive Mode

**Purpose**: Configure packet filtering mode

**Input**:
- AH = 14h
- BX = Handle
- CX = Mode flags

**Mode Flags**:
- Bit 0: Directed packets (unicast to this station)
- Bit 1: Broadcast packets
- Bit 2: Limited multicast (specific addresses)
- Bit 3: All multicast packets  
- Bit 4: Promiscuous mode (all packets)
- Bit 5: Error packets (packets with errors)

**Example**:
```assembly
; Enable unicast + broadcast + multicast
mov ah, 14h                ; Function 14h
mov bx, [handle]           ; Handle
mov cx, 0007h              ; Bits 0,1,2 set
int 60h                    ; Set mode
jc error
```

### Function 15h: Get Receive Mode

**Purpose**: Query current packet filtering mode

**Input**:
- AH = 15h
- BX = Handle

**Output**:
- CF = 0 if successful, 1 if error
- CX = Current mode flags

### Function 18h: Get Statistics

**Purpose**: Retrieve interface statistics

**Input**:
- AH = 18h
- BX = Handle
- CX = Statistics structure size
- ES:DI = Buffer for statistics

**Output**:
- CF = 0 if successful, 1 if error
- CX = Actual size returned
- ES:DI = Buffer filled with statistics

**Statistics Structure**:
```c
struct packet_statistics {
    uint32_t packets_in;       /* Packets received */
    uint32_t packets_out;      /* Packets transmitted */
    uint32_t bytes_in;         /* Bytes received */
    uint32_t bytes_out;        /* Bytes transmitted */
    uint32_t errors_in;        /* Receive errors */
    uint32_t errors_out;       /* Transmit errors */
    uint32_t packets_lost;     /* Dropped packets */
    uint32_t collisions;       /* Collision count */
    uint32_t crc_errors;       /* CRC errors */
    uint32_t alignment_errors; /* Alignment errors */
    uint32_t overruns;         /* Buffer overruns */
    uint32_t underruns;        /* Buffer underruns */
};
```

### Function 19h: Set Address

**Purpose**: Set hardware (MAC) address

**Input**:
- AH = 19h
- BX = Handle
- CX = Address length (6 for Ethernet)
- DS:SI = New MAC address

**Output**:
- CF = 0 if successful, 1 if error

**Note**: Not supported on all hardware. Use with caution.

## Extended Functions (3Com Specific)

### Function 20h: Set Handle Priority

**Purpose**: Set application priority for multi-application support

**Input**:
- AH = 20h
- BX = Handle
- CL = Priority (0-255, higher = more priority)
- CH = Reserved (0)

**Output**:
- CF = 0 if successful, 1 if error

**Priority Levels**:
- 0-63: Background applications
- 64-127: Normal applications
- 128-191: High priority applications
- 192-255: Critical/real-time applications

### Function 21h: Get Routing Information

**Purpose**: Query routing table and load balancing status

**Input**:
- AH = 21h
- BX = Handle
- CX = Buffer size
- ES:DI = Buffer for routing info

**Output**:
- CF = 0 if successful, 1 if error
- CX = Actual size returned
- ES:DI = Buffer filled with routing information

### Function 22h: Set Load Balance

**Purpose**: Configure load balancing parameters

**Input**:
- AH = 22h
- BX = Handle
- CL = Load balance mode
- CH = Primary NIC preference
- DX = Configuration flags

**Load Balance Modes**:
- 0: Round-robin
- 1: Weighted by performance
- 2: Connection-aware
- 3: Application-specific

### Function 23h: Get NIC Status

**Purpose**: Get status of specific network interface

**Input**:
- AH = 23h
- BX = Handle
- CL = NIC index (0-based)
- ES:DI = Buffer for status structure

**Output**:
- CF = 0 if successful, 1 if error
- ES:DI = Buffer filled with NIC status

**NIC Status Structure**:
```c
struct nic_status {
    uint8_t nic_index;         /* NIC index */
    uint8_t status;            /* Status flags */
    uint16_t link_speed;       /* Speed in Mbps */
    uint8_t duplex_mode;       /* 0=half, 1=full */
    uint8_t link_state;        /* 0=down, 1=up */
    uint32_t utilization;      /* Utilization % */
    uint32_t error_count;      /* Error count */
    char description[32];      /* NIC description */
};
```

### Function 24h: Set QoS Parameters

**Purpose**: Configure Quality of Service parameters

**Input**:
- AH = 24h
- BX = Handle
- CL = Priority class (0-7)
- DX = Bandwidth limit (KB/s, 0=unlimited)
- ES:DI = QoS parameters structure

**QoS Parameters Structure**:
```c
struct qos_params {
    uint8_t priority_class;    /* 0-7 priority */
    uint8_t drop_policy;       /* Drop policy */
    uint16_t max_latency;      /* Max latency (ms) */
    uint32_t min_bandwidth;    /* Min bandwidth guarantee */
    uint32_t max_bandwidth;    /* Max bandwidth limit */
};
```

### Function 25h: Get Flow Statistics

**Purpose**: Get per-flow statistics for connection tracking

**Input**:
- AH = 25h
- BX = Handle
- CX = Buffer size
- ES:DI = Buffer for flow statistics

**Output**:
- CF = 0 if successful, 1 if error
- CX = Number of flows returned
- ES:DI = Buffer with flow statistics

## Data Structures

### Handle Structure

```c
typedef struct {
    uint16_t handle_id;        /* Unique handle identifier */
    uint16_t packet_type;      /* Registered packet type */
    uint8_t interface_num;     /* Interface number */
    uint8_t flags;             /* Handle flags */
    void far *receiver_func;   /* Receive handler */
    uint8_t priority;          /* Application priority */
    uint32_t bandwidth_limit;  /* Bandwidth limit */
    uint32_t packets_received; /* Packet count */
    uint32_t bytes_received;   /* Byte count */
} packet_handle_t;
```

### Packet Header Format

```c
typedef struct {
    uint8_t dest_addr[6];      /* Destination MAC */
    uint8_t src_addr[6];       /* Source MAC */
    uint16_t type_length;      /* Type/Length field */
    /* Packet data follows */
} ethernet_header_t;
```

### Statistics Structure (Extended)

```c
typedef struct {
    /* Basic statistics */
    uint32_t packets_in;
    uint32_t packets_out;
    uint32_t bytes_in;
    uint32_t bytes_out;
    uint32_t errors_in;
    uint32_t errors_out;
    uint32_t packets_lost;
    
    /* Extended statistics */
    uint32_t multicast_in;
    uint32_t broadcast_in;
    uint32_t collisions;
    uint32_t crc_errors;
    uint32_t alignment_errors;
    uint32_t overruns;
    uint32_t underruns;
    uint32_t late_collisions;
    uint32_t excessive_collisions;
    uint32_t carrier_errors;
    
    /* Performance statistics */
    uint32_t interrupts;
    uint32_t dma_operations;
    uint32_t buffer_reallocs;
    uint32_t memory_errors;
    
    /* Multi-NIC statistics */
    uint32_t packets_routed;
    uint32_t load_balance_switches;
    uint32_t failover_events;
    uint32_t recovery_events;
} extended_statistics_t;
```

## Error Codes

### Standard Error Codes

| Code | Symbol | Description |
|------|--------|-------------|
| 0 | NO_ERROR | Operation successful |
| 1 | BAD_HANDLE | Invalid handle |
| 2 | NO_CLASS | Packet class not supported |
| 3 | NO_TYPE | Packet type not supported |
| 4 | NO_NUMBER | Interface number invalid |
| 5 | BAD_TYPE | Bad packet type specified |
| 6 | NO_MULTICAST | Multicast not supported |
| 7 | CANT_TERMINATE | Can't terminate driver |
| 8 | BAD_MODE | Invalid receive mode |
| 9 | NO_SPACE | Insufficient space |
| 10 | TYPE_INUSE | Type already in use |
| 11 | BAD_COMMAND | Invalid function number |
| 12 | CANT_SEND | Can't send packet |
| 13 | CANT_SET | Can't set hardware address |
| 14 | BAD_ADDRESS | Invalid address |
| 15 | CANT_RESET | Can't reset interface |

### Extended Error Codes (3Com Specific)

| Code | Symbol | Description |
|------|--------|-------------|
| 128 | NO_HANDLES | No handles available |
| 129 | ROUTING_FAILED | Routing operation failed |
| 130 | NIC_UNAVAILABLE | NIC not available |
| 131 | BANDWIDTH_EXCEEDED | Bandwidth limit exceeded |
| 132 | PRIORITY_CONFLICT | Priority conflict |
| 133 | QOS_NOT_SUPPORTED | QoS not supported |
| 134 | LOAD_BALANCE_FAILED | Load balancing failed |
| 135 | TOPOLOGY_CHANGED | Network topology changed |
| 136 | HARDWARE_ERROR | Hardware error detected |
| 137 | CONFIGURATION_ERROR | Configuration error |
| 138 | RESOURCE_EXHAUSTED | System resources exhausted |
| 139 | FEATURE_DISABLED | Feature disabled |

## Programming Examples

### C Language Examples

#### Basic Packet Reception

```c
#include <dos.h>
#include <stdio.h>

#define PD_INT 0x60

/* Packet driver function numbers */
#define PD_DRIVER_INFO    1
#define PD_ACCESS_TYPE    2
#define PD_SEND_PACKET    4
#define PD_GET_ADDRESS    6

/* Global variables */
int pd_handle = -1;
unsigned char our_mac[6];

/* Receive handler - called for each packet */
void far packet_receiver(void) {
    /* AX = handle, CX = length, DS:SI = data */
    /* This is just a stub - real handler would process packet */
    asm {
        push ax
        push bx
        push cx
        push dx
        push si
        push di
        push ds
        push es
        
        /* Packet processing code here */
        
        pop es
        pop ds
        pop di
        pop si
        pop dx
        pop cx
        pop bx
        pop ax
        
        retf        /* Far return */
    }
}

/* Initialize packet driver */
int init_packet_driver(void) {
    union REGS regs;
    struct SREGS sregs;
    unsigned char packet_type[2] = {0x08, 0x00}; /* IP */
    
    /* Check if driver is loaded */
    regs.h.ah = PD_DRIVER_INFO;
    regs.h.dl = 0;  /* Interface 0 */
    int86(PD_INT, &regs, &regs);
    if (regs.x.cflag) {
        printf("Packet driver not loaded\n");
        return -1;
    }
    
    printf("Driver version %d.%d\n", regs.h.bh, regs.h.bl);
    
    /* Get our MAC address */
    regs.h.ah = PD_GET_ADDRESS;
    regs.x.bx = 0;  /* Any handle works for this */
    regs.x.cx = 6;  /* Buffer size */
    sregs.es = FP_SEG(our_mac);
    regs.x.di = FP_OFF(our_mac);
    int86x(PD_INT, &regs, &regs, &sregs);
    if (regs.x.cflag) {
        printf("Can't get MAC address\n");
        return -1;
    }
    
    printf("MAC address: %02X:%02X:%02X:%02X:%02X:%02X\n",
           our_mac[0], our_mac[1], our_mac[2],
           our_mac[3], our_mac[4], our_mac[5]);
    
    /* Register for IP packets */
    regs.h.ah = PD_ACCESS_TYPE;
    regs.h.al = 0;  /* Interface 0 */
    regs.x.bx = 0x0800;  /* IP packet type */
    regs.h.dl = 1;  /* Ethernet class */
    regs.x.cx = 2;  /* Type length */
    sregs.ds = FP_SEG(packet_type);
    regs.x.si = FP_OFF(packet_type);
    sregs.es = FP_SEG(packet_receiver);
    regs.x.di = FP_OFF(packet_receiver);
    int86x(PD_INT, &regs, &regs, &sregs);
    if (regs.x.cflag) {
        printf("Can't register packet type: error %d\n", regs.x.ax);
        return -1;
    }
    
    pd_handle = regs.x.ax;
    printf("Registered handle %d for IP packets\n", pd_handle);
    return 0;
}

/* Send a packet */
int send_packet(unsigned char *packet, int length) {
    union REGS regs;
    struct SREGS sregs;
    
    if (pd_handle < 0) return -1;
    
    regs.h.ah = PD_SEND_PACKET;
    regs.x.cx = length;
    sregs.ds = FP_SEG(packet);
    regs.x.si = FP_OFF(packet);
    regs.x.di = 0;  /* No completion handler */
    int86x(PD_INT, &regs, &regs, &sregs);
    
    return regs.x.cflag ? -regs.x.ax : 0;
}
```

#### Statistics Collection

```c
#include <dos.h>
#include <stdio.h>

struct packet_stats {
    unsigned long packets_in;
    unsigned long packets_out;
    unsigned long bytes_in;
    unsigned long bytes_out;
    unsigned long errors_in;
    unsigned long errors_out;
    unsigned long packets_lost;
};

int get_statistics(int handle, struct packet_stats *stats) {
    union REGS regs;
    struct SREGS sregs;
    
    regs.h.ah = 0x18;  /* Get statistics */
    regs.x.bx = handle;
    regs.x.cx = sizeof(struct packet_stats);
    sregs.es = FP_SEG(stats);
    regs.x.di = FP_OFF(stats);
    int86x(0x60, &regs, &regs, &sregs);
    
    return regs.x.cflag ? -regs.x.ax : 0;
}

void print_statistics(int handle) {
    struct packet_stats stats;
    
    if (get_statistics(handle, &stats) == 0) {
        printf("Statistics for handle %d:\n", handle);
        printf("  Packets in:  %lu\n", stats.packets_in);
        printf("  Packets out: %lu\n", stats.packets_out);
        printf("  Bytes in:    %lu\n", stats.bytes_in);
        printf("  Bytes out:   %lu\n", stats.bytes_out);
        printf("  Errors in:   %lu\n", stats.errors_in);
        printf("  Errors out:  %lu\n", stats.errors_out);
        printf("  Packets lost: %lu\n", stats.packets_lost);
    } else {
        printf("Error getting statistics\n");
    }
}
```

### Assembly Language Examples

#### Complete Assembly Interface

```assembly
.MODEL SMALL
.STACK 1000h

.DATA
pd_interrupt    equ 60h         ; Packet driver interrupt
handle          dw -1           ; Our handle
mac_address     db 6 dup(?)     ; Our MAC address
packet_type     dw 0800h        ; IP packet type
error_msg       db 'Error: $'
ok_msg          db 'OK$'

.CODE
START:
    mov ax, @data
    mov ds, ax
    
    ; Initialize packet driver
    call init_packet_driver
    jc error_exit
    
    ; Main program loop
main_loop:
    ; Check for keypress
    mov ah, 1
    int 16h
    jz main_loop
    
    ; Key pressed, exit
    call cleanup_packet_driver
    
error_exit:
    mov ax, 4C00h
    int 21h

; Initialize packet driver
init_packet_driver proc
    ; Check if driver loaded
    mov ah, 1                   ; Function 1: Driver info
    mov dl, 0                   ; Interface 0
    int pd_interrupt
    jc init_error
    
    ; Get MAC address
    mov ah, 6                   ; Function 6: Get address
    mov bx, 0                   ; Any handle
    mov cx, 6                   ; Address length
    mov di, offset mac_address
    push ds
    pop es
    int pd_interrupt
    jc init_error
    
    ; Register packet type
    mov ah, 2                   ; Function 2: Access type
    mov al, 0                   ; Interface 0
    mov bx, packet_type         ; Packet type
    mov dl, 1                   ; Ethernet class
    mov cx, 2                   ; Type length
    mov si, offset packet_type
    mov di, offset packet_handler
    push ds
    pop es
    int pd_interrupt
    jc init_error
    
    mov handle, ax              ; Save handle
    clc                         ; Clear carry (success)
    ret

init_error:
    stc                         ; Set carry (error)
    ret
init_packet_driver endp

; Packet receive handler
packet_handler proc far
    ; Entry: AX = handle, CX = length, DS:SI = packet data
    ; Must preserve all registers
    pushf
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push ds
    push es
    
    ; Process packet here
    ; For this example, just count it
    
    pop es
    pop ds
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    popf
    retf                        ; Far return
packet_handler endp

; Send a packet
; Input: DS:SI = packet data, CX = length
send_packet proc
    mov ah, 4                   ; Function 4: Send packet
    xor di, di                  ; No completion handler
    int pd_interrupt
    ret
send_packet endp

; Get statistics
; Input: BX = handle, ES:DI = buffer
get_stats proc
    mov ah, 18h                 ; Function 18h: Get statistics
    mov cx, 28                  ; Structure size
    int pd_interrupt
    ret
get_stats endp

; Cleanup
cleanup_packet_driver proc
    cmp handle, -1
    je cleanup_done
    
    mov ah, 3                   ; Function 3: Release type
    mov bx, handle
    int pd_interrupt
    
cleanup_done:
    ret
cleanup_packet_driver endp

END START
```

#### Multi-NIC Support Example

```assembly
; Extended multi-NIC functions
.DATA
nic_count       db 0
nic_handles     dw 8 dup(-1)
load_balance    db 0            ; Load balance mode

.CODE
; Initialize multiple NICs
init_multi_nic proc
    xor dl, dl                  ; Start with interface 0
    xor bl, bl                  ; Handle index
    
init_nic_loop:
    ; Check if interface exists
    mov ah, 1                   ; Driver info
    int pd_interrupt
    jc init_multi_done          ; No more interfaces
    
    ; Register for this interface
    mov ah, 2                   ; Access type
    mov al, dl                  ; Interface number
    push dx
    mov bx, 0800h              ; IP type
    mov dh, 1                   ; Ethernet class
    mov cx, 2                   ; Type length
    mov si, offset packet_type
    mov di, offset multi_packet_handler
    push ds
    pop es
    int pd_interrupt
    pop dx
    jc init_next_nic
    
    ; Save handle
    mov si, bx                  ; Handle index
    shl si, 1                   ; Word index
    mov nic_handles[si], ax     ; Save handle
    inc nic_count
    
init_next_nic:
    inc dl                      ; Next interface
    inc bl                      ; Next handle index
    cmp bl, 8                   ; Max 8 NICs
    jb init_nic_loop
    
init_multi_done:
    ret
init_multi_nic endp

; Multi-NIC packet handler
multi_packet_handler proc far
    ; Determine which NIC received packet
    ; AX = handle
    
    pushf
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push ds
    push es
    
    ; Find NIC index for this handle
    mov bx, 0
find_nic_loop:
    mov si, bx
    shl si, 1
    cmp ax, nic_handles[si]
    je found_nic
    inc bx
    cmp bx, 8
    jb find_nic_loop
    jmp handler_done            ; Handle not found
    
found_nic:
    ; BX = NIC index
    ; Process packet with NIC awareness
    
handler_done:
    pop es
    pop ds
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
    popf
    retf
multi_packet_handler endp

; Send packet with load balancing
send_packet_balanced proc
    ; Input: DS:SI = packet, CX = length
    
    ; Select NIC based on load balance mode
    mov al, load_balance
    cmp al, 0                   ; Round-robin?
    je round_robin_send
    cmp al, 1                   ; Performance-based?
    je performance_send
    ; Default to first NIC
    mov bx, nic_handles[0]
    jmp do_send
    
round_robin_send:
    ; Simple round-robin selection
    ; (Implementation details omitted)
    mov bx, nic_handles[0]      ; Use first NIC for example
    jmp do_send
    
performance_send:
    ; Select best performing NIC
    ; (Implementation details omitted)
    mov bx, nic_handles[0]      ; Use first NIC for example
    
do_send:
    mov ah, 4                   ; Send packet
    xor di, di                  ; No completion handler
    int pd_interrupt
    ret
send_packet_balanced endp
```

## Assembly Interface

### Register Conventions

**Input Registers**:
- AH: Function number
- AL: Subfunction (if applicable)
- BX: Handle (for handle-based functions)
- CX: Count/Length parameter
- DX: Flags/Options parameter
- DS:SI: Input data pointer
- ES:DI: Output buffer pointer

**Output Registers**:
- CF: Carry flag (0=success, 1=error)
- AX: Return value or error code
- BX: Secondary return value
- CX: Count/Length returned
- DX: Additional flags/status

**Preserved Registers**:
All registers except AX, BX, CX, DX are preserved across calls.

### Function Call Template

```assembly
; Template for packet driver calls
    mov ah, function_number     ; Set function
    mov al, subfunction         ; Set subfunction (if needed)
    mov bx, handle              ; Set handle (if needed)
    mov cx, parameter           ; Set parameter
    mov dx, flags               ; Set flags
    mov si, offset input_data   ; Input data
    mov di, offset output_buffer ; Output buffer
    push ds                     ; Set up ES:DI
    pop es
    int 60h                     ; Call packet driver
    jc error_handler            ; Check for error
    ; Success - process results
```

## C Interface

### Function Prototypes

```c
/* Basic packet driver functions */
int pd_driver_info(int interface, struct driver_info *info);
int pd_access_type(int interface, int packet_type, 
                   void far *handler, int *handle);
int pd_release_type(int handle);
int pd_send_packet(void *packet, int length);
int pd_get_address(int handle, unsigned char *address);
int pd_reset_interface(int handle);

/* Extended functions */
int pd_set_receive_mode(int handle, int mode);
int pd_get_receive_mode(int handle, int *mode);
int pd_get_statistics(int handle, struct packet_statistics *stats);
int pd_set_address(int handle, unsigned char *address);

/* 3Com-specific extensions */
int pd_set_priority(int handle, int priority);
int pd_get_routing_info(int handle, struct routing_info *info);
int pd_set_load_balance(int handle, int mode, int flags);
int pd_get_nic_status(int nic, struct nic_status *status);
int pd_set_qos_params(int handle, struct qos_params *params);
```

### Helper Macros

```c
/* Error checking macro */
#define PD_CHECK(call) \
    do { \
        int _err = (call); \
        if (_err != 0) { \
            printf("Packet driver error %d at %s:%d\n", \
                   _err, __FILE__, __LINE__); \
            return _err; \
        } \
    } while(0)

/* Handle validation */
#define PD_VALID_HANDLE(h) ((h) >= 0 && (h) < MAX_HANDLES)

/* Type definitions */
typedef void (far *packet_handler_t)(int handle, int length, void far *data);
```

### Complete C Example

```c
#include <stdio.h>
#include <dos.h>
#include "packet.h"

static int my_handle = -1;
static unsigned long packet_count = 0;

/* Packet receive handler */
void far packet_handler(void) {
    /* Called for each received packet */
    packet_count++;
    /* AX=handle, CX=length, DS:SI=data */
    /* Add packet processing here */
}

int main(void) {
    struct driver_info info;
    unsigned char mac_addr[6];
    int err;
    
    /* Initialize packet driver */
    err = pd_driver_info(0, &info);
    PD_CHECK(err);
    
    printf("Packet driver v%d.%d loaded\n", 
           info.version_major, info.version_minor);
    
    /* Get MAC address */
    err = pd_get_address(0, mac_addr);
    PD_CHECK(err);
    
    printf("MAC address: %02X:%02X:%02X:%02X:%02X:%02X\n",
           mac_addr[0], mac_addr[1], mac_addr[2],
           mac_addr[3], mac_addr[4], mac_addr[5]);
    
    /* Register for IP packets */
    err = pd_access_type(0, 0x0800, packet_handler, &my_handle);
    PD_CHECK(err);
    
    printf("Registered handle %d\n", my_handle);
    
    /* Main loop */
    printf("Press any key to exit...\n");
    while (!kbhit()) {
        /* Let packets be received */
        delay(100);
    }
    
    /* Cleanup */
    if (my_handle >= 0) {
        pd_release_type(my_handle);
    }
    
    printf("Received %lu packets\n", packet_count);
    return 0;
}
```

## Advanced Features

### Multi-Application Support

The driver supports up to 16 simultaneous applications through handle multiplexing:

```c
/* Application priority levels */
#define PRIORITY_BACKGROUND  0
#define PRIORITY_NORMAL     64
#define PRIORITY_HIGH      128
#define PRIORITY_CRITICAL  192

/* Set application priority */
pd_set_priority(handle, PRIORITY_HIGH);
```

### Load Balancing

For systems with multiple NICs:

```c
/* Load balancing modes */
#define LB_ROUND_ROBIN    0
#define LB_WEIGHTED       1
#define LB_PERFORMANCE    2
#define LB_CONNECTION     3

/* Configure load balancing */
pd_set_load_balance(handle, LB_PERFORMANCE, 0);
```

### Quality of Service

```c
struct qos_params {
    int priority_class;     /* 0-7 */
    long min_bandwidth;     /* Minimum guaranteed */
    long max_bandwidth;     /* Maximum allowed */
    int max_latency;        /* Maximum latency (ms) */
};

pd_set_qos_params(handle, &qos);
```

### Flow Control

```c
/* Enable hardware flow control */
pd_set_flow_control(handle, FLOW_CONTROL_ON);

/* Set receive mode for flow awareness */
pd_set_receive_mode(handle, MODE_DIRECTED | MODE_FLOW_AWARE);
```

## Compatibility Notes

### DOS Version Compatibility

- **DOS 2.x**: Basic functionality only
- **DOS 3.x**: Full standard features
- **DOS 5.x+**: All extended features including UMB support
- **Windows 3.x**: Limited functionality in DOS box
- **Windows 95+**: DOS mode only

### Compiler Compatibility

**Borland C/C++**:
```c
#pragma argsused
void far interrupt packet_handler(void) {
    /* Borland-specific handler */
}
```

**Microsoft C**:
```c
void far _interrupt packet_handler(void) {
    /* Microsoft-specific handler */
}
```

**Watcom C**:
```c
void __far __interrupt packet_handler(void) {
    /* Watcom-specific handler */
}
```

### Memory Model Considerations

**Small Model**: Handles up to 8 applications
**Medium Model**: Handles up to 16 applications  
**Large Model**: No practical limits

### TSR Compatibility

The driver is compatible with common TSRs:
- HIMEM.SYS
- EMM386.EXE
- QEMM
- Mouse drivers
- Sound drivers (with proper IRQ management)

### Application Compatibility

**Tested Applications**:
- mTCP suite (all utilities)
- Trumpet Winsock
- Novell NetWare client
- Microsoft Network Client
- Various DOS games with IPX support

**Known Issues**:
- Some applications require specific packet types
- Windows applications may need special handling
- Real-mode only - no protected mode support

---

For additional programming examples and integration guides, see:
- [User Manual](USER_MANUAL.md) - Complete user guide
- [Configuration Guide](CONFIGURATION.md) - Configuration examples
- [Troubleshooting Guide](TROUBLESHOOTING.md) - Problem resolution
- [Performance Tuning Guide](PERFORMANCE_TUNING.md) - Optimization strategies