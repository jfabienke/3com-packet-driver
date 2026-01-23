; @file flow_routing.asm
; @brief Fast flow table lookups, connection tracking
;
; 3Com Packet Driver - Support for 3C515-TX and 3C509B NICs
;
; This file is part of the 3Com Packet Driver project.
;

.MODEL SMALL
.386

; Include patch type definitions
include patch_macros.inc

; Flow table constants
MAX_FLOWS           EQU 256     ; Maximum number of concurrent flows
FLOW_TIMEOUT        EQU 300     ; Flow timeout in seconds (5 minutes)
FLOW_ENTRY_SIZE     EQU 32      ; Size of each flow entry in bytes

; Flow states
FLOW_STATE_FREE     EQU 0       ; Flow entry is free
FLOW_STATE_ACTIVE   EQU 1       ; Flow is active
FLOW_STATE_EXPIRED  EQU 2       ; Flow has expired

; Protocol types
PROTO_TCP           EQU 6       ; TCP protocol
PROTO_UDP           EQU 17      ; UDP protocol
PROTO_ICMP          EQU 1       ; ICMP protocol

; Hash table size (must be power of 2)
HASH_TABLE_SIZE     EQU 64      ; Hash table size
HASH_MASK           EQU 63      ; Hash mask (HASH_TABLE_SIZE - 1)

; Flow lookup result codes
FLOW_FOUND          EQU 0       ; Flow found
FLOW_NOT_FOUND      EQU 1       ; Flow not found
FLOW_TABLE_FULL     EQU 2       ; Flow table is full
FLOW_INVALID        EQU 3       ; Invalid flow parameters

; Data segment
_DATA SEGMENT
        ASSUME  DS:_DATA

; Flow table structure
; Each entry: src_ip(4) + dst_ip(4) + src_port(2) + dst_port(2) + protocol(1) + 
;            state(1) + timestamp(4) + nic_index(1) + reserved(13)
flow_table          db MAX_FLOWS * FLOW_ENTRY_SIZE dup(0)

; Hash table for fast lookups (stores flow table indices)
hash_table          dw HASH_TABLE_SIZE dup(0FFFFh)  ; 0xFFFF = empty

; Flow management
next_free_flow      dw 0        ; Next potentially free flow entry
active_flows        dw 0        ; Number of active flows
flow_lookups        dd 0        ; Total number of lookups
flow_hits          dd 0        ; Successful lookups
flow_misses        dd 0        ; Failed lookups

; Aging timer
last_aging_time     dd 0        ; Last time aging was performed

_DATA ENDS

; Code segment
_TEXT SEGMENT
        ASSUME  CS:_TEXT, DS:_DATA

; Public function exports
PUBLIC flow_routing_init
PUBLIC flow_lookup
PUBLIC flow_add
PUBLIC flow_remove
PUBLIC flow_age_entries
PUBLIC flow_get_stats
PUBLIC hash_calculate
PUBLIC flow_find_nic

; Export swap functions for SMC patching from C
PUBLIC swap_ip_dxax
PUBLIC swap_ip_eax
PUBLIC swap_ip_bswap

; External references
EXTRN get_cpu_features:PROC     ; From cpu_detect.asm

;-----------------------------------------------------------------------------
; flow_routing_init - Initialize flow-aware routing system
;
; Input:  None
; Output: AX = 0 for success, non-zero for error
; Uses:   AX, BX, CX, DX, SI
;-----------------------------------------------------------------------------
flow_routing_init PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si

        ; Initialize flow table and hash table
        ; Set up aging timer
        ; Clear statistics

        ; Clear flow table
        mov     cx, MAX_FLOWS * FLOW_ENTRY_SIZE
        mov     si, OFFSET flow_table
        xor     al, al
.clear_flow_table:
        mov     [si], al
        inc     si
        loop    .clear_flow_table

        ; Initialize hash table with empty markers
        mov     cx, HASH_TABLE_SIZE
        mov     si, OFFSET hash_table
        mov     ax, 0FFFFh              ; Empty marker
.clear_hash_table:
        mov     [si], ax
        add     si, 2
        loop    .clear_hash_table

        ; Initialize counters
        mov     word ptr [next_free_flow], 0
        mov     word ptr [active_flows], 0
        mov     dword ptr [flow_lookups], 0
        mov     dword ptr [flow_hits], 0
        mov     dword ptr [flow_misses], 0
        mov     dword ptr [last_aging_time], 0

        ; Success
        mov     ax, 0

        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
flow_routing_init ENDP

;-----------------------------------------------------------------------------
; flow_lookup - Look up flow entry for packet routing
;
; Input:  ES:DI = packet header (IP header), BX = packet length
; Output: AX = result code, CL = NIC index (if found)
; Uses:   AX, BX, CX, DX, SI, DI
;-----------------------------------------------------------------------------
flow_lookup PROC
        push    bp
        mov     bp, sp
        push    bx
        push    dx
        push    si
        push    di

        ; Extract flow key from packet
        ; Calculate hash
        ; Search hash table and flow entries
        ; Update statistics

        ; Increment lookup counter
        inc     dword ptr [flow_lookups]

        ; Extract IP addresses and ports from packet
        ; IP header format: version|IHL(1) + ToS(1) + length(2) + ID(2) + flags(2) + TTL(1) + proto(1) + checksum(2) + src_ip(4) + dst_ip(4)
        
        ; Get source IP (offset 12 in IP header) - 286 compatible
        mov     ax, es:[di+12]          ; Low word of IP
        mov     dx, es:[di+14]          ; High word of IP
PATCH_flow_src_ip:                      ; SMC patch point for endian conversion
        call    swap_ip_dxax           ; 3 bytes: E8 xx xx (will be patched)
        nop                             ; 2 bytes padding
        nop
        ; Store result (DX:AX on 286, EAX on 386+)
        mov     word ptr [flow_src_ip], ax
        mov     word ptr [flow_src_ip+2], dx
        
        ; Get destination IP (offset 16 in IP header) - 286 compatible
        mov     ax, es:[di+16]          ; Low word of IP
        mov     dx, es:[di+18]          ; High word of IP
PATCH_flow_dst_ip:                      ; SMC patch point for endian conversion
        call    swap_ip_dxax           ; 3 bytes: E8 xx xx (will be patched)
        nop                             ; 2 bytes padding
        nop
        ; Store result (DX:AX on 286, EAX on 386+)
        mov     word ptr [flow_dst_ip], ax
        mov     word ptr [flow_dst_ip+2], dx
        
        ; Get protocol (offset 9 in IP header)
        mov     al, es:[di+9]           ; Protocol
        mov     [flow_protocol], al
        
        ; Extract ports for TCP/UDP
        cmp     al, PROTO_TCP
        je      .extract_ports
        cmp     al, PROTO_UDP
        je      .extract_ports
        jmp     .no_ports

.extract_ports:
        ; TCP/UDP header starts after IP header
        ; Get IP header length (IHL field * 4)
        mov     al, es:[di]             ; Version|IHL
        and     al, 0Fh                 ; IHL only
        mov     cl, 2                   ; Multiply by 4 (shift left 2)
        shl     al, cl
        mov     bl, al                  ; IP header length
        
        ; Source port at offset 0 of TCP/UDP header
        mov     ax, es:[di+bx]          ; Source port
        xchg    al, ah                  ; Convert to little endian
        mov     [flow_src_port], ax
        
        ; Destination port at offset 2 of TCP/UDP header
        mov     ax, es:[di+bx+2]        ; Destination port
        xchg    al, ah                  ; Convert to little endian
        mov     [flow_dst_port], ax
        jmp     .search_flow

.no_ports:
        ; For ICMP and other protocols, use 0 for ports
        mov     word ptr [flow_src_port], 0
        mov     word ptr [flow_dst_port], 0

.search_flow:
        ; Calculate hash for the flow
        call    hash_calculate
        mov     bx, ax                  ; BX = hash value

        ; Look up in hash table
        shl     bx, 1                   ; Convert to word offset
        mov     si, OFFSET hash_table
        add     si, bx
        mov     dx, [si]                ; DX = flow table index

        ; Check if hash slot is empty
        cmp     dx, 0FFFFh
        je      .flow_not_found

        ; Validate flow entry
        call    flow_validate_entry
        cmp     ax, 0
        jne     .flow_not_found

        ; Flow found - get NIC index
        mov     si, OFFSET flow_table
        mov     ax, dx
        mov     cl, FLOW_ENTRY_SIZE
        mul     cl                      ; AX = offset to flow entry
        add     si, ax
        mov     cl, [si+25]             ; NIC index at offset 25

        ; Update hit statistics
        inc     dword ptr [flow_hits]
        
        mov     ax, FLOW_FOUND
        jmp     .exit

.flow_not_found:
        ; Update miss statistics
        inc     dword ptr [flow_misses]
        
        mov     ax, FLOW_NOT_FOUND
        mov     cl, 0                   ; Default NIC

.exit:
        pop     di
        pop     si
        pop     dx
        pop     bx
        pop     bp
        ret

; Local variables for flow key extraction
flow_src_ip         dd ?
flow_dst_ip         dd ?
flow_src_port       dw ?
flow_dst_port       dw ?
flow_protocol       db ?

flow_lookup ENDP

;-----------------------------------------------------------------------------
; flow_add - Add new flow entry
;
; Input:  ES:DI = packet header, AL = NIC index
; Output: AX = 0 for success, non-zero for error
; Uses:   All registers
;-----------------------------------------------------------------------------
flow_add PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si
        push    di

        ; Extract flow key from packet (reuse logic from flow_lookup)
        ; Find free flow entry
        ; Add to hash table
        ; Update statistics

        ; Check if flow table is full
        cmp     word ptr [active_flows], MAX_FLOWS
        jae     .table_full

        ; Find free flow entry
        call    flow_find_free_entry
        cmp     ax, 0FFFFh
        je      .table_full

        ; AX = free flow index
        mov     bx, ax

        ; Extract flow key (reuse logic from flow_lookup)
        ; Get source IP (offset 12 in IP header) - 286 compatible
        mov     ax, es:[di+12]          ; Low word of IP
        mov     dx, es:[di+14]          ; High word of IP
PATCH_flow_src_ip_add:                  ; SMC patch point for endian conversion
        call    swap_ip_dxax           ; 3 bytes: E8 xx xx (will be patched)
        nop                             ; 2 bytes padding
        nop
        ; Store result (DX:AX on 286, EAX on 386+)
        mov     word ptr [flow_src_ip], ax
        mov     word ptr [flow_src_ip+2], dx
        
        ; Get destination IP (offset 16 in IP header) - 286 compatible
        mov     ax, es:[di+16]          ; Low word of IP
        mov     dx, es:[di+18]          ; High word of IP
PATCH_flow_dst_ip_add:                  ; SMC patch point for endian conversion
        call    swap_ip_dxax           ; 3 bytes: E8 xx xx (will be patched)
        nop                             ; 2 bytes padding
        nop
        ; Store result (DX:AX on 286, EAX on 386+)
        mov     word ptr [flow_dst_ip], ax
        mov     word ptr [flow_dst_ip+2], dx
        
        ; Get protocol (offset 9 in IP header)
        mov     cl, es:[di+9]           ; Protocol
        mov     [flow_protocol], cl
        
        ; Extract ports for TCP/UDP
        cmp     cl, PROTO_TCP
        je      .extract_ports_add
        cmp     cl, PROTO_UDP
        je      .extract_ports_add
        jmp     .no_ports_add

.extract_ports_add:
        ; Get IP header length (IHL field * 4)
        mov     cl, es:[di]             ; Version|IHL
        and     cl, 0Fh                 ; IHL only
        shl     cl, 2                   ; Multiply by 4
        mov     bl, cl                  ; IP header length
        
        ; Source port
        mov     dx, es:[di+bx]          ; Source port
        xchg    dl, dh                  ; Convert to little endian
        mov     [flow_src_port], dx
        
        ; Destination port
        mov     dx, es:[di+bx+2]        ; Destination port
        xchg    dl, dh                  ; Convert to little endian
        mov     [flow_dst_port], dx
        jmp     .populate_entry

.no_ports_add:
        mov     word ptr [flow_src_port], 0
        mov     word ptr [flow_dst_port], 0

.populate_entry:
        ; Populate flow entry
        mov     si, OFFSET flow_table
        mov     dx, bx                  ; BX = free flow index
        mov     cx, FLOW_ENTRY_SIZE
        mul     cx                      ; AX = offset to flow entry
        add     si, ax
        
        ; Copy flow key to entry
        mov     eax, [flow_src_ip]
        mov     [si], eax               ; src_ip at offset 0
        mov     eax, [flow_dst_ip]
        mov     [si+4], eax             ; dst_ip at offset 4
        mov     dx, [flow_src_port]
        mov     [si+8], dx              ; src_port at offset 8
        mov     dx, [flow_dst_port]
        mov     [si+10], dx             ; dst_port at offset 10
        mov     cl, [flow_protocol]
        mov     [si+12], cl             ; protocol at offset 12
        
        ; Set flow state and metadata
        mov     byte ptr [si+21], FLOW_STATE_ACTIVE ; state at offset 21
        ; Set timestamp at offset 22-25 (use simple tick counter for now)
        push    eax
        call    get_system_ticks        ; Get system tick count
        mov     [si+22], eax            ; timestamp at offset 22-25
        pop     eax
        mov     [si+25], al             ; nic_index at offset 25 (from input AL)
        
        ; Add to hash table
        call    hash_calculate
        shl     ax, 1                   ; Convert to word offset
        mov     dx, OFFSET hash_table
        add     dx, ax
        mov     si, dx
        mov     [si], bx                ; Store flow index in hash table
        
        ; Update counters

        ; For now, just mark as successful
        inc     word ptr [active_flows]
        mov     ax, 0
        jmp     .exit

.table_full:
        mov     ax, FLOW_TABLE_FULL
        jmp     .exit

.exit:
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
flow_add ENDP

;-----------------------------------------------------------------------------
; flow_remove - Remove flow entry
;
; Input:  BX = flow table index
; Output: AX = 0 for success, non-zero for error
; Uses:   AX, BX, CX, DX, SI
;-----------------------------------------------------------------------------
flow_remove PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si

        ; Validate flow index
        ; Remove from hash table by finding and clearing the hash entry
        push    bx
        call    flow_remove_from_hash
        pop     bx
        ; Clear flow entry data
        call    flow_clear_entry_data
        ; Update statistics - decrement active flows handled above

        ; Validate flow index
        cmp     bx, MAX_FLOWS
        jae     .invalid_index

        ; Get flow entry
        mov     si, OFFSET flow_table
        mov     ax, bx
        mov     cl, FLOW_ENTRY_SIZE
        mul     cl
        add     si, ax

        ; Check if flow is active
        cmp     byte ptr [si+21], FLOW_STATE_ACTIVE  ; State at offset 21
        jne     .not_active

        ; Mark as free
        mov     byte ptr [si+21], FLOW_STATE_FREE

        ; Remove from hash table
        push    bx
        call    flow_remove_from_hash_by_index
        pop     bx
        ; Clear entry data
        call    flow_clear_entry_by_index

        ; Update counters
        dec     word ptr [active_flows]

        mov     ax, 0
        jmp     .exit

.invalid_index:
        mov     ax, FLOW_INVALID
        jmp     .exit

.not_active:
        mov     ax, FLOW_NOT_FOUND
        jmp     .exit

.exit:
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
flow_remove ENDP

;-----------------------------------------------------------------------------
; flow_age_entries - Age out expired flow entries
; Should be called periodically to remove old flows
;
; Input:  None
; Output: AX = number of entries aged out
; Uses:   All registers
;-----------------------------------------------------------------------------
flow_age_entries PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si

        ; Get current time (placeholder for now)
        ; Scan flow table for expired entries
        ; Remove expired flows
        ; Update aging timestamp

        mov     cx, 0                   ; Aged entries counter
        mov     bx, 0                   ; Flow index
        mov     si, OFFSET flow_table

.age_loop:
        ; Check if flow is active
        cmp     byte ptr [si+21], FLOW_STATE_ACTIVE
        jne     .next_flow

        ; Check if flow has expired using timestamp comparison
        push    eax
        call    get_system_ticks        ; Get current time
        mov     edx, eax                ; EDX = current time
        pop     eax
        
        ; Get flow timestamp (offset 22-25)
        mov     eax, [si+22]            ; Flow timestamp
        sub     edx, eax                ; Time difference
        cmp     edx, FLOW_TIMEOUT * 18  ; Convert seconds to ticks (18 ticks/sec)
        jb      .next_flow              ; Flow not expired
        
        ; Flow has expired - remove it
        push    bx
        call    flow_remove
        pop     bx
        inc     cx                      ; Increment aged counter

.next_flow:
        add     si, FLOW_ENTRY_SIZE
        inc     bx
        cmp     bx, MAX_FLOWS
        jb      .age_loop

        ; Update last aging time
        push    eax
        call    get_system_ticks
        mov     dword ptr [last_aging_time], eax
        pop     eax

        mov     ax, cx                  ; Return aged count

        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
flow_age_entries ENDP

;-----------------------------------------------------------------------------
; flow_get_stats - Get flow routing statistics
;
; Input:  ES:DI = buffer for statistics (24 bytes)
; Output: AX = 0 for success
; Uses:   AX, CX, SI, DI
;-----------------------------------------------------------------------------
flow_get_stats PROC
        push    bp
        mov     bp, sp
        push    cx
        push    si

        ; Copy statistics to buffer
        mov     si, OFFSET active_flows
        mov     cx, 12                  ; Copy 24 bytes (12 words)
        rep     movsw

        mov     ax, 0

        pop     si
        pop     cx
        pop     bp
        ret
flow_get_stats ENDP

;-----------------------------------------------------------------------------
; hash_calculate - Calculate hash value for flow key
; Uses currently extracted flow key variables
;
; Input:  Flow key in local variables
; Output: AX = hash value (0 to HASH_TABLE_SIZE-1)
; Uses:   AX, BX, CX, DX
;-----------------------------------------------------------------------------
hash_calculate PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx

        ; Implement hash function using all flow key components
        ; Use XOR and rotation for good distribution

        ; Simple hash using XOR of key components
        mov     eax, [flow_src_ip]
        xor     eax, [flow_dst_ip]
        mov     dx, [flow_src_port]
        xor     ax, dx
        mov     dx, [flow_dst_port]
        xor     ax, dx
        mov     dl, [flow_protocol]
        xor     al, dl

        ; Reduce to hash table size
        and     ax, HASH_MASK

        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
hash_calculate ENDP

;-----------------------------------------------------------------------------
; flow_find_free_entry - Find free entry in flow table
;
; Input:  None
; Output: AX = flow index if found, 0xFFFF if table full
; Uses:   AX, BX, CX, SI
;-----------------------------------------------------------------------------
flow_find_free_entry PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    si

        ; Start search from next_free_flow hint
        mov     bx, [next_free_flow]
        mov     cx, MAX_FLOWS
        mov     si, OFFSET flow_table

.search_loop:
        ; Calculate offset to flow entry
        push    ax
        mov     ax, bx
        mov     dl, FLOW_ENTRY_SIZE
        mul     dl
        add     si, ax
        pop     ax

        ; Check if entry is free
        cmp     byte ptr [si+21], FLOW_STATE_FREE  ; State at offset 21
        je      .found_free

        ; Move to next entry
        inc     bx
        cmp     bx, MAX_FLOWS
        jb      .no_wrap
        mov     bx, 0                   ; Wrap around
        mov     si, OFFSET flow_table

.no_wrap:
        loop    .search_loop

        ; Table is full
        mov     ax, 0FFFFh
        jmp     .exit

.found_free:
        mov     ax, bx                  ; Return flow index
        inc     bx                      ; Update hint
        cmp     bx, MAX_FLOWS
        jb      .no_wrap_hint
        mov     bx, 0

.no_wrap_hint:
        mov     [next_free_flow], bx

.exit:
        pop     si
        pop     cx
        pop     bx
        pop     bp
        ret
flow_find_free_entry ENDP

;-----------------------------------------------------------------------------
; flow_validate_entry - Validate flow entry matches current key
;
; Input:  DX = flow table index
; Output: AX = 0 if valid, non-zero if invalid
; Uses:   AX, BX, CX, SI
;-----------------------------------------------------------------------------
flow_validate_entry PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    si

        ; Compare flow entry with current flow key
        ; Check if entry is still active
        ; Validate timestamps

        ; Get flow entry
        mov     si, OFFSET flow_table
        mov     ax, dx
        mov     cl, FLOW_ENTRY_SIZE
        mul     cl
        add     si, ax

        ; Check if flow is active
        cmp     byte ptr [si+21], FLOW_STATE_ACTIVE
        jne     .invalid

        ; Compare flow key fields
        ; Compare source IP
        mov     eax, [flow_src_ip]
        cmp     eax, [si]
        jne     .invalid
        
        ; Compare destination IP
        mov     eax, [flow_dst_ip]
        cmp     eax, [si+4]
        jne     .invalid
        
        ; Compare source port
        mov     dx, [flow_src_port]
        cmp     dx, [si+8]
        jne     .invalid
        
        ; Compare destination port
        mov     dx, [flow_dst_port]
        cmp     dx, [si+10]
        jne     .invalid
        
        ; Compare protocol
        mov     cl, [flow_protocol]
        cmp     cl, [si+12]
        jne     .invalid
        
        ; Flow matches
        mov     ax, 0
        jmp     .exit

.invalid:
        mov     ax, 1
        jmp     .exit

.exit:
        pop     si
        pop     cx
        pop     bx
        pop     bp
        ret
flow_validate_entry ENDP

;-----------------------------------------------------------------------------
; flow_find_nic - Determine NIC for packet based on flow routing
;
; Input:  ES:DI = packet header
; Output: AL = NIC index, AH = routing method (0=static, 1=flow)
; Uses:   AX, BX, CX, DX
;-----------------------------------------------------------------------------
flow_find_nic PROC
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx

        ; Try flow-based routing first
        call    flow_lookup
        cmp     ax, FLOW_FOUND
        je      .flow_found

        ; Flow not found - use static routing or default
        ; Call static routing module (placeholder)
        mov     al, 0                   ; Default to NIC 0
        mov     ah, 0                   ; Static routing
        jmp     .exit

.flow_found:
        ; CL already contains NIC index from flow_lookup
        mov     al, cl
        mov     ah, 1                   ; Flow-based routing

.exit:
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
flow_find_nic ENDP

;-----------------------------------------------------------------------------
; flow_remove_from_hash - Remove flow entry from hash table
;
; Input:  BX = flow table index
; Output: None
; Uses:   AX, CX, DX, SI
;-----------------------------------------------------------------------------
flow_remove_from_hash PROC
        push    bp
        mov     bp, sp
        push    cx
        push    dx
        push    si
        
        ; Search hash table for this flow index
        mov     cx, HASH_TABLE_SIZE
        mov     si, OFFSET hash_table
        
.search_hash:
        cmp     [si], bx                ; Check if this slot contains our flow index
        jne     .next_slot
        
        ; Found the slot - clear it
        mov     word ptr [si], 0FFFFh   ; Empty marker
        jmp     .done
        
.next_slot:
        add     si, 2                   ; Next word
        loop    .search_hash
        
.done:
        pop     si
        pop     dx
        pop     cx
        pop     bp
        ret
flow_remove_from_hash ENDP

;-----------------------------------------------------------------------------
; flow_remove_from_hash_by_index - Remove flow by index from hash table
;
; Input:  BX = flow table index
; Output: None
; Uses:   AX, CX, DX, SI
;-----------------------------------------------------------------------------
flow_remove_from_hash_by_index PROC
        ; Same as flow_remove_from_hash for now
        call    flow_remove_from_hash
        ret
flow_remove_from_hash_by_index ENDP

;-----------------------------------------------------------------------------
; flow_clear_entry_data - Clear flow entry data
;
; Input:  SI = pointer to flow entry
; Output: None
; Uses:   AX, CX, DI
;-----------------------------------------------------------------------------
flow_clear_entry_data PROC
        push    bp
        mov     bp, sp
        push    cx
        push    di
        
        ; Clear the flow entry
        mov     di, si
        mov     cx, FLOW_ENTRY_SIZE
        xor     al, al
        rep     stosb
        
        pop     di
        pop     cx
        pop     bp
        ret
flow_clear_entry_data ENDP

;-----------------------------------------------------------------------------
; flow_clear_entry_by_index - Clear flow entry by index
;
; Input:  BX = flow table index
; Output: None
; Uses:   AX, CX, DX, SI, DI
;-----------------------------------------------------------------------------
flow_clear_entry_by_index PROC
        push    bp
        mov     bp, sp
        push    cx
        push    dx
        push    si
        push    di
        
        ; Calculate flow entry address
        mov     si, OFFSET flow_table
        mov     ax, bx
        mov     cl, FLOW_ENTRY_SIZE
        mul     cl
        add     si, ax
        
        ; Clear the entry
        call    flow_clear_entry_data
        
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bp
        ret
flow_clear_entry_by_index ENDP

;-----------------------------------------------------------------------------
; swap_ip_dxax - Swap IP address from network to host order (286 version)
;
; Input:  DX:AX = IP address in network byte order
; Output: DX:AX = IP address in host byte order
; Uses:   None (preserves all other registers)
;-----------------------------------------------------------------------------
swap_ip_dxax PROC NEAR
        xchg    al, ah                  ; Swap low word bytes
        xchg    dl, dh                  ; Swap high word bytes
        xchg    ax, dx                  ; Swap words
        ret
swap_ip_dxax ENDP

;-----------------------------------------------------------------------------
; swap_ip_eax - Swap IP address from network to host order (386 version)
;
; Input:  DX:AX = IP address in network byte order (high:low)
; Output: DX:AX = IP address in host byte order (high:low)
; Uses:   EAX internally, but preserves calling convention
;-----------------------------------------------------------------------------
swap_ip_eax PROC NEAR
        ; Combine DX:AX into EAX
        push    dx                      ; Push high word
        push    ax                      ; Push low word
        pop     eax                     ; Pop as 32-bit value
        
        ; Perform byteswap with rotates
        ror     ax, 8                   ; Swap low word
        ror     eax, 16                 ; Rotate halves
        ror     ax, 8                   ; Swap high word (now low)
        
        ; Split back to DX:AX
        push    eax                     ; Push 32-bit result
        pop     ax                      ; Pop low word
        pop     dx                      ; Pop high word
        ret
swap_ip_eax ENDP

;-----------------------------------------------------------------------------
; swap_ip_bswap - Swap IP using BSWAP instruction (486+ version)
;
; Input:  DX:AX = IP address in network byte order (high:low)
; Output: DX:AX = IP address in host byte order (high:low)
; Uses:   EAX internally, but preserves calling convention
;-----------------------------------------------------------------------------
swap_ip_bswap PROC NEAR
        ; Combine DX:AX into EAX
        push    dx                      ; Push high word
        push    ax                      ; Push low word
        pop     eax                     ; Pop as 32-bit value
        
        ; Perform BSWAP
        db      0Fh, 0C8h               ; BSWAP EAX
        
        ; Split back to DX:AX
        push    eax                     ; Push 32-bit result
        pop     ax                      ; Pop low word
        pop     dx                      ; Pop high word
        ret
swap_ip_bswap ENDP

;-----------------------------------------------------------------------------
; get_system_ticks - Get system tick count (placeholder)
;
; Input:  None
; Output: EAX = system tick count
; Uses:   EAX
;-----------------------------------------------------------------------------
get_system_ticks PROC
        push    bp
        mov     bp, sp
        
        ; Read system timer tick count from BIOS data area (0x40:0x6C)
        push    ds
        mov     ax, 0040h
        mov     ds, ax
        mov     eax, ds:[006Ch]         ; BIOS timer tick count
        pop     ds
        
        pop     bp
        ret
get_system_ticks ENDP

;-----------------------------------------------------------------------------
; SMC Patch Table for flow routing module
;-----------------------------------------------------------------------------
; This table defines the patch points that will be modified during init
; based on detected CPU capabilities (486+ gets BSWAP optimization)

; Note: The default CALL targets will be fixed up at initialization
; These are placeholders that the SMC patcher will replace
flow_routing_patch_table:
        ; All 4 patches follow same pattern for consistency
        ; The patcher will replace the CALL targets based on CPU type
        
        ; Patch 1: Source IP in flow_lookup
        dw      PATCH_flow_src_ip
        db      PATCH_TYPE_ENDIAN
        db      5
        db      0E8h, 00h, 00h, 90h, 90h ; 286: CALL swap_ip_dxax (offset TBD)
        db      0E8h, 00h, 00h, 90h, 90h ; 386: CALL swap_ip_eax (offset TBD)
        db      0E8h, 00h, 00h, 90h, 90h ; 486: CALL swap_ip_bswap (offset TBD)
        db      0E8h, 00h, 00h, 90h, 90h ; Pentium: CALL swap_ip_bswap (offset TBD)

        ; Patch 2: Destination IP in flow_lookup
        dw      PATCH_flow_dst_ip
        db      PATCH_TYPE_ENDIAN
        db      5
        db      0E8h, 00h, 00h, 90h, 90h ; 286: CALL swap_ip_dxax
        db      0E8h, 00h, 00h, 90h, 90h ; 386: CALL swap_ip_eax
        db      0E8h, 00h, 00h, 90h, 90h ; 486: CALL swap_ip_bswap
        db      0E8h, 00h, 00h, 90h, 90h ; Pentium: CALL swap_ip_bswap

        ; Patch 3: Source IP in flow_add
        dw      PATCH_flow_src_ip_add
        db      PATCH_TYPE_ENDIAN
        db      5
        db      0E8h, 00h, 00h, 90h, 90h ; 286: CALL swap_ip_dxax
        db      0E8h, 00h, 00h, 90h, 90h ; 386: CALL swap_ip_eax
        db      0E8h, 00h, 00h, 90h, 90h ; 486: CALL swap_ip_bswap
        db      0E8h, 00h, 00h, 90h, 90h ; Pentium: CALL swap_ip_bswap

        ; Patch 4: Destination IP in flow_add
        dw      PATCH_flow_dst_ip_add
        db      PATCH_TYPE_ENDIAN
        db      5
        db      0E8h, 00h, 00h, 90h, 90h ; 286: CALL swap_ip_dxax
        db      0E8h, 00h, 00h, 90h, 90h ; 386: CALL swap_ip_eax
        db      0E8h, 00h, 00h, 90h, 90h ; 486: CALL swap_ip_bswap
        db      0E8h, 00h, 00h, 90h, 90h ; Pentium: CALL swap_ip_bswap

flow_routing_patch_count equ 4

_TEXT ENDS

END