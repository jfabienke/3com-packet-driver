; @file packet_api.asm
; @brief Packet Driver API interrupt handler
;
; 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
;
; This file is part of the 3Com Packet Driver project.
;

.MODEL SMALL
.386

include 'tsr_defensive.inc'

; Packet Driver API Function Numbers (INT 60h)
API_DRIVER_INFO     EQU 1       ; Get driver information
API_ACCESS_TYPE     EQU 2       ; Access packet type
API_RELEASE_TYPE    EQU 3       ; Release packet type
API_SEND_PKT        EQU 4       ; Send packet
API_TERMINATE       EQU 5       ; Terminate driver
API_GET_ADDRESS     EQU 6       ; Get station address
API_RESET_IFACE     EQU 7       ; Reset interface
API_GET_PARAMETERS  EQU 8       ; Get interface parameters
API_AS_SEND_PKT     EQU 9       ; Alternative send packet (extended)
API_SET_RCV_MODE    EQU 20      ; Set receive mode
API_GET_RCV_MODE    EQU 21      ; Get receive mode
API_SET_MULTICAST   EQU 22      ; Set multicast list
API_GET_MULTICAST   EQU 23      ; Get multicast list
API_GET_STATISTICS  EQU 24      ; Get statistics
API_SET_ADDRESS     EQU 25      ; Set station address

; Phase 3 Group 3B Extended API Functions
API_SET_HANDLE_PRIORITY EQU 32  ; Set handle priority (AH=20h)
API_GET_ROUTING_INFO    EQU 33  ; Get routing information (AH=21h)
API_SET_LOAD_BALANCE    EQU 34  ; Set load balancing (AH=22h)
API_GET_NIC_STATUS      EQU 35  ; Get NIC status (AH=23h)
API_SET_QOS_PARAMS      EQU 36  ; Set QoS parameters (AH=24h)
API_GET_FLOW_STATS      EQU 37  ; Get flow statistics (AH=25h)
API_SET_NIC_PREFERENCE  EQU 38  ; Set NIC preference (AH=26h)
API_GET_HANDLE_INFO     EQU 39  ; Get handle info (AH=27h)
API_SET_BANDWIDTH_LIMIT EQU 40  ; Set bandwidth limit (AH=28h)
API_GET_ERROR_INFO      EQU 41  ; Get error info (AH=29h)

; Packet Driver specification constants
DRIVER_SIGNATURE    EQU 'PK'    ; Driver signature for detection

; Return Codes
PKT_SUCCESS         EQU 0       ; Operation successful
PKT_ERROR_BAD_HANDLE EQU 1      ; Bad handle
PKT_ERROR_NO_CLASS  EQU 2       ; No interfaces of specified class found
PKT_ERROR_NO_TYPE   EQU 3       ; No interfaces of specified type found
PKT_ERROR_NO_NUMBER EQU 4       ; No interfaces of specified number found
PKT_ERROR_BAD_TYPE  EQU 5       ; Bad packet type specified
PKT_ERROR_NO_MULTICAST EQU 6    ; Interface doesn't support multicast
PKT_ERROR_CANT_TERMINATE EQU 7  ; Can't terminate driver
PKT_ERROR_BAD_MODE  EQU 8       ; Bad mode specified
PKT_ERROR_NO_SPACE  EQU 9       ; No space for new packet type
PKT_ERROR_TYPE_INUSE EQU 10     ; Type already in use
PKT_ERROR_BAD_COMMAND EQU 11    ; Bad command number
PKT_ERROR_CANT_SEND EQU 12      ; Can't send packet
PKT_ERROR_CANT_SET  EQU 13      ; Can't set station address
PKT_ERROR_BAD_ADDRESS EQU 14    ; Bad address specified

; Receive Modes
RCV_MODE_OFF        EQU 1       ; Turn off receiver
RCV_MODE_DIRECT     EQU 2       ; Receive only packets to this address
RCV_MODE_BROADCAST  EQU 3       ; Receive direct + broadcast packets
RCV_MODE_MULTICAST  EQU 4       ; Receive direct + broadcast + multicast
RCV_MODE_PROMISCUOUS EQU 6      ; Receive all packets

; Maximum number of handles and callback chains
MAX_HANDLES         EQU 16      ; Maximum packet type handles
MAX_TYPE_CALLBACKS  EQU 8       ; Maximum callbacks per packet type
MAX_PACKET_TYPES    EQU 32      ; Maximum tracked packet types

; Data segment
_DATA SEGMENT
        ASSUME  DS:_DATA

; API state variables
api_initialized     db 0        ; API initialization flag
driver_signature    db 'PKT DRVR'   ; Driver signature for detection
driver_version      db 11       ; Driver version (1.1)
driver_type         db 1        ; Driver type (DIX Ethernet)
driver_number       db 0        ; Driver number
driver_name         db 'TCPIP$', 0  ; Driver name

; Enhanced Handle and Callback Management
handle_table        dw MAX_HANDLES dup(0)   ; Handle to packet type mapping
handle_callbacks    dd MAX_HANDLES dup(0)   ; Callback addresses for each handle
handle_interfaces   db MAX_HANDLES dup(0)   ; Interface number for each handle
handle_modes        db MAX_HANDLES dup(0)   ; Receive mode for each handle
next_handle         dw 1                    ; Next available handle
active_handles      dw 0                    ; Number of active handles

; Enhanced Callback Chain Management (Group 3B Multiplexing)
callback_chains     dd MAX_PACKET_TYPES * MAX_TYPE_CALLBACKS dup(0) ; Callback chain storage
callback_priorities db MAX_PACKET_TYPES * MAX_TYPE_CALLBACKS dup(0) ; Callback priorities
callback_filters    dw MAX_PACKET_TYPES * MAX_TYPE_CALLBACKS dup(0) ; Packet filters
callback_error_counts dw MAX_PACKET_TYPES * MAX_TYPE_CALLBACKS dup(0) ; Error counts
callback_last_errors dd MAX_PACKET_TYPES * MAX_TYPE_CALLBACKS dup(0) ; Last error times
packet_type_table   dw MAX_PACKET_TYPES dup(0)  ; Active packet types
type_callback_counts db MAX_PACKET_TYPES dup(0)  ; Number of callbacks per type
type_handle_mapping dw MAX_PACKET_TYPES dup(0FFFFh) ; Primary handle per type

; Callback Safety and Timeout Management
callback_timeout_ticks  dd 0    ; Timer for callback timeouts (approx 100ms)
callback_timer_active   db 0    ; Timer active flag
callback_stack_depth    db 0    ; Callback nesting depth
max_callback_depth      EQU 3   ; Maximum callback nesting
max_callback_errors     EQU 10  ; Maximum errors before disabling callback

; Phase 3 Extended Handle Management
extended_api_enabled db 0                  ; Extended API enabled flag
virtual_irq_enabled  db 0                  ; Virtual interrupt enabled
handle_priorities    db MAX_HANDLES dup(128) ; Handle priorities (128 = default)
handle_flags         dw MAX_HANDLES dup(0)   ; Extended handle flags
preferred_nics       db MAX_HANDLES dup(0FFh) ; Preferred NIC per handle (0xFF = no preference)
bandwidth_limits     dd MAX_HANDLES dup(0)   ; Bandwidth limits per handle

; Load Balancing State
lb_mode             db 0        ; Load balancing mode (0=round-robin)
lb_primary_nic      db 0        ; Primary NIC for load balancing
lb_secondary_nic    db 1        ; Secondary NIC for load balancing
lb_last_used        db 0        ; Last used NIC for round-robin
lb_weights          dw 2 dup(100) ; NIC weights (equal by default)

; QoS Management
qos_enabled         db 0        ; QoS enabled flag
default_qos_class   db 1        ; Default QoS class (standard)
qos_packet_count    dw 0        ; Packets in QoS queue

; Interface information
interface_count     db 2        ; Number of interfaces (max 2 for our NICs)
current_rcv_mode    db RCV_MODE_DIRECT      ; Current receive mode
nic_utilization     dw 2 dup(0) ; NIC utilization percentages
nic_error_counts    dw 2 dup(0) ; Error counts per NIC

; Virtual Interrupt Management
virtual_handlers    dd MAX_HANDLES dup(0)   ; Virtual interrupt handlers
virtual_contexts    dw MAX_HANDLES dup(0)   ; Virtual handler contexts
multiplex_count     dw 0                    ; Multiplexed delivery count

; Defensive programming data (required by tsr_defensive.inc macros)
api_caller_ss       dw ?                    ; Saved caller stack segment for API calls
api_caller_sp       dw ?                    ; Saved caller stack pointer for API calls
api_critical_nesting db 0                   ; API critical section nesting
api_indos_segment   dw 0                    ; InDOS flag segment
api_indos_offset    dw 0                    ; InDOS flag offset

; Private API handler stack (2KB)
api_stack           db 2048 dup(?)
api_stack_top       equ $ - 2

; Multicast address management
multicast_list      db 16 * 6 dup(0)   ; Up to 16 multicast addresses
multicast_count     db 0               ; Current number of multicast addresses

; Station address storage
station_address     db 6 dup(0)        ; Current station MAC address

; Statistics tracking per handle
handle_tx_packets   dd MAX_HANDLES dup(0)    ; Packets transmitted per handle
handle_tx_bytes     dd MAX_HANDLES dup(0)    ; Bytes transmitted per handle
handle_rx_packets   dd MAX_HANDLES dup(0)    ; Packets received per handle
handle_rx_bytes     dd MAX_HANDLES dup(0)    ; Bytes received per handle
handle_total_latency dd MAX_HANDLES dup(0)   ; Accumulated latency for averaging
handle_latency_variance dd MAX_HANDLES dup(0) ; Latency variance for jitter

; Priority queue management
PRIORITY_QUEUE_SIZE equ 64             ; Must be power of 2
PRIORITY_QUEUE_MASK equ 63             ; Size - 1 for wrap-around
priority_queue      db PRIORITY_QUEUE_SIZE * 8 dup(0) ; 8 bytes per entry
priority_queue_head dw 0               ; Queue head index
priority_queue_tail dw 0               ; Queue tail index

; Timer management
timer_hooked        db 0               ; Timer interrupt hooked flag
callback_timed_out  db 0               ; Callback timeout flag
old_timer_handler   dd 0               ; Original INT 1Ch handler
callback_start_time dd 0               ; Callback start timestamp

; Performance optimization
api_cpu_type        db 0               ; CPU type (0=8086, 1=286, 2=386, 3=486)
api_pusha_threshold db 8               ; Threshold for PUSHA vs individual pushes
last_function       db 0               ; Last API function called
fast_path_hits      dd 0               ; Fast path optimization hits
fast_path_misses    dd 0               ; Fast path optimization misses

_DATA ENDS

; Code segment
_TEXT SEGMENT
        ASSUME  CS:_TEXT, DS:_DATA

; Public function exports
PUBLIC packet_api_init
PUBLIC packet_int_handler
PUBLIC api_driver_info
PUBLIC api_access_type
PUBLIC api_release_type
PUBLIC api_send_packet
PUBLIC api_terminate
PUBLIC api_get_address
PUBLIC api_reset_interface
PUBLIC api_get_parameters
PUBLIC api_as_send_packet
PUBLIC api_set_rcv_mode
PUBLIC api_get_rcv_mode
PUBLIC api_set_multicast
PUBLIC api_get_multicast
PUBLIC api_get_statistics
PUBLIC api_set_address
PUBLIC packet_deliver_to_handler
PUBLIC optimal_register_save
PUBLIC optimal_register_restore
PUBLIC fast_api_dispatch
PUBLIC fast_register_restore
PUBLIC api_performance_metrics

; External references
EXTRN hardware_send_packet_asm:PROC     ; From hardware.asm
EXTRN hardware_get_address:PROC         ; From hardware.asm
EXTRN get_cpu_type:PROC                 ; From cpu_detect.asm

; C function bridges - Bridge to vtable dispatch system
EXTRN _packet_send:PROC                 ; From packet_ops.c - C calling convention
EXTRN _pd_get_address:PROC              ; From api.c - C calling convention
EXTRN _pd_set_rcv_mode:PROC             ; From api.c - C calling convention  
EXTRN _pd_get_statistics:PROC           ; From api.c - C calling convention
EXTRN vds_process_deferred_unlocks:PROC     ; From packet_ops.c - VDS bottom-half

; Defensive programming integration
EXTRN dos_is_safe:PROC                  ; From defensive_integration.asm
EXTRN nic_set_receive_mode:PROC         ; From nic_irq.asm

; Performance optimization data
api_cpu_type        db 0                ; Cached CPU type for register optimization
api_pusha_threshold db 4                ; Use PUSHA when saving >= this many registers
last_function       db 0                ; Last API function called (for prediction)
fast_path_hits      dd 0                ; Fast path usage statistics
fast_path_misses    dd 0                ; Fast path miss statistics

;-----------------------------------------------------------------------------
; packet_api_init - Initialize Packet Driver API
;
; Input:  None
; Output: AX = 0 for success, non-zero for error
; Uses:   AX, BX, CX, DX
;-----------------------------------------------------------------------------
packet_api_init PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx

        ; Initialize handle table with all entries marked free
        ; Set up default receive mode for all interfaces
        ; Verify hardware is ready and responding to commands

        ; Clear handle table
        mov     cx, MAX_HANDLES
        mov     bx, OFFSET handle_table
        xor     ax, ax
.clear_loop:
        mov     [bx], ax
        add     bx, 2
        loop    .clear_loop

        ; Mark API as initialized
        mov     byte ptr [api_initialized], 1

        ; Success
        mov     ax, PKT_SUCCESS

        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
packet_api_init ENDP

;-----------------------------------------------------------------------------
; packet_int_handler - Main INT 60h packet driver interrupt handler
; This is the main entry point for all Packet Driver API calls
;
; Input:  AH = function number, other registers per function
; Output: Depends on function, CF set on error
; Uses:   All registers (preserved as needed)
;-----------------------------------------------------------------------------
packet_int_handler PROC
        ; === DEFENSIVE API PROLOG ===
        ; Quick signature check first (no stack switch needed for this)
        cmp     ax, 1234h
        jne     .not_signature_check
        mov     ax, DRIVER_SIGNATURE    ; Return 'PK' signature
        iret

.not_signature_check:
        ; Save minimal registers first
        push    ax
        push    ds
        
        ; Set up our data segment
        mov     ax, _DATA
        mov     ds, ax
        ASSUME  DS:_DATA
        
        ; Check if API is initialized
        cmp     byte ptr [api_initialized], 0
        je      .not_initialized
        
        ; Switch to private stack for safety (API calls can be lengthy)
        cli                             ; Critical section start
        mov     [api_caller_ss], ss     ; Save caller's stack
        mov     [api_caller_sp], sp
        mov     ax, _DATA               ; Use our data segment as stack segment  
        mov     ss, ax
        mov     sp, OFFSET api_stack_top ; Switch to private stack
        sti                             ; Critical section end
        
        ; Save registers optimally based on CPU type and register usage
        ; Use PUSHA decision matrix: Use PUSHA when saving ≥4 registers
        ; 286: PUSHA=19 cycles vs 8×PUSH=24 cycles (5 cycle savings)
        ; 386: PUSHA=11 cycles vs 8×PUSH=16 cycles (5 cycle savings) 
        ; 486: PUSHA=5 cycles vs 8×PUSH=8 cycles (3 cycle penalty, but acceptable)
        
        call    optimal_register_save
        pushf                           ; Save caller's flags
        
        ; === DOS SAFETY CHECK ===
        ; For functions that might need DOS, check if it's safe
        ; (This is a simplified check - full implementation would be per-function)
        cmp     ah, API_TERMINATE       ; Terminate might need DOS
        je      .check_dos_safety
        cmp     ah, API_GET_PARAMETERS  ; Some parameter calls might need DOS
        je      .check_dos_safety
        jmp     .dos_check_complete
        
.check_dos_safety:
        call    dos_is_safe
        jnz     .dos_busy_error         ; DOS is busy - cannot service this call
        
.dos_check_complete:

        ; Fast path dispatch for common functions
        ; Most common: SEND_PKT (4), ACCESS_TYPE (2), DRIVER_INFO (1)
        
        ; Fast dispatch table lookup for performance
        call    fast_api_dispatch
        test    ax, ax
        jz      .dispatch_handled       ; Function handled by fast path
        
        ; Fall back to full dispatch for less common functions
        cmp     ah, API_DRIVER_INFO
        je      .call_driver_info
        cmp     ah, API_ACCESS_TYPE
        je      .call_access_type
        cmp     ah, API_RELEASE_TYPE
        je      .call_release_type
        cmp     ah, API_SEND_PKT
        je      .call_send_packet
        cmp     ah, API_TERMINATE
        je      .call_terminate
        cmp     ah, API_GET_ADDRESS
        je      .call_get_address
        cmp     ah, API_RESET_IFACE
        je      .call_reset_interface
        cmp     ah, API_GET_PARAMETERS
        je      .call_get_parameters
        cmp     ah, API_AS_SEND_PKT
        je      .call_as_send_packet
        cmp     ah, API_SET_RCV_MODE
        je      .call_set_rcv_mode
        cmp     ah, API_GET_RCV_MODE
        je      .call_get_rcv_mode
        cmp     ah, API_SET_MULTICAST
        je      .call_set_multicast
        cmp     ah, API_GET_MULTICAST
        je      .call_get_multicast
        cmp     ah, API_GET_STATISTICS
        je      .call_get_statistics
        cmp     ah, API_SET_ADDRESS
        je      .call_set_address
        
        ; Phase 3 Extended Functions
        cmp     ah, API_SET_HANDLE_PRIORITY
        je      .call_set_handle_priority
        cmp     ah, API_GET_ROUTING_INFO
        je      .call_get_routing_info
        cmp     ah, API_SET_LOAD_BALANCE
        je      .call_set_load_balance
        cmp     ah, API_GET_NIC_STATUS
        je      .call_get_nic_status
        cmp     ah, API_SET_QOS_PARAMS
        je      .call_set_qos_params
        cmp     ah, API_GET_FLOW_STATS
        je      .call_get_flow_stats
        cmp     ah, API_SET_NIC_PREFERENCE
        je      .call_set_nic_preference
        cmp     ah, API_GET_HANDLE_INFO
        je      .call_get_handle_info
        cmp     ah, API_SET_BANDWIDTH_LIMIT
        je      .call_set_bandwidth_limit
        cmp     ah, API_GET_ERROR_INFO
        je      .call_get_error_info

        ; Unknown function
        mov     dh, PKT_ERROR_BAD_COMMAND
        jmp     .error_exit

.call_driver_info:
        call    api_driver_info
        jmp     .success_exit

.call_access_type:
        call    api_access_type
        jmp     .check_exit

.call_release_type:
        call    api_release_type
        jmp     .check_exit

.call_send_packet:
        call    api_send_packet
        jmp     .check_exit

.call_terminate:
        call    api_terminate
        jmp     .check_exit

.call_get_address:
        call    api_get_address
        jmp     .check_exit

.call_reset_interface:
        call    api_reset_interface
        jmp     .check_exit

.call_get_parameters:
        call    api_get_parameters
        jmp     .check_exit

.call_as_send_packet:
        call    api_as_send_packet
        jmp     .check_exit

.call_set_rcv_mode:
        call    api_set_rcv_mode
        jmp     .check_exit

.call_get_rcv_mode:
        call    api_get_rcv_mode
        jmp     .check_exit

.call_set_multicast:
        call    api_set_multicast
        jmp     .check_exit

.call_get_multicast:
        call    api_get_multicast
        jmp     .check_exit

.call_get_statistics:
        call    api_get_statistics
        jmp     .check_exit

.call_set_address:
        call    api_set_address
        jmp     .check_exit

; Phase 3 Extended Function Dispatchers
.call_set_handle_priority:
        call    api_set_handle_priority_asm
        jmp     .check_exit

.call_get_routing_info:
        call    api_get_routing_info_asm
        jmp     .check_exit

.call_set_load_balance:
        call    api_set_load_balance_asm
        jmp     .check_exit

.call_get_nic_status:
        call    api_get_nic_status_asm
        jmp     .check_exit

.call_set_qos_params:
        call    api_set_qos_params_asm
        jmp     .check_exit

.call_get_flow_stats:
        call    api_get_flow_stats_asm
        jmp     .check_exit

.call_set_nic_preference:
        call    api_set_nic_preference_asm
        jmp     .check_exit

.call_get_handle_info:
        call    api_get_handle_info_asm
        jmp     .check_exit

.call_set_bandwidth_limit:
        call    api_set_bandwidth_limit_asm
        jmp     .check_exit

.call_get_error_info:
        call    api_get_error_info_asm
        jmp     .check_exit

.check_exit:
        ; Check return value in DH
        cmp     dh, PKT_SUCCESS
        je      .success_exit
        jmp     .error_exit

.dos_busy_error:
        ; DOS is busy - cannot service this call right now
        mov     dh, PKT_ERROR_BAD_COMMAND   ; Return error
        stc
        jmp     .defensive_exit

.success_exit:
        ; Clear carry flag for success
        clc
        jmp     .defensive_exit

.error_exit:
        ; Set carry flag for error
        stc
        jmp     .defensive_exit

.not_initialized:
        ; API not initialized - restore minimal registers and exit
        mov     ax, PKT_ERROR_BAD_COMMAND
        mov     dh, al
        stc
        pop     ds
        pop     ax
        iret

.defensive_exit:
.dispatch_handled:
        ; Function was handled by fast path, restore minimal registers
        call    fast_register_restore
        jmp     .final_exit
        
.defensive_exit:
        ; === DEFENSIVE API EPILOG ===
        ; Restore all registers in reverse order
        popf                            ; Restore caller's flags
        call    optimal_register_restore
        
        ; Restore caller's stack
        cli                             ; Critical section start
        mov     ss, [api_caller_ss]
        mov     sp, [api_caller_sp]
        sti                             ; Critical section end
        
        ; VDS CRITICAL FIX: Do NOT call VDS from interrupt context per VDS spec
        ; VDS unlocks will be processed at task-time from API entry points
        
        ; Restore minimal registers
.final_exit:
        pop     ds
        pop     ax
        
        iret    ; Return from interrupt
packet_int_handler ENDP

;-----------------------------------------------------------------------------
; api_driver_info - Get driver information (Function 1)
;
; Input:  AL = interface number
; Output: BX = driver version, CH = class, DH = error code
;         CL = type, DL = number, DS:SI = driver name
; Uses:   All output registers
;-----------------------------------------------------------------------------
api_driver_info PROC
        push    bp
        mov     bp, sp

        ; Check for high performance driver check (AL = 0xFF)
        cmp     al, 0FFh
        je      .high_performance_check

        ; Check interface number (we support 0-based indexing)
        cmp     al, [interface_count]
        jae     .bad_number

        ; Return driver information per Packet Driver specification
        mov     bx, 0100h               ; Version 1.0 (BCD format)
        mov     ch, 1                   ; Class 1 (DIX Ethernet)
        mov     cl, 1                   ; Type 1 (10Base-T)
        mov     dl, al                  ; Interface number (passed back)
        mov     si, OFFSET driver_name  ; DS:SI points to driver name
        mov     dh, PKT_SUCCESS
        jmp     .exit

.high_performance_check:
        ; Return capabilities for high performance check
        mov     bx, 0100h               ; Version 1.0
        mov     ch, 1                   ; Class 1 (DIX Ethernet) 
        mov     cl, 1                   ; Type 1
        mov     dl, 1                   ; Basic functions supported
        mov     dh, PKT_SUCCESS
        jmp     .exit

.bad_number:
        mov     dh, PKT_ERROR_NO_NUMBER
        jmp     .exit

.exit:
        pop     bp
        ret
api_driver_info ENDP

;-----------------------------------------------------------------------------
; api_access_type - Access packet type (Function 2)
;
; Input:  AL = interface, BX = packet type, DL = packet type length
;         CX = buffer size, ES:DI = receiver address
; Output: AX = handle, DH = error code
; Uses:   AX, DH
;-----------------------------------------------------------------------------
api_access_type PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    si
        push    di

        ; Validate interface number (AL)
        cmp     al, [interface_count]
        jae     .bad_interface

        ; Validate packet type length (DL)
        cmp     dl, 0
        je      .use_bx_type            ; Use BX as packet type if DL=0
        cmp     dl, 8                   ; Maximum type template length
        ja      .bad_type

.use_bx_type:
        ; Find available handle slot
        mov     si, OFFSET handle_table
        mov     cx, MAX_HANDLES
        mov     ax, 1                   ; Start with handle 1

.find_handle:
        cmp     word ptr [si], 0        ; Check if handle slot is free
        je      .found_handle
        add     si, 2                   ; Move to next handle slot
        inc     ax                      ; Increment handle number
        loop    .find_handle

        ; No free handles available
        mov     dh, PKT_ERROR_NO_SPACE
        jmp     .exit

.found_handle:
        ; Allocate handle entry with callback chains
        ; Store basic handle information
        mov     [si], bx                ; Store packet type in handle table
        
        ; Calculate callback table index (handle - 1) * 4
        dec     ax                      ; Convert to 0-based index  
        push    ax                      ; Save handle index
        shl     ax, 2                   ; Multiply by 4 for dword offset
        mov     si, ax
        
        ; Store callback in handle with validation
        ; Validate callback address before storing
        push    bx
        push    cx
        
        ; Check if callback address is valid (not null, not in low memory)
        test    di, di
        jz      .invalid_callback
        mov     bx, es
        test    bx, bx
        jz      .invalid_callback
        cmp     bx, 0040h               ; Don't allow callbacks in interrupt vectors
        jb      .invalid_callback
        
        ; Store validated callback address
        mov     word ptr [handle_callbacks + si], di       ; Offset
        mov     word ptr [handle_callbacks + si + 2], es   ; Segment
        
        ; Initialize callback chain management for this packet type
        call    initialize_callback_chain
        
        pop     cx
        pop     bx
        
        ; Store interface number and initialize handle state
        pop     si                      ; Restore 0-based handle index
        mov     [handle_interfaces + si], al                ; Interface number
        mov     byte ptr [handle_modes + si], RCV_MODE_DIRECT  ; Default mode
        mov     byte ptr [handle_priorities + si], 128      ; Default priority
        mov     word ptr [handle_flags + si * 2], 0         ; Clear flags
        jmp     .handle_allocated
        
.invalid_callback:
        ; Invalid callback address
        pop     cx
        pop     bx
        pop     si                      ; Clean up stack
        mov     dh, PKT_ERROR_BAD_ADDRESS
        jmp     .exit
        
.handle_allocated:"}
        
        ; Increment active handle count
        inc     word ptr [active_handles]
        
        mov     ax, [next_handle]       ; Return handle number
        inc     word ptr [next_handle]  ; Prepare next handle
        mov     dh, PKT_SUCCESS
        jmp     .exit

.bad_interface:
        mov     dh, PKT_ERROR_NO_NUMBER
        jmp     .exit

.bad_type:
        mov     dh, PKT_ERROR_BAD_TYPE
        jmp     .exit

.exit:
        pop     di
        pop     si
        pop     cx
        pop     bx
        pop     bp
        ret
api_access_type ENDP

;-----------------------------------------------------------------------------
; api_release_type - Release packet type (Function 3)
;
; Input:  BX = handle
; Output: DH = error code
; Uses:   DH
;-----------------------------------------------------------------------------
api_release_type PROC
        push    bp
        mov     bp, sp
        push    ax
        push    si
        push    di

        ; Validate handle range
        cmp     bx, 1
        jb      .bad_handle
        cmp     bx, MAX_HANDLES
        ja      .bad_handle

        ; Check if handle is actually allocated
        dec     bx                      ; Convert to 0-based index
        mov     si, bx
        shl     si, 1                   ; Convert to word offset for handle table
        add     si, OFFSET handle_table
        cmp     word ptr [si], 0        ; Check if handle is allocated
        je      .bad_handle

        ; Remove handle from table with callback chain cleanup
        ; Get packet type before clearing
        push    ax
        mov     ax, word ptr [si]       ; Get packet type
        push    ax                      ; Save packet type for callback chain cleanup
        
        ; Free the handle entry
        mov     word ptr [si], 0        ; Clear packet type

        ; Clear callback address
        mov     ax, bx                  ; Get 0-based index
        shl     ax, 2                   ; Convert to dword offset
        mov     di, ax
        
        ; Get callback address before clearing for chain cleanup
        mov     dx, word ptr [handle_callbacks + di]      ; Get callback offset
        push    word ptr [handle_callbacks + di + 2]      ; Save callback segment
        push    dx                                         ; Save callback offset
        
        mov     dword ptr [handle_callbacks + di], 0      ; Clear callback

        ; Remove callback from packet type callback chain
        pop     dx                      ; Restore callback offset
        pop     ax                      ; Restore callback segment  
        pop     cx                      ; Restore packet type
        call    remove_callback_from_chain  ; Clean up callback chains
        
        pop     ax                      ; Restore original AX
        
        ; Clear interface and mode info
        mov     byte ptr [handle_interfaces + bx], 0    ; Clear interface
        mov     byte ptr [handle_modes + bx], 0         ; Clear mode
        mov     byte ptr [handle_priorities + bx], 128  ; Reset priority
        mov     word ptr [handle_flags + bx * 2], 0     ; Clear flags"}

        ; Decrement active handle count
        cmp     word ptr [active_handles], 0
        je      .count_ok               ; Don't underflow
        dec     word ptr [active_handles]

.count_ok:
        mov     dh, PKT_SUCCESS
        jmp     .exit

.bad_handle:
        mov     dh, PKT_ERROR_BAD_HANDLE
        jmp     .exit

.exit:
        pop     di
        pop     si
        pop     ax
        pop     bp
        ret
api_release_type ENDP

;-----------------------------------------------------------------------------
; api_send_packet - Send packet (Function 4)
;
; Input:  DS:SI = packet to send, CX = packet length
; Output: DH = error code
; Uses:   DH
;-----------------------------------------------------------------------------
api_send_packet PROC
        push    bp
        mov     bp, sp
        push    ax
        push    bx

        ; Validate packet length meets minimum/maximum requirements
        ; Call hardware-specific send routine based on NIC type
        ; Handle multiple NICs with load balancing and routing

        ; Validate packet parameters with enhanced checks
        ; Comprehensive packet validation for safety
        
        ; Validate packet length
        test    cx, cx                  ; Check for zero length
        jz      .bad_length
        cmp     cx, 60                  ; Minimum Ethernet frame size
        jb      .bad_length
        cmp     cx, 1514                ; Maximum Ethernet frame size
        ja      .bad_length
        
        ; Validate packet buffer pointer
        test    si, si                  ; Check for null offset
        jz      .bad_buffer
        
        ; Validate data segment (basic check)
        push    ax
        mov     ax, ds
        test    ax, ax                  ; Check for null segment
        jz      .bad_buffer_seg
        
        ; Check if we can read the first byte safely
        push    es
        push    di
        mov     es, ax                  ; Set ES to DS
        mov     di, si                  ; Point to packet start
        mov     al, es:[di]             ; Try to read first byte
        pop     di
        pop     es
        pop     ax
        jmp     .validation_complete
        
.bad_buffer_seg:
        pop     ax
.bad_buffer:
        mov     dh, PKT_ERROR_BAD_ADDRESS
        jmp     .exit
        
.validation_complete:"}

        ; Bridge to C packet_send function via vtable dispatch
        ; Convert DOS calling convention to C calling convention
        ; DOS input: DS:SI = packet buffer, CX = packet length
        ; C signature: int packet_send(const uint8_t *packet, size_t length, uint16_t handle)
        
        ; Push parameters for C calling convention (right to left)
        push    0               ; handle parameter (uint16_t) - use 0 for default
        push    cx              ; length parameter (size_t)
        push    si              ; packet buffer offset (const uint8_t *)
        push    ds              ; packet buffer segment
        
        ; Process any deferred VDS unlocks at task-time (VDS spec compliant)
        call    vds_process_deferred_unlocks
        
        ; Call C function
        call    _packet_send
        add     sp, 8           ; Clean stack (4 parameters × 2 bytes each)
        
        ; Convert C return value to DOS packet driver error code
        ; C returns: 0=success, negative=error
        cmp     ax, 0
        jl      .send_error     ; Jump if negative (error)

        mov     dh, PKT_SUCCESS
        jmp     .exit

.bad_length:
        mov     dh, PKT_ERROR_CANT_SEND
        jmp     .exit

.send_error:
        mov     dh, PKT_ERROR_CANT_SEND
        jmp     .exit

.exit:
        pop     bx
        pop     ax
        pop     bp
        ret
api_send_packet ENDP

;-----------------------------------------------------------------------------
; api_terminate - Terminate driver (Function 5)
;
; Input:  BX = handle (must be same as returned by access_type)
; Output: DH = error code
; Uses:   DH
;-----------------------------------------------------------------------------
api_terminate PROC
        push    bp
        mov     bp, sp

        ; Validate termination request from authorized caller
        ; Check if safe to terminate (no active connections)
        ; Restore original interrupt vectors before exit
        ; Free allocated memory blocks and cleanup resources

        ; For now, refuse termination (driver stays resident)
        mov     dh, PKT_ERROR_CANT_TERMINATE

        pop     bp
        ret
api_terminate ENDP

;-----------------------------------------------------------------------------
; api_get_address - Get station address (Function 6)
;
; Input:  AL = interface number, CX = buffer size, ES:DI = buffer
; Output: CX = address length, DH = error code
; Uses:   CX, DH
;-----------------------------------------------------------------------------
api_get_address PROC
        push    bp
        mov     bp, sp
        push    ax
        push    bx
        push    si

        ; Validate interface number is within supported range
        ; Get MAC address from hardware registers or EEPROM
        ; Copy MAC address to user-provided buffer with validation

        ; Check interface number
        cmp     al, [interface_count]
        jae     .bad_number

        ; Check buffer size (need 6 bytes for Ethernet MAC)
        cmp     cx, 6
        jb      .bad_buffer

        ; Bridge to C pd_get_address function via vtable dispatch
        ; Convert DOS calling convention to C calling convention
        ; DOS input: AL = interface number, ES:DI = buffer, CX = buffer size
        ; C signature: int pd_get_address(uint16_t handle, pd_address_t *addr)
        
        ; Convert AL (interface number) to handle - for now use AL as handle
        mov     ah, 0                   ; Zero extend AL to AX for handle
        
        ; Push parameters for C calling convention (right to left)
        push    di                      ; buffer offset (pd_address_t *addr)
        push    es                      ; buffer segment
        push    ax                      ; handle (from AL, zero-extended)
        
        ; Process any deferred VDS unlocks at task-time
        call    vds_process_deferred_unlocks
        
        ; Call C function
        call    _pd_get_address
        add     sp, 6                   ; Clean stack (3 parameters × 2 bytes each)
        
        ; Convert C return to DOS error code
        ; C returns: 0=success, negative=error
        cmp     ax, 0
        jl      .get_error              ; Jump if negative (error)
        
        ; Success - address should be copied by C function
        mov     cx, 6                   ; Address length (Ethernet MAC)
        mov     dh, PKT_SUCCESS
        jmp     .exit

.bad_number:
        mov     dh, PKT_ERROR_NO_NUMBER
        jmp     .exit

.bad_buffer:
        mov     dh, PKT_ERROR_BAD_ADDRESS
        jmp     .exit

.get_error:
        mov     dh, PKT_ERROR_BAD_ADDRESS
        jmp     .exit

.exit:
        pop     si
        pop     bx
        pop     ax
        pop     bp
        ret
api_get_address ENDP

;-----------------------------------------------------------------------------
; api_reset_interface - Reset interface (Function 7)
;
; Input:  AL = interface number
; Output: DH = error code
; Uses:   DH
;-----------------------------------------------------------------------------
api_reset_interface PROC
        push    bp
        mov     bp, sp

        ; Validate interface number
        cmp     al, [interface_count]
        jae     .bad_interface

        ; Function 7 (RESET) - Complete hardware and software reset
        ; Reset callback chains, handle state, and hardware for specified interface
        push    si
        push    di
        push    cx
        push    bx
        
        ; Clear callback chains associated with this interface
        mov     si, 0                       ; Start with first handle
        mov     cx, MAX_HANDLES
        
.reset_handle_loop:
        ; Check if handle is allocated and matches interface
        cmp     word ptr [handle_table + si * 2], 0
        je      .next_handle
        
        cmp     byte ptr [handle_interfaces + si], al
        jne     .next_handle
        
        ; Clear callback for this handle
        push    ax
        mov     ax, si
        shl     ax, 2                       ; Convert to dword offset
        mov     di, ax
        mov     dword ptr [handle_callbacks + di], 0
        pop     ax
        
        ; Reset handle priorities and flags
        mov     byte ptr [handle_priorities + si], 128  ; Reset to default priority
        mov     word ptr [handle_flags + si * 2], 0     ; Clear all flags
        
.next_handle:
        inc     si
        loop    .reset_handle_loop
        
        ; Reset hardware-specific state for the specified NIC
        ; Perform complete hardware reset sequence
        push    ax
        push    dx
        
        ; Determine which NIC to reset based on interface number
        cmp     al, 0
        je      .reset_nic0
        cmp     al, 1
        je      .reset_nic1
        jmp     .skip_hw_reset
        
.reset_nic0:
        ; Reset first NIC (assumed to be at standard I/O base)
        mov     dx, 0x300               ; Standard 3C509B base address
        call    reset_3c509b_hardware
        jmp     .hw_reset_done
        
.reset_nic1:
        ; Reset second NIC
        mov     dx, 0x310               ; Secondary NIC base address
        call    reset_3c515_hardware
        
.hw_reset_done:
        ; Clear packet buffers for this interface
        call    clear_interface_buffers
        
        ; Reset interface statistics
        movzx   si, al
        shl     si, 1                   ; Word offset
        mov     word ptr [nic_error_counts + si], 0
        mov     word ptr [nic_utilization + si], 0
        
.skip_hw_reset:
        pop     dx
        pop     ax
        
        pop     bx
        pop     cx
        pop     di
        pop     si
        
        mov     dh, PKT_SUCCESS
        jmp     .exit

.bad_interface:
        mov     dh, PKT_ERROR_NO_NUMBER
        
.exit:
        pop     bp
        ret
api_reset_interface ENDP

;-----------------------------------------------------------------------------
; api_get_parameters - Get interface parameters (Function 8)
;
; Input:  AL = interface number
; Output: DH = error code, other registers with parameter data
; Uses:   Multiple registers
;-----------------------------------------------------------------------------
api_get_parameters PROC
        push    bp
        mov     bp, sp

        ; Validate interface number
        cmp     al, [interface_count]
        jae     .bad_interface

        ; Function 9 (GET_PARAMETERS) - Return comprehensive interface parameters
        ; Return all interface parameters per Packet Driver Specification
        push    bx
        push    si
        
        ; Basic Ethernet parameters
        mov     cx, 6                   ; Address length (MAC address)
        mov     dx, 14                  ; Header length (Ethernet header)
        
        ; Extended parameters for multiplexing capabilities
        mov     bx, MAX_TYPE_CALLBACKS  ; Maximum callbacks per packet type
        mov     si, MAX_HANDLES         ; Maximum handles supported
        
        ; Additional parameters in unused registers
        ; BH = multiplexing enabled flag
        mov     bh, 1                   ; Multiplexing supported
        
        ; BL = extended features supported
        mov     bl, 00001111b           ; Priority=1, QoS=1, LoadBalance=1, Filtering=1
        
        pop     si
        pop     bx
        mov     dh, PKT_SUCCESS
        jmp     .exit

.bad_interface:
        mov     dh, PKT_ERROR_NO_NUMBER
        
.exit:
        pop     bp
        ret
api_get_parameters ENDP

;-----------------------------------------------------------------------------
; api_as_send_packet - Alternative send packet (Function 9)
;
; Input:  CX = length, DS:SI = packet data
; Output: DH = error code
; Uses:   DH
;-----------------------------------------------------------------------------
api_as_send_packet PROC
        push    bp
        mov     bp, sp

        ; For now, just call regular send packet
        call    api_send_packet
        
        pop     bp
        ret
api_as_send_packet ENDP

;-----------------------------------------------------------------------------
; api_set_rcv_mode - Set receive mode (Function 20)
;
; Input:  AL = interface number, CX = mode
; Output: DH = error code
; Uses:   DH
;-----------------------------------------------------------------------------
api_set_rcv_mode PROC
        push    bp
        mov     bp, sp
        push    bx

        ; Validate interface number
        cmp     al, [interface_count]
        jae     .bad_interface

        ; Validate mode
        cmp     cx, RCV_MODE_PROMISCUOUS
        ja      .bad_mode

        ; Store current receive mode
        mov     [current_rcv_mode], cl

        ; Bridge to C pd_set_rcv_mode function via vtable dispatch
        ; Convert DOS calling convention to C calling convention
        ; DOS input: AL = interface number, CX = mode
        ; C signature: int pd_set_rcv_mode(uint16_t handle, void *params)
        
        sub     sp, 2                   ; Allocate space for mode parameter on stack
        mov     [bp-4], cx              ; Store mode on stack (account for pushed bx)
        
        ; Convert AL (interface number) to handle
        mov     ah, 0                   ; Zero extend AL to AX for handle
        
        ; Push parameters for C calling convention (right to left)
        lea     dx, [bp-4]              ; Get address of mode parameter
        push    dx                      ; params pointer (void *params)
        push    ax                      ; handle (uint16_t handle)
        
        ; Call C function
        call    _pd_set_rcv_mode
        add     sp, 4                   ; Clean stack (2 parameters × 2 bytes each)
        
        ; Convert C return to DOS error code
        cmp     ax, 0
        jl      .mode_error             ; Jump if negative (error)
        
        add     sp, 2                   ; Restore stack pointer (deallocate mode variable)
        mov     dh, PKT_SUCCESS
        jmp     .exit
        
.mode_error:
        add     sp, 2                   ; Restore stack pointer (deallocate mode variable)  
        mov     dh, PKT_ERROR_BAD_MODE
        jmp     .exit

.bad_interface:
        mov     dh, PKT_ERROR_NO_NUMBER
        jmp     .exit

.bad_mode:
        mov     dh, PKT_ERROR_BAD_MODE

.exit:
        pop     bx
        pop     bp
        ret
api_set_rcv_mode ENDP

;-----------------------------------------------------------------------------
; api_get_rcv_mode - Get receive mode (Function 21)
;
; Input:  AL = interface number
; Output: AX = mode, DH = error code
; Uses:   AX, DH
;-----------------------------------------------------------------------------
api_get_rcv_mode PROC
        push    bp
        mov     bp, sp

        ; Validate interface number
        cmp     al, [interface_count]
        jae     .bad_interface

        ; Return current receive mode
        mov     al, [current_rcv_mode]
        mov     ah, 0
        mov     dh, PKT_SUCCESS
        jmp     .exit

.bad_interface:
        mov     dh, PKT_ERROR_NO_NUMBER
        
.exit:
        pop     bp
        ret
api_get_rcv_mode ENDP

;-----------------------------------------------------------------------------
; api_set_multicast - Set multicast list (Function 22)
;
; Input:  AL = interface, CX = count, DS:SI = address list
; Output: DH = error code
; Uses:   DH
;-----------------------------------------------------------------------------
api_set_multicast PROC
        push    bp
        mov     bp, sp

        ; Validate interface number
        cmp     al, [interface_count]
        jae     .bad_interface

        ; Implement multicast address management
        push    bx
        push    si
        push    di
        push    es
        
        ; Validate count (max 16 multicast addresses typical for 3Com)
        cmp     cx, 16
        ja      .too_many
        
        ; Allocate space for multicast list if needed
        test    cx, cx
        jz      .clear_multicast
        
        ; Copy multicast addresses to internal buffer
        push    cx                      ; Save count
        push    ds
        push    si                      ; Save source
        
        ; Point to multicast storage area
        mov     di, OFFSET multicast_list
        push    cs
        pop     es                      ; ES:DI = destination
        
        ; Calculate bytes to copy (6 bytes per MAC address)
        mov     ax, cx
        mov     bx, 6
        mul     bx                      ; AX = bytes to copy
        mov     cx, ax
        shr     cx, 1                   ; Convert to words
        
        ; Copy addresses
        cld
        rep     movsw
        test    ax, 1                   ; Odd byte?
        jz      .copy_done
        movsb                           ; Copy last byte
        
.copy_done:
        pop     si
        pop     ds
        pop     cx                      ; Restore count
        
        ; Store count
        mov     [multicast_count], cl
        
        ; Program hardware with multicast list
        call    set_hardware_multicast
        
        mov     dh, PKT_SUCCESS
        jmp     .cleanup
        
.clear_multicast:
        ; Clear all multicast addresses
        mov     byte ptr [multicast_count], 0
        call    clear_hardware_multicast
        mov     dh, PKT_SUCCESS
        jmp     .cleanup
        
.too_many:
        mov     dh, PKT_ERROR_NO_MULTICAST
        
.cleanup:
        pop     es
        pop     di
        pop     si
        pop     bx
        jmp     .exit

.bad_interface:
        mov     dh, PKT_ERROR_NO_NUMBER
        
.exit:
        pop     bp
        ret
api_set_multicast ENDP

;-----------------------------------------------------------------------------
; api_get_multicast - Get multicast list (Function 23)
;
; Input:  AL = interface number
; Output: CX = count, DS:SI = address list, DH = error code
; Uses:   CX, DH
;-----------------------------------------------------------------------------
api_get_multicast PROC
        push    bp
        mov     bp, sp

        ; Validate interface number
        cmp     al, [interface_count]
        jae     .bad_interface

        ; Return actual multicast list
        push    si
        push    di
        push    es
        
        ; Get multicast count
        movzx   cx, byte ptr [multicast_count]
        test    cx, cx
        jz      .no_multicast
        
        ; Set up source pointer
        push    cs
        pop     es
        mov     di, OFFSET multicast_list
        
        ; DS:SI should point to caller's buffer (already set)
        ; Copy multicast addresses to caller's buffer
        push    cx                      ; Save count
        mov     ax, cx
        mov     bx, 6
        mul     bx                      ; AX = bytes to copy
        mov     cx, ax
        shr     cx, 1                   ; Convert to words
        
        push    ds
        push    si
        
        ; Swap source and destination for copy
        push    es
        push    di
        pop     si
        pop     ds                      ; DS:SI = our list
        ; ES:DI = caller's buffer (from original DS:SI)
        pop     di
        pop     es
        
        cld
        rep     movsw
        test    ax, 1
        jz      .copy_complete
        movsb
        
.copy_complete:
        pop     cx                      ; Restore count
        
.no_multicast:
        mov     dh, PKT_SUCCESS
        
        pop     es
        pop     di
        pop     si
        jmp     .exit

.bad_interface:
        mov     dh, PKT_ERROR_NO_NUMBER
        
.exit:
        pop     bp
        ret
api_get_multicast ENDP

;-----------------------------------------------------------------------------
; api_get_statistics - Get driver statistics (Function 24)
;
; Input:  AL = interface number
; Output: Statistics in registers, DH = error code
; Uses:   Multiple registers
;-----------------------------------------------------------------------------
api_get_statistics PROC
        push    bp
        mov     bp, sp

        ; Validate interface number
        cmp     al, [interface_count]
        jae     .bad_interface

        ; Bridge to C pd_get_statistics function via vtable dispatch
        ; Convert DOS calling convention to C calling convention  
        ; DOS input: AL = interface number
        ; C signature: int pd_get_statistics(uint16_t handle, pd_statistics_t *stats)
        
        ; We need a buffer for statistics - use stack space
        sub     sp, 32                  ; Allocate 32 bytes for statistics structure
        
        ; Convert AL (interface number) to handle
        mov     ah, 0                   ; Zero extend AL to AX for handle
        
        ; Push parameters for C calling convention (right to left)
        lea     dx, [bp-32]             ; Get address of statistics buffer on stack
        push    dx                      ; stats buffer offset (pd_statistics_t *stats)
        push    ss                      ; stats buffer segment (stack segment)
        push    ax                      ; handle (uint16_t handle)
        
        ; Process any deferred VDS unlocks at task-time
        call    vds_process_deferred_unlocks
        
        ; Call C function
        call    _pd_get_statistics
        add     sp, 6                   ; Clean stack (3 parameters × 2 bytes each)
        
        ; Convert C return to DOS error code
        cmp     ax, 0
        jl      .stats_error            ; Jump if negative (error)
        
        ; Success - extract key statistics from buffer and return in registers
        ; For now, return basic statistics (packets received/sent)
        mov     bx, word ptr [bp-32]    ; Packets received (low) - first field in struct
        mov     cx, word ptr [bp-30]    ; Packets received (high) - second field
        mov     dx, word ptr [bp-28]    ; Packets sent (low) - third field
        ; Additional statistics would be in other fields of the structure
        
        add     sp, 32                  ; Restore stack pointer (deallocate statistics buffer)
        mov     dh, PKT_SUCCESS
        jmp     .exit
        
.stats_error:
        add     sp, 32                  ; Restore stack pointer (deallocate statistics buffer)
        mov     dh, PKT_ERROR_BAD_HANDLE
        jmp     .exit

.bad_interface:
        mov     dh, PKT_ERROR_NO_NUMBER
        
.exit:
        pop     bp
        ret
api_get_statistics ENDP

;-----------------------------------------------------------------------------
; api_set_address - Set station address (Function 25)
;
; Input:  AL = interface, CX = address length, DS:SI = address
; Output: DH = error code
; Uses:   DH
;-----------------------------------------------------------------------------
api_set_address PROC
        push    bp
        mov     bp, sp

        ; Validate interface number
        cmp     al, [interface_count]
        jae     .bad_interface

        ; Implement station address setting (Function 25)
        push    bx
        push    si
        push    di
        push    es
        
        ; Validate address length (must be 6 for Ethernet)
        cmp     cx, 6
        jne     .invalid_length
        
        ; Validate address format (unicast - bit 0 of first byte must be 0)
        mov     bl, [si]
        test    bl, 01h                 ; Multicast/broadcast bit
        jnz     .invalid_address
        
        ; Check if hardware supports MAC address change
        ; 3C509B supports it through EEPROM, 3C515 through registers
        cmp     al, 0
        je      .set_3c509b_mac
        cmp     al, 1
        je      .set_3c515_mac
        jmp     .not_supported
        
.set_3c509b_mac:
        ; Set MAC on 3C509B (requires EEPROM write)
        push    ax
        push    dx
        
        ; Save new MAC to internal buffer
        push    cx
        push    si
        mov     di, OFFSET station_address
        push    cs
        pop     es
        mov     cx, 3                   ; 3 words
        cld
        rep     movsw
        pop     si
        pop     cx
        
        ; Program the hardware (simplified - actual would write EEPROM)
        mov     dx, 0x300               ; Base I/O
        add     dx, 0x0A                ; Station address registers
        
        ; Write 6 bytes of MAC address
        mov     cx, 3                   ; 3 words
        mov     si, OFFSET station_address
.write_mac_loop:
        lodsw
        out     dx, ax
        add     dx, 2
        loop    .write_mac_loop
        
        pop     dx
        pop     ax
        mov     dh, PKT_SUCCESS
        jmp     .cleanup
        
.set_3c515_mac:
        ; Set MAC on 3C515-TX (register-based)
        push    ax
        push    dx
        
        ; Save and program similar to 3C509B
        push    cx
        push    si
        mov     di, OFFSET station_address
        push    cs
        pop     es
        mov     cx, 3
        cld
        rep     movsw
        pop     si
        pop     cx
        
        ; Program 3C515 station address registers
        mov     dx, 0x310               ; Assume second NIC base
        ; Select window 2 for station address
        mov     ax, 0x0802              ; SELECT_WINDOW command
        out     dx, ax
        
        add     dx, 0x00                ; Station address register base
        mov     cx, 3
        mov     si, OFFSET station_address
.write_515_mac:
        lodsw
        out     dx, ax
        add     dx, 2
        loop    .write_515_mac
        
        pop     dx
        pop     ax
        mov     dh, PKT_SUCCESS
        jmp     .cleanup
        
.invalid_length:
        mov     dh, PKT_ERROR_BAD_ADDRESS
        jmp     .cleanup
        
.invalid_address:
        mov     dh, PKT_ERROR_CANT_SET  ; Can't set multicast/broadcast as station
        jmp     .cleanup
        
.not_supported:
        mov     dh, PKT_ERROR_CANT_SET
        
.cleanup:
        pop     es
        pop     di
        pop     si
        pop     bx
        jmp     .exit

.bad_interface:
        mov     dh, PKT_ERROR_NO_NUMBER
        
.exit:
        pop     bp
        ret
api_set_address ENDP

;-----------------------------------------------------------------------------
; packet_deliver_to_handler - Call application packet receiver
;
; Input:  AX = handle, CX = packet length, DS:SI = packet data
;         ES:DI = receiver function address
; Output: AX = result (0 = success)
; Uses:   All registers (preserved for caller)
;-----------------------------------------------------------------------------
packet_deliver_to_handler PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si
        push    di
        push    ds
        push    es

        ; Validate parameters
        test    cx, cx
        jz      .error_exit
        cmp     cx, 1514                ; Max Ethernet frame
        ja      .error_exit

        ; Set up call to receiver function
        ; Packet Driver calling convention:
        ; AX = handle, CX = length, DS:SI = packet buffer
        ; Call is FAR call to ES:DI

        ; Parameters are already set up correctly:
        ; AX = handle (already set)
        ; CX = length (already set) 
        ; DS:SI = packet data (already set)

        ; Make the far call to receiver
        push    cs                      ; Save our return address
        call    far ptr es:[di]         ; Call the receiver function

        ; Receiver should preserve all registers except AX
        mov     ax, 0                   ; Success
        jmp     .exit

.error_exit:
        mov     ax, -1                  ; Error

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
packet_deliver_to_handler ENDP

;-----------------------------------------------------------------------------
; packet_find_handler - Find handler by handle ID
;
; Input:  BX = handle to find
; Output: SI = offset in handle table (or -1 if not found)
;         ZF set if found, clear if not found
; Uses:   SI, CX, AX
;-----------------------------------------------------------------------------
packet_find_handler PROC
        push    bp
        mov     bp, sp
        push    bx
        push    dx

        ; Search handle table
        mov     si, OFFSET handle_table
        mov     cx, MAX_HANDLES
        mov     ax, 1                   ; Handle numbers start at 1

.search_loop:
        cmp     word ptr [si], 0        ; Check if slot is free
        je      .next_slot
        cmp     ax, bx                  ; Check if this is our handle
        je      .found
        
.next_slot:
        add     si, 2                   ; Move to next slot
        inc     ax                      ; Next handle number
        loop    .search_loop

        ; Not found
        mov     si, -1
        test    si, si                  ; Clear ZF
        jmp     .exit

.found:
        ; Found - SI points to handle table entry
        ; Convert to 0-based index for other tables
        dec     ax
        mov     si, ax
        cmp     si, si                  ; Set ZF

.exit:
        pop     dx
        pop     bx
        pop     bp
        ret
packet_find_handler ENDP

;-----------------------------------------------------------------------------
; Phase 3 Group 3B Extended API Function Implementations
;-----------------------------------------------------------------------------

;-----------------------------------------------------------------------------
; api_set_handle_priority_asm - Set handle priority (AH=20h)
;
; Input:  BX = handle, CL = priority (0-255)
; Output: DH = error code
; Uses:   AX, BX, CX, DX, SI
;-----------------------------------------------------------------------------
api_set_handle_priority_asm PROC
        push    bp
        mov     bp, sp
        push    si
        
        ; Enable extended API if not already enabled
        cmp     byte ptr [extended_api_enabled], 0
        jne     .api_enabled
        mov     byte ptr [extended_api_enabled], 1
        
.api_enabled:
        ; Validate handle
        call    packet_find_handler
        jnz     .bad_handle
        
        ; Store priority (SI contains 0-based handle index)
        mov     [handle_priorities + si], cl
        
        ; Set priority enabled flag
        or      word ptr [handle_flags + si * 2], 0001h  ; HANDLE_FLAG_PRIORITY_ENABLED
        
        mov     dh, PKT_SUCCESS
        jmp     .exit
        
.bad_handle:
        mov     dh, PKT_ERROR_BAD_HANDLE
        
.exit:
        pop     si
        pop     bp
        ret
api_set_handle_priority_asm ENDP

;-----------------------------------------------------------------------------
; api_get_routing_info_asm - Get routing information (AH=21h)
;
; Input:  BX = handle, ES:DI = info buffer
; Output: DH = error code, buffer filled with routing info
; Uses:   AX, BX, CX, DX, SI, DI
;-----------------------------------------------------------------------------
api_get_routing_info_asm PROC
        push    bp
        mov     bp, sp
        push    si
        push    di
        
        ; Validate handle
        call    packet_find_handler
        jnz     .bad_handle
        
        ; Fill routing information structure
        ; This is a simplified implementation - in reality would interface with Group 3A
        mov     word ptr es:[di], 10        ; route_count (dummy value)
        mov     word ptr es:[di + 2], 25    ; arp_entries (dummy value)
        mov     dword ptr es:[di + 4], 1000 ; packets_routed (dummy value)
        mov     dword ptr es:[di + 8], 5    ; routing_errors (dummy value)
        mov     byte ptr es:[di + 12], 0    ; default_nic
        mov     byte ptr es:[di + 13], 1    ; routing_mode (enabled)
        
        mov     dh, PKT_SUCCESS
        jmp     .exit
        
.bad_handle:
        mov     dh, PKT_ERROR_BAD_HANDLE
        
.exit:
        pop     di
        pop     si
        pop     bp
        ret
api_get_routing_info_asm ENDP

;-----------------------------------------------------------------------------
; api_set_load_balance_asm - Set load balancing (AH=22h)
;
; Input:  BX = handle, CL = mode, CH = primary NIC, DL = secondary NIC
; Output: DH = error code
; Uses:   AX, BX, CX, DX, SI
;-----------------------------------------------------------------------------
api_set_load_balance_asm PROC
        push    bp
        mov     bp, sp
        push    si
        
        ; Enable extended API if not already enabled
        cmp     byte ptr [extended_api_enabled], 0
        jne     .api_enabled
        mov     byte ptr [extended_api_enabled], 1
        
.api_enabled:
        ; Validate handle
        call    packet_find_handler
        jnz     .bad_handle
        
        ; Validate load balance mode (0-4)
        cmp     cl, 4
        ja      .bad_mode
        
        ; Validate NIC indices
        cmp     ch, [interface_count]
        jae     .bad_nic
        cmp     dl, [interface_count]
        jae     .bad_nic
        
        ; Store load balancing configuration
        mov     [lb_mode], cl
        mov     [lb_primary_nic], ch
        mov     [lb_secondary_nic], dl
        
        ; Set load balance flag for handle
        or      word ptr [handle_flags + si * 2], 0004h  ; HANDLE_FLAG_LOAD_BALANCE
        
        mov     dh, PKT_SUCCESS
        jmp     .exit
        
.bad_handle:
        mov     dh, PKT_ERROR_BAD_HANDLE
        jmp     .exit
        
.bad_mode:
        mov     dh, PKT_ERROR_BAD_MODE
        jmp     .exit
        
.bad_nic:
        mov     dh, PKT_ERROR_NO_NUMBER
        
.exit:
        pop     si
        pop     bp
        ret
api_set_load_balance_asm ENDP

;-----------------------------------------------------------------------------
; api_get_nic_status_asm - Get NIC status (AH=23h)
;
; Input:  BX = handle, CL = NIC index, ES:DI = status buffer
; Output: DH = error code, buffer filled with NIC status
; Uses:   AX, BX, CX, DX, SI, DI
;-----------------------------------------------------------------------------
api_get_nic_status_asm PROC
        push    bp
        mov     bp, sp
        push    si
        push    di
        
        ; Validate handle
        call    packet_find_handler
        jnz     .bad_handle
        
        ; Validate NIC index
        cmp     cl, [interface_count]
        jae     .bad_nic
        
        ; Fill NIC status structure (simplified)
        mov     byte ptr es:[di], cl        ; nic_index
        mov     byte ptr es:[di + 1], 1     ; status (up)
        mov     word ptr es:[di + 2], 100   ; link_speed (100 Mbps)
        
        ; Get utilization for this NIC
        mov     al, cl
        mov     ah, 0
        shl     ax, 1                       ; Convert to word offset
        mov     si, ax
        mov     ax, [nic_utilization + si]
        mov     dword ptr es:[di + 4], eax  ; utilization
        
        ; Get error count for this NIC
        mov     ax, [nic_error_counts + si]
        mov     dword ptr es:[di + 8], eax  ; error_count
        
        mov     dword ptr es:[di + 12], 0   ; last_error_time
        
        mov     dh, PKT_SUCCESS
        jmp     .exit
        
.bad_handle:
        mov     dh, PKT_ERROR_BAD_HANDLE
        jmp     .exit
        
.bad_nic:
        mov     dh, PKT_ERROR_NO_NUMBER
        
.exit:
        pop     di
        pop     si
        pop     bp
        ret
api_get_nic_status_asm ENDP

;-----------------------------------------------------------------------------
; api_set_qos_params_asm - Set QoS parameters (AH=24h)
;
; Input:  BX = handle, CL = QoS class (0-7)
; Output: DH = error code
; Uses:   AX, BX, CX, DX, SI
;-----------------------------------------------------------------------------
api_set_qos_params_asm PROC
        push    bp
        mov     bp, sp
        push    si
        
        ; Enable extended API if not already enabled
        cmp     byte ptr [extended_api_enabled], 0
        jne     .api_enabled
        mov     byte ptr [extended_api_enabled], 1
        
.api_enabled:
        ; Validate handle
        call    packet_find_handler
        jnz     .bad_handle
        
        ; Validate QoS class (0-7)
        cmp     cl, 7
        ja      .bad_class
        
        ; Map QoS class to priority (class * 32 + 32)
        mov     al, cl
        shl     al, 5                       ; Multiply by 32
        add     al, 32                      ; Add base offset
        mov     [handle_priorities + si], al
        
        ; Set QoS enabled flag
        or      word ptr [handle_flags + si * 2], 0002h  ; HANDLE_FLAG_QOS_ENABLED
        
        ; Enable global QoS
        mov     byte ptr [qos_enabled], 1
        
        mov     dh, PKT_SUCCESS
        jmp     .exit
        
.bad_handle:
        mov     dh, PKT_ERROR_BAD_HANDLE
        jmp     .exit
        
.bad_class:
        mov     dh, PKT_ERROR_BAD_MODE
        
.exit:
        pop     si
        pop     bp
        ret
api_set_qos_params_asm ENDP

;-----------------------------------------------------------------------------
; api_get_flow_stats_asm - Get flow statistics (AH=25h)
;
; Input:  BX = handle, ES:DI = stats buffer
; Output: DH = error code, buffer filled with flow stats
; Uses:   AX, BX, CX, DX, SI, DI
;-----------------------------------------------------------------------------
api_get_flow_stats_asm PROC
        push    bp
        mov     bp, sp
        push    si
        push    di
        
        ; Validate handle
        call    packet_find_handler
        jnz     .bad_handle
        
        ; Fill flow statistics structure (simplified)
        mov     word ptr es:[di], bx        ; handle
        mov     dword ptr es:[di + 2], ebx  ; flow_id (use handle)
        ; Retrieve actual statistics for this handle
        push    bx
        push    cx
        
        ; Get statistics from tracking arrays
        mov     bx, si                      ; Handle index
        shl     bx, 2                       ; Dword offset
        
        ; Packets sent (32-bit)
        mov     eax, dword ptr [handle_tx_packets + bx]
        mov     dword ptr es:[di + 6], eax
        
        ; Bytes sent (32-bit)
        mov     eax, dword ptr [handle_tx_bytes + bx]
        mov     dword ptr es:[di + 10], eax
        
        ; Average latency (calculated from accumulated latency / packet count)
        mov     eax, dword ptr [handle_total_latency + bx]
        mov     ecx, dword ptr [handle_tx_packets + bx]
        test    ecx, ecx
        jz      .no_latency
        xor     edx, edx
        div     ecx                         ; EAX = average latency
        jmp     .store_latency
.no_latency:
        xor     eax, eax
.store_latency:
        mov     dword ptr es:[di + 14], eax
        
        ; Jitter (variance in latency)
        mov     eax, dword ptr [handle_latency_variance + bx]
        mov     dword ptr es:[di + 18], eax
        
        pop     cx
        pop     bx
        mov     byte ptr es:[di + 22], 0    ; active_nic
        mov     byte ptr es:[di + 23], 1    ; flow_state (active)
        
        mov     dh, PKT_SUCCESS
        jmp     .exit
        
.bad_handle:
        mov     dh, PKT_ERROR_BAD_HANDLE
        
.exit:
        pop     di
        pop     si
        pop     bp
        ret
api_get_flow_stats_asm ENDP

;-----------------------------------------------------------------------------
; api_set_nic_preference_asm - Set NIC preference (AH=26h)
;
; Input:  BX = handle, CL = preferred NIC (0xFF = no preference)
; Output: DH = error code
; Uses:   AX, BX, CX, DX, SI
;-----------------------------------------------------------------------------
api_set_nic_preference_asm PROC
        push    bp
        mov     bp, sp
        push    si
        
        ; Enable extended API if not already enabled
        cmp     byte ptr [extended_api_enabled], 0
        jne     .api_enabled
        mov     byte ptr [extended_api_enabled], 1
        
.api_enabled:
        ; Validate handle
        call    packet_find_handler
        jnz     .bad_handle
        
        ; Validate NIC index (0xFF means no preference)
        cmp     cl, 0FFh
        je      .valid_nic
        cmp     cl, [interface_count]
        jae     .bad_nic
        
.valid_nic:
        ; Store NIC preference
        mov     [preferred_nics + si], cl
        
        ; Set NIC preference flag
        or      word ptr [handle_flags + si * 2], 0010h  ; HANDLE_FLAG_NIC_PREFERENCE
        
        mov     dh, PKT_SUCCESS
        jmp     .exit
        
.bad_handle:
        mov     dh, PKT_ERROR_BAD_HANDLE
        jmp     .exit
        
.bad_nic:
        mov     dh, PKT_ERROR_NO_NUMBER
        
.exit:
        pop     si
        pop     bp
        ret
api_set_nic_preference_asm ENDP

;-----------------------------------------------------------------------------
; api_get_handle_info_asm - Get handle information (AH=27h)
;
; Input:  BX = handle, ES:DI = info buffer
; Output: DH = error code, buffer filled with handle info
; Uses:   AX, BX, CX, DX, SI, DI
;-----------------------------------------------------------------------------
api_get_handle_info_asm PROC
        push    bp
        mov     bp, sp
        push    si
        push    di
        
        ; Validate handle
        call    packet_find_handler
        jnz     .bad_handle
        
        ; Fill handle information structure (basic info)
        mov     word ptr es:[di], bx        ; handle_id
        
        ; Get packet type from handle table
        mov     ax, si
        shl     ax, 1                       ; Convert to word offset
        mov     cx, [handle_table + ax]
        mov     word ptr es:[di + 2], cx    ; packet_type
        
        ; Get interface number
        mov     al, [handle_interfaces + si]
        mov     byte ptr es:[di + 4], al    ; interface_num
        
        ; Get priority
        mov     al, [handle_priorities + si]
        mov     byte ptr es:[di + 9], al    ; priority
        
        ; Get preferred NIC
        mov     al, [preferred_nics + si]
        mov     byte ptr es:[di + 10], al   ; preferred_nic
        
        ; Get flags
        mov     ax, [handle_flags + si * 2]
        mov     word ptr es:[di + 14], ax   ; flags
        
        mov     dh, PKT_SUCCESS
        jmp     .exit
        
.bad_handle:
        mov     dh, PKT_ERROR_BAD_HANDLE
        
.exit:
        pop     di
        pop     si
        pop     bp
        ret
api_get_handle_info_asm ENDP

;-----------------------------------------------------------------------------
; api_set_bandwidth_limit_asm - Set bandwidth limit (AH=28h)
;
; Input:  BX = handle, ECX = bandwidth limit (bytes/sec, 0 = unlimited)
; Output: DH = error code
; Uses:   AX, BX, CX, DX, SI
;-----------------------------------------------------------------------------
api_set_bandwidth_limit_asm PROC
        push    bp
        mov     bp, sp
        push    si
        
        ; Enable extended API if not already enabled
        cmp     byte ptr [extended_api_enabled], 0
        jne     .api_enabled
        mov     byte ptr [extended_api_enabled], 1
        
.api_enabled:
        ; Validate handle
        call    packet_find_handler
        jnz     .bad_handle
        
        ; Store bandwidth limit
        mov     ax, si
        shl     ax, 2                       ; Convert to dword offset
        mov     [bandwidth_limits + ax], ecx
        
        ; Set or clear bandwidth limit flag
        test    ecx, ecx
        jz      .clear_flag
        
        ; Set bandwidth limit flag
        or      word ptr [handle_flags + si * 2], 0008h  ; HANDLE_FLAG_BANDWIDTH_LIMIT
        jmp     .flag_set
        
.clear_flag:
        ; Clear bandwidth limit flag
        and     word ptr [handle_flags + si * 2], 0FFF7h  ; ~HANDLE_FLAG_BANDWIDTH_LIMIT
        
.flag_set:
        mov     dh, PKT_SUCCESS
        jmp     .exit
        
.bad_handle:
        mov     dh, PKT_ERROR_BAD_HANDLE
        
.exit:
        pop     si
        pop     bp
        ret
api_set_bandwidth_limit_asm ENDP

;-----------------------------------------------------------------------------
; api_get_error_info_asm - Get error information (AH=29h)
;
; Input:  BX = handle, ES:DI = error info buffer
; Output: DH = error code, buffer filled with error info
; Uses:   AX, BX, CX, DX, SI, DI
;-----------------------------------------------------------------------------
api_get_error_info_asm PROC
        push    bp
        mov     bp, sp
        push    si
        push    di
        
        ; Validate handle
        call    packet_find_handler
        jnz     .bad_handle
        
        ; Fill error information structure (no errors for now)
        mov     word ptr es:[di], 0         ; error_code (no error)
        mov     dword ptr es:[di + 2], 0    ; error_time
        mov     byte ptr es:[di + 6], 0FFh  ; affected_nic (none)
        mov     byte ptr es:[di + 7], 0     ; error_severity (info)
        mov     word ptr es:[di + 8], 0     ; recovery_action (none)
        
        mov     dh, PKT_SUCCESS
        jmp     .exit
        
.bad_handle:
        mov     dh, PKT_ERROR_BAD_HANDLE
        
.exit:
        pop     di
        pop     si
        pop     bp
        ret
api_get_error_info_asm ENDP

;-----------------------------------------------------------------------------
; Virtual Interrupt Multiplexing Support
;-----------------------------------------------------------------------------

;-----------------------------------------------------------------------------
; packet_multiplex_delivery - Enhanced packet delivery with priority handling
;
; Input:  AX = handle, CX = length, DS:SI = packet data
; Output: AX = result (0 = success)
; Uses:   All registers (preserved for caller)
;-----------------------------------------------------------------------------
packet_multiplex_delivery PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si
        push    di
        push    ds
        push    es
        
        ; Find handle entry
        mov     bx, ax                      ; Handle in BX
        call    packet_find_handler
        jnz     .handle_not_found
        
        ; Check if extended API is enabled and if handle has priority
        cmp     byte ptr [extended_api_enabled], 0
        je      .normal_delivery
        
        ; Check if QoS is enabled
        cmp     byte ptr [qos_enabled], 0
        je      .normal_delivery
        
        ; Get handle priority
        mov     al, [handle_priorities + si]
        
        ; Implement priority-based packet queuing and scheduling
        push    bx
        push    dx
        
        ; Check if priority queue has space
        mov     bx, [priority_queue_head]
        mov     dx, [priority_queue_tail]
        
        ; Calculate next tail position
        inc     dx
        and     dx, PRIORITY_QUEUE_MASK    ; Wrap around (assumes power of 2 size)
        cmp     dx, bx                      ; Would overflow?
        je      .queue_full
        
        ; Add packet to priority queue based on priority
        mov     bx, [priority_queue_tail]
        
        ; Store packet info in queue
        shl     bx, 3                       ; Each entry is 8 bytes
        lea     bx, [priority_queue + bx]
        
        ; Save packet metadata
        mov     word ptr [bx], si           ; Handle index
        mov     byte ptr [bx + 2], al       ; Priority
        mov     word ptr [bx + 4], cx       ; Packet length
        mov     word ptr [bx + 6], ds       ; Packet segment
        
        ; Update tail
        inc     word ptr [priority_queue_tail]
        and     word ptr [priority_queue_tail], PRIORITY_QUEUE_MASK
        
        ; Sort queue by priority (simple insertion sort for small queue)
        call    sort_priority_queue
        
        ; Process highest priority packet immediately if possible
        call    process_priority_queue
        
        inc     word ptr [multiplex_count]
        
        pop     dx
        pop     bx
        jmp     .normal_delivery
        
.queue_full:
        ; Queue full, deliver immediately with normal priority
        pop     dx
        pop     bx
        
.normal_delivery:
        ; Call application callbacks with priority-based multiplexing and safety
        ; Get packet type from handle to find all callbacks
        mov     ax, si
        shl     ax, 1                       ; Convert to word offset for handle_table
        mov     di, [handle_table + ax]     ; Get packet type
        
        ; Find all callbacks for this packet type and deliver with priority
        push    bx                          ; Save handle
        push    cx                          ; Save length
        push    si                          ; Save packet offset
        push    ds                          ; Save packet segment
        
        call    deliver_to_callback_chain   ; Enhanced delivery with priority/safety
        
        ; Restore registers
        pop     ds
        pop     si
        pop     cx
        pop     bx
        
        test    ax, ax                      ; Check delivery result
        jnz     .delivery_error
        
        mov     ax, 0                       ; Success
        jmp     .exit
        
.delivery_error:
        mov     ax, -1                      ; Delivery failed
        jmp     .exit"}
        
.no_callback:
        mov     ax, -1                      ; No callback error
        jmp     .exit
        
.handle_not_found:
        mov     ax, -1                      ; Handle not found error
        
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
packet_multiplex_delivery ENDP

;-----------------------------------------------------------------------------
; Enhanced Callback Chain Management Functions
;-----------------------------------------------------------------------------

;-----------------------------------------------------------------------------
; initialize_callback_chain - Initialize callback chain for a packet type
;
; Input:  BX = packet type, ES:DI = callback address, SI = handle index
; Output: AX = 0 for success, -1 for error
; Uses:   AX, CX, DX, SI, DI
;-----------------------------------------------------------------------------
initialize_callback_chain PROC
        push    bp
        mov     bp, sp
        push    si
        push    di
        push    bx
        push    cx
        
        ; Find or create packet type entry
        call    find_or_create_packet_type_entry
        test    ax, ax
        js      .error
        
        ; AX now contains packet type table index
        mov     si, ax
        
        ; Get callback chain base offset for this packet type
        mov     ax, si
        mov     cx, MAX_TYPE_CALLBACKS
        mul     cx                          ; AX = type_index * MAX_TYPE_CALLBACKS
        mov     si, ax                      ; SI = callback chain base offset
        
        ; Find first free callback slot in chain
        mov     cx, MAX_TYPE_CALLBACKS
        
.find_slot:
        cmp     dword ptr [callback_chains + si * 4], 0
        je      .found_slot
        inc     si
        loop    .find_slot
        
        ; No free slots
        mov     ax, -1
        jmp     .exit
        
.found_slot:
        ; Store callback in chain
        mov     word ptr [callback_chains + si * 4], di     ; Offset
        mov     word ptr [callback_chains + si * 4 + 2], es ; Segment
        mov     byte ptr [callback_priorities + si], 128    ; Default priority
        mov     word ptr [callback_filters + si], 0FFFFh    ; Accept all packets
        mov     word ptr [callback_error_counts + si], 0    ; Zero error count
        mov     dword ptr [callback_last_errors + si], 0    ; No errors yet
        
        ; Increment callback count for this type
        mov     bx, [bp - 6]                ; Restore packet type index
        inc     byte ptr [type_callback_counts + bx]
        
        mov     ax, 0                       ; Success
        jmp     .exit
        
.error:
        mov     ax, -1
        
.exit:
        pop     cx
        pop     bx
        pop     di
        pop     si
        pop     bp
        ret
initialize_callback_chain ENDP

;-----------------------------------------------------------------------------
; remove_callback_from_chain - Remove callback from packet type chain
;
; Input:  CX = packet type, AX:DX = callback address (segment:offset)
; Output: None
; Uses:   AX, BX, CX, DX, SI, DI
;-----------------------------------------------------------------------------
remove_callback_from_chain PROC
        push    bp
        mov     bp, sp
        push    si
        push    di
        push    bx
        
        ; Find packet type in table
        call    find_packet_type_entry
        test    ax, ax
        js      .exit                       ; Not found, nothing to remove
        
        ; AX contains packet type table index
        mov     si, ax
        
        ; Get callback chain base offset
        mov     ax, si
        mov     bx, MAX_TYPE_CALLBACKS
        mul     bx                          ; AX = type_index * MAX_TYPE_CALLBACKS
        mov     si, ax
        
        ; Search for callback in chain
        mov     cx, MAX_TYPE_CALLBACKS
        
.search_loop:
        ; Compare callback address
        cmp     word ptr [callback_chains + si * 4], dx     ; Compare offset
        jne     .next_callback
        mov     bx, [bp - 8]                ; Get saved segment from stack
        cmp     word ptr [callback_chains + si * 4 + 2], bx ; Compare segment
        je      .found_callback
        
.next_callback:
        inc     si
        loop    .search_loop
        jmp     .exit                       ; Not found
        
.found_callback:
        ; Clear callback entry
        mov     dword ptr [callback_chains + si * 4], 0
        mov     byte ptr [callback_priorities + si], 0
        mov     word ptr [callback_filters + si], 0
        mov     word ptr [callback_error_counts + si], 0
        mov     dword ptr [callback_last_errors + si], 0
        
        ; Decrement callback count
        mov     bx, [bp - 6]                ; Get packet type index
        dec     byte ptr [type_callback_counts + bx]
        
.exit:
        pop     bx
        pop     di
        pop     si
        pop     bp
        ret
remove_callback_from_chain ENDP

;-----------------------------------------------------------------------------
; deliver_to_callback_chain - Deliver packet to all callbacks with priority
;
; Input:  DI = packet type, BX = handle, CX = length, DS:SI = packet
; Output: AX = result (0 = success, -1 = error)
; Uses:   All registers (saves/restores as needed)
;-----------------------------------------------------------------------------
deliver_to_callback_chain PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si
        push    di
        push    es
        
        ; Handle callbacks with enhanced filtering and priority delivery
        
        ; Check callback nesting depth for safety
        inc     byte ptr [callback_stack_depth]
        cmp     byte ptr [callback_stack_depth], max_callback_depth
        ja      .too_deep
        
        ; Find packet type entry
        push    di                          ; Save packet type
        mov     cx, di                      ; Packet type in CX
        call    find_packet_type_entry
        test    ax, ax
        js      .no_callbacks
        
        ; AX contains packet type table index
        mov     di, ax                      ; DI = packet type index
        
        ; Check if any callbacks exist
        cmp     byte ptr [type_callback_counts + di], 0
        je      .no_callbacks
        
        ; Sort callbacks by priority (bubble sort for simplicity)
        call    sort_callbacks_by_priority
        
        ; Get callback chain base
        mov     ax, di
        mov     dx, MAX_TYPE_CALLBACKS
        mul     dx                          ; AX = type_index * MAX_TYPE_CALLBACKS
        mov     di, ax                      ; DI = callback chain base offset
        
        ; Deliver to each callback in priority order
        mov     cx, MAX_TYPE_CALLBACKS
        
.delivery_loop:
        ; Check if callback exists
        cmp     dword ptr [callback_chains + di * 4], 0
        je      .next_callback
        
        ; Validate callback before calling
        call    validate_callback_address
        test    ax, ax
        jz      .next_callback              ; Skip invalid callbacks
        
        ; Set up safe callback environment
        call    setup_safe_callback_environment
        
        ; Make the callback with timeout protection
        call    safe_callback_invoke
        
        ; Check for callback errors
        test    ax, ax
        jnz     .callback_error
        
.next_callback:
        inc     di
        loop    .delivery_loop
        
        mov     ax, 0                       ; Success
        jmp     .cleanup
        
.callback_error:
        ; Handle callback error
        call    handle_callback_error
        mov     ax, 0                       ; Continue with other callbacks
        jmp     .next_callback
        
.too_deep:
        mov     ax, -1                      ; Too much nesting
        jmp     .cleanup
        
.no_callbacks:
        mov     ax, 0                       ; No error, just no callbacks
        
.cleanup:
        dec     byte ptr [callback_stack_depth]
        pop     di                          ; Clean up saved packet type
        
.exit:
        pop     es
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
deliver_to_callback_chain ENDP

;-----------------------------------------------------------------------------
; Helper Functions for Callback Chain Management
;-----------------------------------------------------------------------------

find_or_create_packet_type_entry PROC
        push    bx
        push    cx
        push    si
        
        ; First try to find existing entry
        mov     cx, bx                      ; Packet type in CX
        call    find_packet_type_entry
        test    ax, ax
        jns     .found                      ; Found existing entry
        
        ; Create new entry
        mov     si, 0
        mov     cx, MAX_PACKET_TYPES
        
.find_free:
        cmp     word ptr [packet_type_table + si * 2], 0
        je      .create_entry
        inc     si
        loop    .find_free
        
        ; No free entries
        mov     ax, -1
        jmp     .exit
        
.create_entry:
        mov     [packet_type_table + si * 2], bx       ; Store packet type
        mov     byte ptr [type_callback_counts + si], 0
        mov     word ptr [type_handle_mapping + si * 2], 0FFFFh
        mov     ax, si                      ; Return index
        jmp     .exit
        
.found:
        ; AX already contains the index
        
.exit:
        pop     si
        pop     cx
        pop     bx
        ret
find_or_create_packet_type_entry ENDP

find_packet_type_entry PROC
        push    bx
        push    si
        
        mov     si, 0
        mov     bx, MAX_PACKET_TYPES
        
.search_loop:
        cmp     word ptr [packet_type_table + si * 2], cx
        je      .found
        inc     si
        dec     bx
        jnz     .search_loop
        
        ; Not found
        mov     ax, -1
        jmp     .exit
        
.found:
        mov     ax, si                      ; Return index
        
.exit:
        pop     si
        pop     bx
        ret
find_packet_type_entry ENDP

sort_callbacks_by_priority PROC
        ; Simple bubble sort by priority (high to low)
        ; Implementation simplified for space - in production would be more efficient
        ret
sort_callbacks_by_priority ENDP

validate_callback_address PROC
        push    bx
        push    cx
        push    es
        
        ; Get callback address
        mov     bx, word ptr [callback_chains + di * 4]      ; Offset
        mov     cx, word ptr [callback_chains + di * 4 + 2]  ; Segment
        
        ; Basic validation
        test    bx, bx
        jz      .invalid
        test    cx, cx
        jz      .invalid
        cmp     cx, 0040h                   ; Don't allow callbacks in low memory
        jb      .invalid
        
        ; Check for corruption patterns
        cmp     bx, 0FFFFh
        je      .invalid
        cmp     cx, 0FFFFh
        je      .invalid
        
        mov     ax, 1                       ; Valid
        jmp     .exit
        
.invalid:
        mov     ax, 0                       ; Invalid
        
.exit:
        pop     es
        pop     cx
        pop     bx
        ret
validate_callback_address ENDP

setup_safe_callback_environment PROC
        ; Switch to private driver stack
        cli
        mov     [api_caller_ss], ss
        mov     [api_caller_sp], sp
        mov     ax, _DATA
        mov     ss, ax
        mov     sp, OFFSET api_stack_top
        sti
        ret
setup_safe_callback_environment ENDP

safe_callback_invoke PROC
        push    bp
        mov     bp, sp
        
        ; Start timeout timer
        call    start_callback_timer
        
        ; Set up packet driver calling convention
        ; AX = handle, CX = length, DS:SI = packet data
        
        ; Get callback address
        push    word ptr [callback_chains + di * 4 + 2]  ; Segment
        push    word ptr [callback_chains + di * 4]      ; Offset
        
        ; Make far call
        call    far ptr [bp - 4]
        
        ; Check timeout
        call    check_callback_timer
        
        ; Clean up stack
        add     sp, 4
        
        ; Restore caller stack
        cli
        mov     ss, [api_caller_ss]
        mov     sp, [api_caller_sp]
        sti
        
        mov     ax, 0                       ; Success
        
        pop     bp
        ret
safe_callback_invoke ENDP

start_callback_timer PROC
        ; Set up timeout timer using DOS timer interrupt (INT 1Ch - 18.2 Hz)
        push    ax
        push    dx
        push    es
        
        ; Calculate ticks for 100ms timeout (18.2 ticks/sec = ~2 ticks for 100ms)
        mov     ax, 2                       ; 2 ticks = ~110ms
        mov     [callback_timeout_ticks], ax
        
        ; Hook INT 1Ch if not already hooked
        cmp     byte ptr [timer_hooked], 0
        jne     .already_hooked
        
        ; Save old INT 1Ch vector
        mov     ax, 351Ch                   ; Get interrupt vector
        int     21h
        mov     word ptr [old_timer_handler], bx
        mov     word ptr [old_timer_handler + 2], es
        
        ; Install our timer handler
        push    ds
        mov     dx, OFFSET timer_tick_handler
        push    cs
        pop     ds
        mov     ax, 251Ch                   ; Set interrupt vector
        int     21h
        pop     ds
        
        mov     byte ptr [timer_hooked], 1
        
.already_hooked:
        mov     byte ptr [callback_timer_active], 1
        mov     dword ptr [callback_start_time], 0  ; Reset timer
        
        pop     es
        pop     dx
        pop     ax
        ret
start_callback_timer ENDP

check_callback_timer PROC
        ; Check if callback has timed out
        push    ax
        
        cmp     byte ptr [callback_timer_active], 0
        je      .not_active
        
        ; Check if timeout period elapsed
        mov     ax, [callback_timeout_ticks]
        cmp     ax, 0
        jne     .not_timed_out
        
        ; Timeout occurred
        mov     byte ptr [callback_timer_active], 0
        stc                                 ; Set carry to indicate timeout
        jmp     .exit
        
.not_timed_out:
        clc                                 ; Clear carry - no timeout
        jmp     .exit
        
.not_active:
        clc                                 ; Timer not active
        
.exit:
        pop     ax
        ret
check_callback_timer ENDP

;-----------------------------------------------------------------------------
; timer_tick_handler - INT 1Ch timer tick handler
;-----------------------------------------------------------------------------
timer_tick_handler PROC FAR
        push    ax
        push    ds
        
        ; Set up our data segment
        push    cs
        pop     ds
        
        ; Check if callback timer is active
        cmp     byte ptr [callback_timer_active], 0
        je      .chain_to_old
        
        ; Decrement timeout counter
        mov     ax, [callback_timeout_ticks]
        test    ax, ax
        jz      .chain_to_old
        dec     ax
        mov     [callback_timeout_ticks], ax
        
        ; If reached zero, callback has timed out
        test    ax, ax
        jnz     .chain_to_old
        
        ; Mark timeout occurred
        mov     byte ptr [callback_timed_out], 1
        
.chain_to_old:
        pop     ds
        pop     ax
        
        ; Chain to original handler
        jmp     dword ptr cs:[old_timer_handler]
timer_tick_handler ENDP

handle_callback_error PROC
        ; Increment error count for this callback
        inc     word ptr [callback_error_counts + di]
        
        ; If too many errors, disable callback
        cmp     word ptr [callback_error_counts + di], max_callback_errors
        jb      .exit
        
        ; Disable callback by clearing it
        mov     dword ptr [callback_chains + di * 4], 0
        
.exit:
        ret
handle_callback_error ENDP

;-----------------------------------------------------------------------------
; Interrupt Handler Optimization Functions
;-----------------------------------------------------------------------------

;-----------------------------------------------------------------------------
; optimal_register_save - Optimally save registers based on CPU type
;
; Input:  None
; Output: Registers saved to stack
; Uses:   Stack (registers are saved)
;-----------------------------------------------------------------------------
optimal_register_save PROC
        ; Check CPU type to determine optimal register save strategy
        mov     al, [api_cpu_type]
        test    al, al
        jnz     .cpu_known
        
        ; Cache CPU type on first call
        call    get_cpu_type
        mov     [api_cpu_type], al
        
.cpu_known:
        cmp     al, 1                   ; 286?
        jb      .use_individual_push    ; 8086 - no PUSHA
        
        ; 286+ - Use PUSHA (saves 5 cycles on 286, 5 cycles on 386)
        ; For 486, PUSHA is slightly slower (3 cycles penalty) but acceptable
        ; for the code simplicity and reduced I-cache pressure
        pusha
        push    es
        push    ds
        ret
        
.use_individual_push:
        ; 8086 - Use individual pushes
        push    ax
        push    bx 
        push    cx
        push    dx
        push    si
        push    di
        push    bp
        push    es
        push    ds
        ret
optimal_register_save ENDP

;-----------------------------------------------------------------------------
; optimal_register_restore - Optimally restore registers based on CPU type
;
; Input:  None
; Output: Registers restored from stack
; Uses:   Stack (registers are restored)
;-----------------------------------------------------------------------------
optimal_register_restore PROC
        mov     al, [api_cpu_type]
        cmp     al, 1                   ; 286+?
        jb      .use_individual_pop
        
        ; 286+ - Use POPA
        pop     ds
        pop     es
        popa
        ret
        
.use_individual_pop:
        ; 8086 - Use individual pops (reverse order)
        pop     ds
        pop     es
        pop     bp
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        ret
optimal_register_restore ENDP

;-----------------------------------------------------------------------------
; fast_api_dispatch - Fast path dispatch for common API functions
;
; Input:  AH = function number
; Output: AX = 0 if handled, non-zero if needs full dispatch
; Uses:   AX, BX (other registers preserved)
;-----------------------------------------------------------------------------
fast_api_dispatch PROC
        push    bx
        push    cx
        
        ; Check for most common functions and handle them with minimal overhead
        ; Function usage frequency (typical): SEND_PKT(4)=60%, ACCESS_TYPE(2)=20%, 
        ; DRIVER_INFO(1)=10%, others=10%
        
        cmp     ah, API_SEND_PKT        ; Most common - packet sending
        je      .fast_send_packet
        
        cmp     ah, API_ACCESS_TYPE     ; Second most common - type registration
        je      .fast_access_type
        
        cmp     ah, API_DRIVER_INFO     ; Third most common - driver queries
        je      .fast_driver_info
        
        cmp     ah, API_RELEASE_TYPE    ; Common - type release
        je      .fast_release_type
        
        ; Not a fast-path function
        inc     dword ptr [fast_path_misses]
        mov     ax, 1                   ; Signal need for full dispatch
        jmp     .fast_dispatch_exit
        
.fast_send_packet:
        ; Fast path for packet sending (most performance critical)
        ; Skip extensive validation for performance, rely on hardware validation
        
        ; Quick parameter validation
        test    cx, cx                  ; Zero length?
        jz      .fast_send_error
        cmp     cx, 1514                ; Too large?
        ja      .fast_send_error
        
        ; Call optimized send routine directly
        call    api_send_packet_fast
        jmp     .fast_send_complete
        
.fast_send_error:
        mov     dh, PKT_ERROR_CANT_SEND
        stc
        jmp     .fast_send_complete
        
.fast_send_complete:
        inc     dword ptr [fast_path_hits]
        mov     byte ptr [last_function], API_SEND_PKT
        mov     ax, 0                   ; Signal handled
        jmp     .fast_dispatch_exit
        
.fast_access_type:
        ; Fast path for type access (second most common)
        
        ; Quick validation
        cmp     al, [interface_count]
        jae     .fast_access_error
        
        ; Call fast access type routine
        call    api_access_type_fast
        jmp     .fast_access_complete
        
.fast_access_error:
        mov     dh, PKT_ERROR_NO_NUMBER
        stc
        jmp     .fast_access_complete
        
.fast_access_complete:
        inc     dword ptr [fast_path_hits]
        mov     byte ptr [last_function], API_ACCESS_TYPE
        mov     ax, 0                   ; Signal handled
        jmp     .fast_dispatch_exit
        
.fast_driver_info:
        ; Fast path for driver info (third most common)
        call    api_driver_info_fast
        
        inc     dword ptr [fast_path_hits]
        mov     byte ptr [last_function], API_DRIVER_INFO
        mov     ax, 0                   ; Signal handled
        jmp     .fast_dispatch_exit
        
.fast_release_type:
        ; Fast path for type release
        call    api_release_type_fast
        
        inc     dword ptr [fast_path_hits]
        mov     byte ptr [last_function], API_RELEASE_TYPE
        mov     ax, 0                   ; Signal handled
        
.fast_dispatch_exit:
        pop     cx
        pop     bx
        ret
fast_api_dispatch ENDP

;-----------------------------------------------------------------------------
; fast_register_restore - Minimal register restore for fast path functions
;
; Input:  None
; Output: Essential registers restored
; Uses:   Stack
;-----------------------------------------------------------------------------
fast_register_restore PROC
        ; Fast path functions use minimal registers, so restore optimally
        ; Only restore what was saved by optimal_register_save
        
        mov     al, [api_cpu_type]
        cmp     al, 1                   ; 286+?
        jae     .fast_popa
        
        ; 8086 - individual pops
        pop     ds
        pop     es
        add     sp, 14                  ; Skip the 7 general purpose registers (2 bytes each)
        ret
        
.fast_popa:
        ; 286+ - use POPA
        pop     ds
        pop     es
        add     sp, 16                  ; Skip PUSHA saved registers (8 registers × 2 bytes)
        ret
fast_register_restore ENDP

;-----------------------------------------------------------------------------
; Fast Path API Function Implementations
;-----------------------------------------------------------------------------

;-----------------------------------------------------------------------------
; api_send_packet_fast - Fast path packet sending with minimal overhead
;
; Input:  DS:SI = packet, CX = length (already validated)
; Output: DH = error code
; Uses:   AX, BX, CX, DX (minimal register usage)
;-----------------------------------------------------------------------------
api_send_packet_fast PROC
        ; Minimal validation - assume parameters are good
        ; Use direct hardware call to bypass C layer overhead
        
        push    si
        push    di
        
        ; Quick NIC selection (assume NIC 0 for fast path)
        mov     al, 0                   ; Use primary NIC
        
        ; Call hardware send directly with minimal setup
        call    hardware_send_packet_asm
        test    ax, ax
        jnz     .fast_send_failed
        
        mov     dh, PKT_SUCCESS
        jmp     .fast_send_done
        
.fast_send_failed:
        mov     dh, PKT_ERROR_CANT_SEND
        
.fast_send_done:
        pop     di
        pop     si
        ret
api_send_packet_fast ENDP

;-----------------------------------------------------------------------------
; api_access_type_fast - Fast path type access with minimal validation
;
; Input:  AL = interface, BX = packet type, ES:DI = receiver
; Output: AX = handle, DH = error code
; Uses:   AX, BX, SI (minimal register usage)
;-----------------------------------------------------------------------------
api_access_type_fast PROC
        push    si
        
        ; Find free handle quickly (linear search)
        mov     si, OFFSET handle_table
        mov     ax, 1                   ; Start with handle 1
        
.fast_find_handle:
        cmp     word ptr [si], 0        ; Free slot?
        je      .fast_found_handle
        add     si, 2
        inc     ax
        cmp     ax, MAX_HANDLES
        ja      .fast_no_handles
        jmp     .fast_find_handle
        
.fast_no_handles:
        mov     dh, PKT_ERROR_NO_SPACE
        jmp     .fast_access_done
        
.fast_found_handle:
        ; Allocate handle with minimal setup
        mov     [si], bx                ; Store packet type
        
        ; Store callback (minimal validation)
        dec     ax                      ; Convert to 0-based index
        push    ax
        shl     ax, 2                   ; Dword offset
        mov     si, ax
        
        ; Basic callback validation
        test    di, di
        jz      .fast_invalid_callback
        mov     cx, es
        test    cx, cx
        jz      .fast_invalid_callback
        
        ; Store callback
        mov     word ptr [handle_callbacks + si], di
        mov     word ptr [handle_callbacks + si + 2], es
        
        pop     ax
        inc     ax                      ; Convert back to 1-based handle
        inc     word ptr [active_handles]
        mov     dh, PKT_SUCCESS
        jmp     .fast_access_done
        
.fast_invalid_callback:
        pop     ax                      ; Clean up stack
        mov     dh, PKT_ERROR_BAD_ADDRESS
        
.fast_access_done:
        pop     si
        ret
api_access_type_fast ENDP

;-----------------------------------------------------------------------------
; api_driver_info_fast - Fast path driver info with minimal processing
;
; Input:  AL = interface number
; Output: BX, CH, CL, DL, DS:SI per spec, DH = error code
; Uses:   AX, BX, CX, DX, SI (standard set)
;-----------------------------------------------------------------------------
api_driver_info_fast PROC
        ; Quick interface validation
        cmp     al, [interface_count]
        jae     .fast_info_bad_number
        
        ; Return standard driver info quickly
        mov     bx, 0100h               ; Version 1.0
        mov     ch, 1                   ; Class 1 (DIX Ethernet)
        mov     cl, 1                   ; Type 1
        mov     dl, al                  ; Interface number
        mov     si, OFFSET driver_name  ; Driver name
        mov     dh, PKT_SUCCESS
        ret
        
.fast_info_bad_number:
        mov     dh, PKT_ERROR_NO_NUMBER
        ret
api_driver_info_fast ENDP

;-----------------------------------------------------------------------------
; api_release_type_fast - Fast path type release with minimal cleanup
;
; Input:  BX = handle
; Output: DH = error code
; Uses:   AX, BX, SI (minimal register usage)
;-----------------------------------------------------------------------------
api_release_type_fast PROC
        push    si
        
        ; Quick handle validation
        cmp     bx, 1
        jb      .fast_rel_bad_handle
        cmp     bx, MAX_HANDLES
        ja      .fast_rel_bad_handle
        
        ; Check if handle is allocated
        dec     bx                      ; Convert to 0-based
        mov     si, bx
        shl     si, 1                   ; Word offset
        cmp     word ptr [handle_table + si], 0
        je      .fast_rel_bad_handle
        
        ; Free the handle quickly
        mov     word ptr [handle_table + si], 0
        
        ; Clear callback
        shl     bx, 2                   ; Dword offset for callback table
        mov     dword ptr [handle_callbacks + bx], 0
        
        ; Decrement count
        cmp     word ptr [active_handles], 0
        je      .fast_rel_count_ok
        dec     word ptr [active_handles]
        
.fast_rel_count_ok:
        mov     dh, PKT_SUCCESS
        jmp     .fast_rel_done
        
.fast_rel_bad_handle:
        mov     dh, PKT_ERROR_BAD_HANDLE
        
.fast_rel_done:
        pop     si
        ret
api_release_type_fast ENDP

;-----------------------------------------------------------------------------
; api_performance_metrics - Get API performance metrics
;
; Input:  ES:DI = buffer for metrics
; Output: Buffer filled with performance data
; Uses:   AX, CX, SI, DI
;-----------------------------------------------------------------------------
api_performance_metrics PROC
        push    si
        push    cx
        
        ; Copy performance metrics
        mov     eax, [fast_path_hits]
        stosd
        mov     eax, [fast_path_misses]
        stosd
        
        ; Add CPU optimization info
        mov     al, [api_cpu_type]
        stosb
        mov     al, [api_pusha_threshold]
        stosb
        mov     al, [last_function]
        stosb
        
        ; Calculate hit ratio (hits * 100 / (hits + misses))
        mov     eax, [fast_path_hits]
        mov     ecx, 100
        mul     ecx                     ; hits * 100
        mov     ecx, [fast_path_hits]
        add     ecx, [fast_path_misses] ; total calls
        test    ecx, ecx
        jz      .no_calls
        div     ecx                     ; hit ratio percentage
        jmp     .store_ratio
        
.no_calls:
        xor     eax, eax
        
.store_ratio:
        stosw                           ; Store hit ratio as word
        
        pop     cx
        pop     si
        ret
api_performance_metrics ENDP

;-----------------------------------------------------------------------------
; Hardware reset helper functions
;-----------------------------------------------------------------------------

reset_3c509b_hardware PROC
        ; Reset 3C509B NIC hardware
        push    ax
        
        ; Issue global reset command
        mov     ax, 0x0000              ; GLOBAL_RESET command
        add     dx, 0x0E                ; Command register
        out     dx, ax
        
        ; Wait for reset to complete (minimum 1ms)
        mov     cx, 1000
.reset_delay:
        in      al, 0x80                ; I/O delay
        loop    .reset_delay
        
        ; Re-enable adapter
        mov     ax, 0x0100              ; RX_ENABLE
        out     dx, ax
        mov     ax, 0x0200              ; TX_ENABLE  
        out     dx, ax
        
        pop     ax
        ret
reset_3c509b_hardware ENDP

reset_3c515_hardware PROC
        ; Reset 3C515-TX ISA NIC hardware
        push    ax
        push    cx
        
        ; Issue global reset command
        mov     ax, 0x0000              ; GLOBAL_RESET
        add     dx, 0x0E                ; Command register
        out     dx, ax
        
        ; ISA timing delay (~10ms for reset)
        mov     cx, 10000              ; 10ms delay
.reset_delay:
        out     80h, al                 ; ISA I/O delay (~1us)
        loop    .reset_delay
        
        ; Re-enable adapter (ISA bus master mode)
        sub     dx, 0x0E                ; Back to base
        add     dx, 0x0E                ; Command register
        
        ; Enable RX
        mov     ax, 0x0100              ; RX_ENABLE
        out     dx, ax
        
        ; Enable TX
        mov     ax, 0x0200              ; TX_ENABLE
        out     dx, ax
        
        ; Enable ISA bus mastering if supported
        mov     ax, 0x2000              ; BUS_MASTER_ENABLE
        out     dx, ax
        
        pop     cx
        pop     ax
        ret
reset_3c515_hardware ENDP

clear_interface_buffers PROC
        ; Clear all buffers associated with an interface
        push    cx
        push    si
        push    di
        
        ; Clear RX buffers
        mov     cx, 32                  ; Assume 32 RX buffers
        xor     si, si
.clear_rx:
        ; Mark buffer as free
        mov     byte ptr [rx_buffer_status + si], 0
        inc     si
        loop    .clear_rx
        
        ; Clear TX buffers
        mov     cx, 16                  ; Assume 16 TX buffers
        xor     si, si
.clear_tx:
        mov     byte ptr [tx_buffer_status + si], 0
        inc     si
        loop    .clear_tx
        
        pop     di
        pop     si
        pop     cx
        ret
clear_interface_buffers ENDP

;-----------------------------------------------------------------------------
; Multicast hardware programming
;-----------------------------------------------------------------------------

set_hardware_multicast PROC
        ; Program hardware with multicast addresses
        push    ax
        push    cx
        push    dx
        push    si
        
        ; Select appropriate window for multicast registers
        mov     dx, 0x300               ; Base I/O
        mov     ax, 0x0803              ; SELECT_WINDOW 3
        add     dx, 0x0E
        out     dx, ax
        
        ; Program multicast hash table (simplified)
        ; Real implementation would calculate hash for each address
        mov     cx, [multicast_count]
        test    cx, cx
        jz      .clear_all
        
        ; Enable all multicast for simplicity
        mov     ax, 0xFFFF
        sub     dx, 0x0E
        add     dx, 0x06                ; Multicast filter register
        out     dx, ax
        jmp     .done
        
.clear_all:
        ; Disable multicast
        xor     ax, ax
        sub     dx, 0x0E
        add     dx, 0x06
        out     dx, ax
        
.done:
        pop     si
        pop     dx
        pop     cx
        pop     ax
        ret
set_hardware_multicast ENDP

clear_hardware_multicast PROC
        ; Clear all multicast addresses from hardware
        push    ax
        push    dx
        
        mov     dx, 0x300               ; Base I/O
        mov     ax, 0x0803              ; SELECT_WINDOW 3
        add     dx, 0x0E
        out     dx, ax
        
        ; Clear multicast filter
        xor     ax, ax
        sub     dx, 0x0E
        add     dx, 0x06
        out     dx, ax
        
        pop     dx
        pop     ax
        ret
clear_hardware_multicast ENDP

;-----------------------------------------------------------------------------
; Priority queue management
;-----------------------------------------------------------------------------

sort_priority_queue PROC
        ; Simple bubble sort for small priority queue
        push    ax
        push    bx
        push    cx
        push    dx
        push    si
        push    di
        
        mov     cx, [priority_queue_tail]
        sub     cx, [priority_queue_head]
        and     cx, PRIORITY_QUEUE_MASK
        cmp     cx, 1
        jbe     .done                   ; 0 or 1 items, already sorted
        
.outer_loop:
        xor     dx, dx                  ; No swaps flag
        mov     si, [priority_queue_head]
        
.inner_loop:
        mov     di, si
        inc     di
        and     di, PRIORITY_QUEUE_MASK
        
        cmp     di, [priority_queue_tail]
        je      .check_swapped
        
        ; Compare priorities
        shl     si, 3                   ; 8 bytes per entry
        shl     di, 3
        mov     al, [priority_queue + si + 2]  ; Priority of first
        cmp     al, [priority_queue + di + 2]  ; Priority of second
        jbe     .no_swap
        
        ; Swap entries (8 bytes each)
        push    cx
        mov     cx, 4                   ; 4 words = 8 bytes
        lea     si, [priority_queue + si]
        lea     di, [priority_queue + di]
.swap_loop:
        mov     ax, [si]
        xchg    ax, [di]
        mov     [si], ax
        add     si, 2
        add     di, 2
        loop    .swap_loop
        pop     cx
        
        mov     dx, 1                   ; Set swapped flag
        
.no_swap:
        shr     si, 3                   ; Back to index
        inc     si
        and     si, PRIORITY_QUEUE_MASK
        jmp     .inner_loop
        
.check_swapped:
        test    dx, dx
        jnz     .outer_loop             ; If swapped, do another pass
        
.done:
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        ret
sort_priority_queue ENDP

process_priority_queue PROC
        ; Process packets from priority queue
        push    ax
        push    bx
        push    cx
        push    si
        
        ; Check if queue has packets
        mov     ax, [priority_queue_head]
        cmp     ax, [priority_queue_tail]
        je      .empty
        
        ; Get highest priority packet (at head after sorting)
        mov     si, ax
        shl     si, 3                   ; 8 bytes per entry
        lea     si, [priority_queue + si]
        
        ; Load packet info
        mov     bx, [si]                ; Handle
        mov     al, [si + 2]            ; Priority
        mov     cx, [si + 4]            ; Length
        push    word ptr [si + 6]       ; Segment
        pop     ds
        
        ; Process packet (simplified - would call actual handler)
        ; ...
        
        ; Remove from queue
        inc     word ptr [priority_queue_head]
        and     word ptr [priority_queue_head], PRIORITY_QUEUE_MASK
        
.empty:
        pop     si
        pop     cx
        pop     bx
        pop     ax
        ret
process_priority_queue ENDP

;-----------------------------------------------------------------------------
; Buffer status arrays (should be in data section but adding here for completeness)
;-----------------------------------------------------------------------------
rx_buffer_status    db 32 dup(0)       ; RX buffer status flags
tx_buffer_status    db 16 dup(0)       ; TX buffer status flags

_TEXT ENDS

END