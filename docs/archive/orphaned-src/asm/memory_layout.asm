;
; @file memory_layout.asm
; @brief Optimized memory layout with hot/cold code separation
;
; Organizes code and data for optimal cache utilization by grouping
; frequently accessed (hot) code and data together, separated from
; rarely used (cold) initialization and error handling code.
;
; Based on DRIVER_TUNING.md specifications for memory optimization.
;

;-----------------------------------------------------------------------------
; HOT SECTION - Frequently accessed code and data
; This section is kept resident and optimized for cache locality
;-----------------------------------------------------------------------------
SEGMENT _HOT_TEXT PUBLIC CLASS=HOT_CODE USE16 ALIGN=16

; Export hot section symbols
PUBLIC _hot_section_start
PUBLIC _hot_section_end

_hot_section_start:

;-----------------------------------------------------------------------------
; HOT DATA - Frequently accessed variables
; Aligned for optimal cache line usage
;-----------------------------------------------------------------------------
ALIGN 16
PUBLIC _hot_data_start

_hot_data_start:
        ; Critical I/O bases for each NIC (accessed on every packet)
        PUBLIC _io_bases
        _io_bases       DW 4 DUP(0)     ; I/O base addresses
        
        ; Precomputed port addresses (GPT-5 optimization)
        PUBLIC _cmd_ports
        PUBLIC _data_ports  
        PUBLIC _status_ports
        _cmd_ports      DW 4 DUP(0)     ; Command ports (base + 0Eh)
        _data_ports     DW 4 DUP(0)     ; Data ports (base + 00h)
        _status_ports   DW 4 DUP(0)     ; Status ports (base + 0Eh)
        
        ; Current window cache (performance optimization)
        PUBLIC _current_windows
        _current_windows DB 4 DUP(0FFh) ; Current window per NIC (0xFF = invalid)
        
        ; TX/RX state (accessed frequently)
        PUBLIC _tx_heads
        PUBLIC _tx_tails
        PUBLIC _rx_heads
        PUBLIC _rx_tails
        _tx_heads       DW 4 DUP(0)     ; TX ring heads
        _tx_tails       DW 4 DUP(0)     ; TX ring tails
        _rx_heads       DW 4 DUP(0)     ; RX ring heads
        _rx_tails       DW 4 DUP(0)     ; RX ring tails
        
        ; Work pending flags (checked in main loop)
        PUBLIC _work_pending
        _work_pending   DB 4 DUP(0)     ; Per-NIC work flags
        
        ; Performance counters (updated per packet)
        ALIGN 16
        PUBLIC _packet_counters
        _packet_counters:
        tx_packets      DD 4 DUP(0)     ; TX packet counts
        rx_packets      DD 4 DUP(0)     ; RX packet counts
        
        ; Copy-break buffers (192 bytes each, cache-aligned)
        ALIGN 16
        PUBLIC _copy_break_buffers
        _copy_break_buffers:
        DB 4 * 192 DUP(0)               ; Small packet buffers

        ; Extension API data structures (GPT-5 Stage 0)
        ALIGN 2                         ; GPT-5: Use ALIGN 2 for 16-bit, not 4
        PUBLIC _extension_feature_bitmap
        PUBLIC _extension_version_info
        _extension_feature_bitmap   DW 0    ; Current enabled features
        _extension_version_info:
        _ext_version_major          DB 1    ; Major version
        _ext_version_minor          DB 0    ; Minor version
        _ext_api_signature          DW 5845h ; 'EX' signature (corrected)
        
        ; Extension vector table (far pointers to handlers)
        PUBLIC _extension_vectors
        _extension_vectors:
        _ext_diagnostics_handler    DD 0    ; Health diagnostics handler
        _ext_runtime_cfg_handler    DD 0    ; Runtime config handler  
        _ext_xms_control_handler    DD 0    ; XMS control handler
        _ext_busmaster_handler      DD 0    ; Bus mastering test handler
        _ext_multi_nic_handler      DD 0    ; Multi-NIC handler

        ; Bus mastering test data (GPT-5 Stage 1: Minimal resident state)
        PUBLIC busmaster_test_status
        PUBLIC busmaster_test_result  
        PUBLIC busmaster_last_score
        PUBLIC busmaster_confidence_level
        PUBLIC busmaster_features_detected
        PUBLIC network_quiesced
        
        busmaster_test_status       DW 0    ; 0=idle, 1=armed, 2=testing, 3=complete
        busmaster_test_result       DW 0    ; Test result code
        busmaster_last_score        DW 0    ; Last test score (0-552)
        busmaster_confidence_level  DW 0    ; Confidence level (0-3)
        busmaster_features_detected DW 0    ; Hardware features detected
        network_quiesced            DW 0    ; Network traffic quiesced flag

        ; Health diagnostics data (GPT-5 Stage 2: Minimal resident state)
        ; GPT-5: ABI versioning for forward compatibility
        PUBLIC health_data_header
        PUBLIC health_error_counters
        PUBLIC health_performance_metrics
        PUBLIC health_last_diagnostic_time
        PUBLIC health_diagnostic_flags
        PUBLIC health_data_size
        
        ; ABI version header (GPT-5: forward compatibility)
        health_data_header:
        DD 'HLTH'                   ; Signature for health data block
        DW 0100h                    ; Version 1.0 (major.minor in BCD)
        DW health_data_size         ; Total size of health data block
        DB 4                        ; Maximum NIC count supported
        DB 0                        ; Reserved for future use
        
        health_error_counters       DD 8 DUP(0)    ; Error counts per category:
                                                    ; [0] TX errors, [1] RX errors
                                                    ; [2] DMA errors, [3] Memory errors  
                                                    ; [4] Hardware errors, [5] API errors
                                                    ; [6] Buffer errors, [7] Timeout errors
        
        health_performance_metrics  DW 16 DUP(0)   ; Key performance indicators:
                                                    ; [0-3] TX rates per NIC
                                                    ; [4-7] RX rates per NIC  
                                                    ; [8-11] Buffer utilization per NIC
                                                    ; [12] CPU utilization estimate
                                                    ; [13] Memory pressure indicator
                                                    ; [14] ISR frequency
                                                    ; [15] API call frequency
        
        health_last_diagnostic_time DD 0           ; Last diagnostic timestamp (ticks)
        health_diagnostic_flags     DW 0           ; Status flags and configuration
        
        ; Calculate total health data block size
        health_data_size EQU ($ - health_data_header)

        ; Runtime configuration data (GPT-5 Stage 3A: Dynamic parameter control)
        PUBLIC runtime_config_header
        PUBLIC runtime_config_params
        PUBLIC runtime_config_flags
        PUBLIC runtime_config_size
        
        ; Configuration ABI header (GPT-5: versioned for forward compatibility)
        runtime_config_header:
        DD 'CONF'                   ; Signature for configuration data
        DW 0100h                    ; Version 1.0 (major.minor in BCD)
        DW runtime_config_size      ; Total size of config data block
        DB 32                       ; Maximum parameters supported
        DB 0                        ; Reserved for future use
        
        ; Runtime adjustable parameters (enterprise production control)
        runtime_config_params:
        ; Network performance parameters [0-7]
        tx_timeout_ms       DW 5000     ; TX timeout in milliseconds
        rx_poll_interval    DW 100      ; RX polling interval (ticks)
        buffer_threshold    DB 75       ; Buffer utilization threshold %
        retry_count         DB 3        ; Packet retry attempts
        dma_burst_size      DW 1024     ; DMA burst transfer size
        irq_coalescing      DB 1        ; IRQ coalescing enabled (0/1)
        flow_control        DB 1        ; Flow control enabled (0/1)
        duplex_mode         DB 2        ; 0=half, 1=full, 2=auto
        
        ; Memory management parameters [8-15]
        buffer_pool_size    DW 32       ; Number of buffers in pool
        xms_threshold       DW 512      ; XMS migration threshold (KB free)
        copy_break_size     DW 128      ; Copy break threshold (bytes)
        memory_pressure_limit DB 90     ; Memory pressure limit %
        gc_interval         DW 1000     ; Garbage collection interval (ticks)
        alloc_strategy      DB 0        ; 0=first_fit, 1=best_fit, 2=pool
        Reserved_mem1       DB 0        ; Reserved
        Reserved_mem2       DB 0        ; Reserved
        
        ; Diagnostic and logging parameters [16-23]
        log_level           DB 2        ; 0=off, 1=error, 2=warn, 3=info, 4=debug
        health_check_interval DW 182    ; Health check interval (ticks)
        stats_reset_on_read DB 0        ; Reset stats after reading (0/1)
        error_threshold     DW 100      ; Error rate threshold (per minute)
        perf_monitoring     DB 1        ; Performance monitoring enabled (0/1)
        debug_output        DB 0        ; Debug output to console (0/1)
        Reserved_diag1      DB 0        ; Reserved
        Reserved_diag2      DB 0        ; Reserved
        
        ; Hardware-specific parameters [24-31]
        nic_speed           DB 0        ; 0=auto, 1=10Mbps, 2=100Mbps
        nic_duplex          DB 0        ; 0=auto, 1=half, 2=full
        bus_master_enable   DB 1        ; Bus mastering enabled (0/1)
        pio_threshold       DW 64       ; PIO vs DMA threshold (bytes)
        irq_mask_time       DW 10       ; IRQ mask time (microseconds)
        cable_test_enable   DB 0        ; Cable testing enabled (0/1)
        Reserved_hw1        DB 0        ; Reserved
        Reserved_hw2        DB 0        ; Reserved
        
        ; Configuration control flags
        runtime_config_flags    DW 0    ; Control flags and status
                                        ; Bit 0: Configuration modified
                                        ; Bit 1: Validation required
        
        ; GPT-5 FIX: Seqlock for lock-free reads
        config_seqlock          DW 0    ; 16-bit sequence counter (even=stable, odd=writing)
        config_write_lock       DB 0    ; Write lock (0=free, 1=locked)
                                        ; Bit 2: Commit pending
                                        ; Bit 3: Rollback available
                                        ; Bits 4-15: Reserved
        
        ; Parameter validation ranges (min/max values for safety)
        PUBLIC param_validation_table
        param_validation_table:
        ; Each entry: min_value (DW), max_value (DW), flags (DB), reserved (DB)
        DW 100, 30000, 1, 0     ; tx_timeout_ms (100ms - 30s)
        DW 10, 1000, 1, 0       ; rx_poll_interval (10 - 1000 ticks)
        DW 25, 95, 2, 0         ; buffer_threshold (25% - 95%)
        DW 1, 10, 2, 0          ; retry_count (1 - 10 attempts)
        DW 64, 8192, 1, 0       ; dma_burst_size (64 - 8192 bytes)
        DW 0, 1, 2, 0           ; irq_coalescing (boolean)
        DW 0, 1, 2, 0           ; flow_control (boolean)
        DW 0, 2, 2, 0           ; duplex_mode (0-2)
        
        ; Calculate total runtime config size
        runtime_config_size EQU ($ - runtime_config_header)

        ; XMS Buffer Migration data (GPT-5 Stage 3B: Dynamic memory management)
        PUBLIC xms_buffer_header
        PUBLIC xms_buffer_status
        PUBLIC xms_pool_descriptors
        PUBLIC xms_migration_stats
        PUBLIC xms_buffer_size
        
        ; XMS ABI header (GPT-5: versioned for forward compatibility)
        xms_buffer_header:
        DD 'XMEM'                   ; Signature for XMS buffer data
        DW 0100h                    ; Version 1.0 (major.minor in BCD)
        DW xms_buffer_size          ; Total size of XMS data block
        DB 16                       ; Maximum XMS pools supported
        DB 0                        ; Reserved for future use
        
        ; XMS system status and control
        xms_buffer_status:
        xms_driver_available    DB 0    ; XMS driver detected (0/1)
        xms_total_kb            DW 0    ; Total XMS available (KB)
        xms_free_kb             DW 0    ; Free XMS available (KB)
        xms_largest_block       DW 0    ; Largest free block (KB)
        conventional_free_kb    DW 0    ; Conventional memory free (KB)
        migration_threshold     DW 512  ; Auto-migrate when conv < threshold (KB)
        migration_enabled       DB 1    ; Auto migration enabled (0/1)
        migration_in_progress   DB 0    ; Migration operation active (0/1)
        in_isr_flag             DB 0    ; GPT-5 FIX: ISR nesting counter (0=process, >0=ISR depth)
        
        ; XMS buffer pool descriptors (16 pools max)
        xms_pool_descriptors:
        pool_entry_size         = 16    ; Size of each pool descriptor
        ; Pool descriptor structure:
        ; +0  DW handle           - XMS handle (0=unused)
        ; +2  DW size_kb          - Pool size in KB
        ; +4  DW buffer_count     - Number of buffers in pool
        ; +6  DW buffer_size      - Size of each buffer
        ; +8  DW alloc_offset     - Current allocation offset
        ; +10 DB pool_type        - Pool type (0=RX, 1=TX, 2=general)
        ; +11 DB flags            - Pool flags (bit 0=active, bit 1=migrated)
        ; +12 DD conventional_ptr - Original conventional memory pointer
        DB 16 * pool_entry_size DUP(0)  ; 16 pool descriptors × 16 bytes
        
        ; XMS migration statistics
        xms_migration_stats:
        total_migrations        DW 0    ; Total migration operations
        successful_migrations   DW 0    ; Successful migrations
        failed_migrations       DW 0    ; Failed migrations
        bytes_migrated_kb       DD 0    ; Total bytes migrated (KB)
        migration_time_ticks    DD 0    ; Total migration time (ticks)
        last_migration_time     DD 0    ; Last migration timestamp
        conventional_saved_kb   DW 0    ; Conventional memory saved
        performance_impact      DB 0    ; Performance impact (0-100%)
        
        ; XMS function vectors (populated during XMS driver detection)
        PUBLIC xms_driver_entry
        xms_driver_entry        DD 0    ; Far pointer to XMS driver entry
        
        ; Buffer allocation tracking
        PUBLIC xms_alloc_bitmap
        xms_alloc_bitmap        DB 32 DUP(0)  ; Bitmap for buffer allocation (256 buffers)
        
        ; Migration control flags
        xms_control_flags       DW 0    ; Control flags and status
                                        ; Bit 0: Auto migration enabled
                                        ; Bit 1: Migration in progress
                                        ; Bit 2: Emergency migration mode
                                        ; Bit 3: XMS driver validated
                                        ; Bit 4: Conventional memory critical
                                        ; Bits 5-15: Reserved
        
        ; Calculate total XMS buffer data size
        xms_buffer_size EQU ($ - xms_buffer_header)

        ; Multi-NIC Coordination data (GPT-5 Stage 3C: Enterprise HA networking)
        PUBLIC multi_nic_header
        PUBLIC multi_nic_status
        PUBLIC nic_descriptors
        PUBLIC load_balance_config
        PUBLIC failover_stats
        PUBLIC multi_nic_size
        
        ; GPT-5 FIX: Minimal multi-NIC header (8 bytes only)
        multi_nic_header:
        DD 'MNIC'                   ; Signature
        DW 0100h                    ; Version 1.0
        DW multi_nic_size           ; Total size
        
        ; GPT-5 FIX: Minimal control (4 bytes only)
        primary_nic_index       DB 0    ; Primary NIC (0-3)
        multi_nic_mode          DB 0    ; 0=none, 1=failover only
        failover_flags          DB 0    ; Bit 0: auto-failover enabled
        load_balance_flags      DB 0    ; Reserved - NOT SUPPORTED
        
        ; GPT-5 FIX: Minimal NIC descriptors (64 bytes = 4 × 16)
        nic_descriptors:
        ; Minimal 16-byte descriptor:
        ; +0  DW io_base          - I/O base
        ; +2  DB irq              - IRQ number  
        ; +3  DB status           - 0=down, 1=up
        ; +4  DD tx_packets       - TX count
        ; +8  DD rx_packets       - RX count
        ; +12 DD errors           - Error count
        DB 4 * 16 DUP(0)            ; 4 NICs × 16 bytes
        
        ; GPT-5 FIX: Minimal failover stats (8 bytes only)
        failover_count          DW 0    ; Total failover events
        failover_success        DW 0    ; Successful failovers
        failover_failed         DW 0    ; Failed failovers
        load_balance_switches   DW 0    ; Reserved - NOT USED
        
        ; GPT-5 FIX: Failover support data
        previous_nic_index      DB 0FFh ; Previous primary NIC
        multicast_count         DB 0    ; Number of multicast addresses
        station_address         DB 6 DUP(0) ; Primary MAC address
        our_ip_address          DD 0    ; Our IP for gratuitous ARP
        arp_buffer              DB 42 DUP(0) ; Temp buffer for ARP packet
        
        ; GPT-5 FIX: Load balance removed - failover only
        ; Load balancing requires switch support and violates
        ; packet driver spec (single MAC requirement)
        load_balance_algorithm  DB 0    ; NOT SUPPORTED
        load_balance_interval   DW 0    ; NOT SUPPORTED
        
        ; Calculate total multi-NIC data size
        multi_nic_size EQU ($ - multi_nic_header)

_hot_data_end:

;-----------------------------------------------------------------------------
; HOT CODE - Fast path functions
; These are the most frequently called functions
;-----------------------------------------------------------------------------
ALIGN 16
PUBLIC _hot_code_start

_hot_code_start:

; Include optimized fast path routines
; These will be linked here to maintain locality

; Fast TX path (from vortex_pio_fast.asm)
EXTERN _vortex_tx_fast: NEAR
EXTERN _vortex_rx_fast: NEAR

; Fast descriptor operations (inline for speed)
PUBLIC _update_tx_head
_update_tx_head PROC NEAR
        ; Input: BX = NIC index (0-3)
        ; Updates TX head with wraparound (GPT-5 fixed 16-bit addressing)
        push    ax
        shl     bx, 1                   ; Scale BX for word addressing
        mov     ax, [_tx_heads + bx]    ; Valid 16-bit addressing
        inc     ax
        and     ax, 31                  ; Ring size - 1
        mov     [_tx_heads + bx], ax    ; Store updated head
        shr     bx, 1                   ; Restore BX
        pop     ax
        ret
_update_tx_head ENDP

PUBLIC _update_rx_tail
_update_rx_tail PROC NEAR
        ; Input: BX = NIC index (0-3)  
        ; Updates RX tail with wraparound (GPT-5 fixed 16-bit addressing)
        push    ax
        shl     bx, 1                   ; Scale BX for word addressing
        mov     ax, [_rx_tails + bx]    ; Valid 16-bit addressing
        inc     ax
        and     ax, 31                  ; Ring size - 1
        mov     [_rx_tails + bx], ax    ; Store updated tail
        shr     bx, 1                   ; Restore BX
        pop     ax
        ret
_update_rx_tail ENDP

; Fast I/O base lookup
PUBLIC _get_io_base
_get_io_base PROC NEAR
        ; Input: BX = NIC index
        ; Output: DX = I/O base
        mov     dx, [_io_bases + bx*2]
        ret
_get_io_base ENDP

; Fast work check
PUBLIC _check_work_pending
_check_work_pending PROC NEAR
        ; Output: ZF=1 if no work, ZF=0 if work pending
        push    cx
        push    si
        mov     cx, 4
        mov     si, OFFSET _work_pending
        xor     ax, ax
.check_loop:
        or      al, [si]
        inc     si
        loop    .check_loop
        test    al, al          ; Set ZF based on result
        pop     si
        pop     cx
        ret
_check_work_pending ENDP

_hot_code_end:

;-----------------------------------------------------------------------------
; Main packet processing loop (hot)
; This is the core of the driver's runtime
;-----------------------------------------------------------------------------
PUBLIC _packet_main_loop
_packet_main_loop PROC NEAR
        push    bp
        mov     bp, sp
        push    bx
        push    cx
        push    dx
        push    si
        push    di
        
.main_loop:
        ; Check for work on any NIC
        call    _check_work_pending
        jz      .idle           ; No work pending
        
        ; Process each NIC with work
        xor     bx, bx          ; NIC index
.nic_loop:
        cmp     bx, 4
        jae     .main_loop      ; Processed all NICs
        
        ; Check this NIC's work flag
        test    BYTE PTR [_work_pending + bx], 1
        jz      .next_nic
        
        ; Clear work flag
        mov     BYTE PTR [_work_pending + bx], 0
        
        ; Get I/O base for this NIC
        call    _get_io_base
        
        ; Process RX packets (with budget)
        mov     cx, 32          ; RX budget
        call    _process_rx_batch
        
        ; Process TX completions
        call    _process_tx_batch
        
.next_nic:
        inc     bx
        jmp     .nic_loop
        
.idle:
        ; Brief idle before checking again
        ; Could add HLT here for power saving
        jmp     .main_loop
        
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     bp
        ret
_packet_main_loop ENDP

; Stub functions (would link to actual implementations)
_process_rx_batch PROC NEAR
        ret
_process_rx_batch ENDP

_process_tx_batch PROC NEAR
        ret
_process_tx_batch ENDP

_hot_section_end:

ENDS

;-----------------------------------------------------------------------------
; COLD SECTION - Rarely accessed code
; This section can be discarded after initialization
;-----------------------------------------------------------------------------
SEGMENT _COLD_TEXT PUBLIC CLASS=COLD_CODE USE16

PUBLIC _cold_section_start
PUBLIC _cold_section_end

_cold_section_start:

;-----------------------------------------------------------------------------
; COLD CODE - Initialization and error handling
;-----------------------------------------------------------------------------

; One-time initialization code
PUBLIC _driver_init_cold
_driver_init_cold PROC FAR
        push    bp
        mov     bp, sp
        
        ; Hardware detection (only at startup)
        call    _detect_hardware
        
        ; PCI enumeration (only at startup)
        call    _pci_enumerate
        
        ; Apply SMC patches (one-time)
        call    _apply_smc_patches
        
        ; Allocate memory pools (one-time)
        call    _allocate_memory
        
        pop     bp
        ret
_driver_init_cold ENDP

; Error handling (rarely executed)
PUBLIC _handle_fatal_error
_handle_fatal_error PROC FAR
        ; Log error and halt
        ; This is cold path, performance doesn't matter
        push    ax
        push    dx
        
        ; Display error message
        mov     ah, 09h
        mov     dx, OFFSET error_msg
        int     21h
        
        ; Halt system
        cli
        hlt
        
        pop     dx
        pop     ax
        ret
_handle_fatal_error ENDP

; Diagnostic functions (cold path)
PUBLIC _dump_statistics
_dump_statistics PROC FAR
        ; Dump stats to screen/log
        ; Not performance critical
        ret
_dump_statistics ENDP

; Stub functions for cold path
_detect_hardware PROC NEAR
        ret
_detect_hardware ENDP

_pci_enumerate PROC NEAR
        ret
_pci_enumerate ENDP

_apply_smc_patches PROC NEAR
        ret
_apply_smc_patches ENDP

_allocate_memory PROC NEAR
        ret
_allocate_memory ENDP

;-----------------------------------------------------------------------------
; Extension API initialization (GPT-5 Stage 0)
;-----------------------------------------------------------------------------
PUBLIC _extension_api_init
_extension_api_init PROC NEAR
        push    ax
        push    bx
        
        ; Initialize feature bitmap with available features
        ; GPT-5 Stage 1: Enable bus mastering test feature
        ; GPT-5 Stage 2: Enable health diagnostics feature  
        ; GPT-5 Stage 3A: Enable runtime configuration feature
        ; GPT-5 Stage 3B: Enable XMS buffer migration feature
        ; GPT-5 Stage 3C: Enable multi-NIC coordination feature
        mov     word ptr [_extension_feature_bitmap], EXT_FEATURE_BUSMASTER OR EXT_FEATURE_DIAGNOSTICS OR EXT_FEATURE_RUNTIME_CFG OR EXT_FEATURE_XMS_BUFFERS OR EXT_FEATURE_MULTI_NIC
        
        ; Initialize version info (already set in data section)
        
        ; Clear extension vector table (GPT-5 fix: clear all DD entries properly)
        push    di
        push    cx
        xor     ax, ax
        mov     di, OFFSET _extension_vectors
        mov     cx, 10                  ; 5 DD entries = 10 words
        rep     stosw
        pop     cx
        pop     di
        
        pop     bx
        pop     ax
        ret
_extension_api_init ENDP

;-----------------------------------------------------------------------------
; Enable extension feature (GPT-5 Stage 0)  
; Input: AX = feature flag to enable
;-----------------------------------------------------------------------------
PUBLIC _extension_enable_feature
_extension_enable_feature PROC NEAR
        push    bx
        
        ; Set feature bit in bitmap
        mov     bx, [_extension_feature_bitmap]
        or      bx, ax
        mov     [_extension_feature_bitmap], bx
        
        pop     bx
        ret
_extension_enable_feature ENDP

;-----------------------------------------------------------------------------
; Disable extension feature (GPT-5 Stage 0)
; Input: AX = feature flag to disable  
;-----------------------------------------------------------------------------
PUBLIC _extension_disable_feature
_extension_disable_feature PROC NEAR
        push    bx
        
        ; Clear feature bit in bitmap
        not     ax                      ; Invert bits for AND mask
        mov     bx, [_extension_feature_bitmap]
        and     bx, ax
        mov     [_extension_feature_bitmap], bx
        
        pop     bx
        ret
_extension_disable_feature ENDP

; Cold data
error_msg       DB 'Fatal driver error!', 13, 10, '$'

_cold_section_end:

ENDS

;-----------------------------------------------------------------------------
; INIT SECTION - Discardable after initialization
; This section is freed after driver setup completes
;-----------------------------------------------------------------------------
SEGMENT _INIT_TEXT PUBLIC CLASS=INIT_CODE USE16

PUBLIC _init_section_start
PUBLIC _init_section_end

_init_section_start:

; Initialization messages and temporary data
init_banner     DB 'PCI Packet Driver v1.0 Initializing...', 13, 10, '$'
init_done       DB 'Initialization complete, freeing init memory', 13, 10, '$'

; Large temporary buffers used only during init
temp_buffer     DB 4096 DUP(0)

PUBLIC _free_init_memory
_free_init_memory PROC FAR
        push    ax
        push    dx
        push    es
        
        ; Display message
        mov     ah, 09h
        mov     dx, OFFSET init_done
        int     21h
        
        ; Calculate size to keep resident (hot section only)
        mov     ax, OFFSET _hot_section_end
        add     ax, 15          ; Round up to paragraph
        shr     ax, 4           ; Convert to paragraphs
        
        ; TSR keeping only hot section
        mov     dx, ax
        mov     ax, 3100h       ; TSR function
        int     21h
        
        pop     es
        pop     dx
        pop     ax
        ret
_free_init_memory ENDP

_init_section_end:

ENDS

;-----------------------------------------------------------------------------
; Memory layout summary for linker
;-----------------------------------------------------------------------------
SEGMENT _LAYOUT_INFO PUBLIC CLASS=INFO USE16

PUBLIC _memory_layout_info
_memory_layout_info:
        DW OFFSET _hot_section_start
        DW OFFSET _hot_section_end
        DW OFFSET _cold_section_start
        DW OFFSET _cold_section_end
        DW OFFSET _init_section_start
        DW OFFSET _init_section_end

; Calculate section sizes
PUBLIC _hot_section_size
PUBLIC _cold_section_size
PUBLIC _init_section_size

_hot_section_size  EQU (_hot_section_end - _hot_section_start)
_cold_section_size EQU (_cold_section_end - _cold_section_start)
_init_section_size EQU (_init_section_end - _init_section_start)

ENDS

END