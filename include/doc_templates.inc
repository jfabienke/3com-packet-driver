; @file doc_templates.inc
; @brief Assembly function documentation templates for 3Com Packet Driver
;
; Provides standardized documentation templates for assembly functions
; to ensure consistent inline documentation across all .asm files
;

;=============================================================================
; ASSEMBLY FUNCTION HEADER TEMPLATE
;
; Template for documenting assembly functions with comprehensive parameter
; and return value documentation, defensive programming notes, and hardware
; interaction details.
;=============================================================================

%macro ASM_FUNC_HEADER 6
;-----------------------------------------------------------------------------
; %1 - Function name and brief description
;
; Purpose: %2
; 
; Context: %3
;
; Parameters:
; %4
;
; Returns:
; %5
;
; Registers Modified: %6
;
; Error Conditions:
;   - Document all possible error states
;   - Include recovery procedures where applicable
;   - Note any defensive programming checks
;
; Hardware Interaction:
;   - Document direct hardware access (I/O ports, memory-mapped I/O)
;   - Include register programming sequences
;   - Note timing-sensitive operations
;
; Memory Safety:
;   - Document memory access patterns
;   - Include bounds checking information
;   - Note any buffer operations
;
; CPU Requirements:
;   - Minimum CPU level required (8088, 80286, 80386, etc.)
;   - Special instruction usage
;   - Performance considerations
;
; Defensive Programming:
;   - Input validation performed
;   - Boundary condition handling
;   - Error recovery mechanisms
;
; Integration Notes:
;   - C function interface compatibility
;   - Calling convention details
;   - Stack frame management
;
; Example Usage:
;   ; Include example of function call from assembly and C
;
; Related Functions:
;   - List related assembly and C functions
;   - Cross-references to documentation
;
; Implementation Notes:
;   - Algorithm details
;   - Performance optimizations
;   - Hardware-specific considerations
;
; Version History:
;   - Phase implementation details
;   - Major changes and rationale
;-----------------------------------------------------------------------------
%endmacro

;=============================================================================
; HARDWARE REGISTER OPERATION TEMPLATE
;
; Template for documenting hardware register operations with timing,
; defensive programming, and error handling considerations.
;=============================================================================

%macro HW_REG_OP_HEADER 5
;-----------------------------------------------------------------------------
; Hardware Register Operation: %1
;
; Register: %2
; Operation Type: %3 (READ/WRITE/READ-MODIFY-WRITE)
; 
; Purpose: %4
;
; Hardware Context:
; %5
;
; Register Programming Sequence:
;   1. Document exact sequence of register operations
;   2. Include timing requirements between operations
;   3. Note any prerequisite register settings
;   4. Document post-operation verification steps
;
; Timing Requirements:
;   - Minimum delay between operations: XXX microseconds
;   - Maximum operation completion time: XXX microseconds
;   - Critical timing sequences documented
;
; Error Detection:
;   - Methods for detecting operation failure
;   - Recovery procedures for failed operations
;   - Timeout handling mechanisms
;
; Defensive Programming:
;   - Input validation for register values
;   - Boundary checking for register addresses
;   - Sanity checks for hardware state
;
; Hardware Dependencies:
;   - Required hardware state before operation
;   - Side effects on other registers
;   - Impact on hardware operation modes
;
; Performance Considerations:
;   - Operation cost in CPU cycles
;   - Memory bandwidth impact
;   - Optimization opportunities
;
; Multi-NIC Support:
;   - Per-instance register handling
;   - Context switching requirements
;   - Shared resource management
;-----------------------------------------------------------------------------
%endmacro

;=============================================================================
; INTERRUPT HANDLER TEMPLATE
;
; Template for documenting interrupt handlers with context preservation,
; hardware state management, and performance considerations.
;=============================================================================

%macro IRQ_HANDLER_HEADER 4
;-----------------------------------------------------------------------------
; Interrupt Handler: %1
;
; IRQ Level: %2
; Handler Type: %3 (PRIMARY/SECONDARY/SHARED)
; 
; Purpose: %4
;
; Entry Conditions:
;   - CPU state at interrupt entry
;   - Expected hardware conditions
;   - Required system state
;
; Context Preservation:
;   - Registers saved/restored
;   - Segment register handling
;   - Stack management
;   - Flag preservation
;
; Interrupt Processing:
;   - Hardware acknowledgment sequence
;   - Interrupt source identification
;   - Priority handling for multiple sources
;   - Nested interrupt considerations
;
; Hardware State Management:
;   - Register context switching
;   - Multi-NIC interrupt routing
;   - Shared interrupt handling
;
; Performance Characteristics:
;   - Maximum interrupt latency: XXX microseconds
;   - Average processing time: XXX microseconds
;   - Memory access patterns
;   - Cache impact considerations
;
; Error Handling:
;   - Spurious interrupt detection
;   - Hardware error recovery
;   - Timeout handling
;   - Panic condition management
;
; DOS Integration:
;   - DOS interrupt chain management
;   - Reentrancy considerations
;   - Critical section handling
;
; Multi-NIC Considerations:
;   - Instance identification
;   - Interrupt source disambiguation
;   - Load balancing between NICs
;
; Debugging Support:
;   - Debug register usage
;   - Trace point locations
;   - Performance monitoring hooks
;-----------------------------------------------------------------------------
%endmacro

;=============================================================================
; MEMORY OPERATION TEMPLATE
;
; Template for documenting memory operations including buffer management,
; DMA operations, and memory safety considerations.
;=============================================================================

%macro MEM_OP_HEADER 5
;-----------------------------------------------------------------------------
; Memory Operation: %1
;
; Memory Type: %2 (CONVENTIONAL/XMS/DMA_BUFFER)
; Operation: %3 (ALLOCATE/FREE/COPY/MAP)
; Size Range: %4
; 
; Purpose: %5
;
; Memory Layout:
;   - Buffer structure documentation
;   - Alignment requirements
;   - Padding considerations
;   - Memory map organization
;
; Allocation Strategy:
;   - Allocation algorithm details
;   - Fragmentation prevention
;   - Memory pool management
;   - Fallback allocation methods
;
; Safety Mechanisms:
;   - Bounds checking implementation
;   - Buffer overflow prevention
;   - Memory leak detection
;   - Double-free protection
;
; DMA Considerations:
;   - DMA coherency requirements
;   - Cache management
;   - Physical memory constraints
;   - Scatter-gather limitations
;
; Performance Optimization:
;   - Memory access patterns
;   - Cache-friendly operations
;   - Bulk operation support
;   - Zero-copy implementations
;
; Error Recovery:
;   - Out-of-memory handling
;   - Corruption detection
;   - Recovery procedures
;   - Graceful degradation
;
; DOS Memory Model:
;   - Segment management
;   - Far pointer handling
;   - Memory model compatibility
;   - TSR memory constraints
;
; Multi-Instance Support:
;   - Per-instance memory pools
;   - Shared memory regions
;   - Synchronization requirements
;-----------------------------------------------------------------------------
%endmacro

;=============================================================================
; PACKET PROCESSING TEMPLATE
;
; Template for documenting packet processing functions including protocol
; handling, routing decisions, and performance optimization.
;=============================================================================

%macro PKT_PROC_HEADER 4
;-----------------------------------------------------------------------------
; Packet Processing: %1
;
; Packet Direction: %2 (RX/TX/BOTH)
; Protocol Support: %3
; 
; Purpose: %4
;
; Packet Flow:
;   - Packet reception/transmission pipeline
;   - Protocol processing stages
;   - Routing decision points
;   - Output destination handling
;
; Protocol Handling:
;   - Supported protocol types
;   - Header parsing/generation
;   - Checksum calculation/verification
;   - Protocol-specific processing
;
; Routing Logic:
;   - Multi-NIC routing decisions
;   - Flow-aware routing implementation
;   - Load balancing algorithms
;   - Route cache management
;
; Buffer Management:
;   - Buffer allocation/deallocation
;   - Zero-copy optimization opportunities
;   - Memory pool utilization
;   - Buffer chain handling
;
; Performance Optimization:
;   - Fast path identification
;   - Assembly language optimizations
;   - Cache-friendly data structures
;   - Batch processing capabilities
;
; Error Handling:
;   - Malformed packet detection
;   - Protocol violation handling
;   - Resource exhaustion management
;   - Error reporting mechanisms
;
; Quality of Service:
;   - Priority handling
;   - Bandwidth limiting
;   - Flow control implementation
;   - Congestion management
;
; Statistics Collection:
;   - Performance metrics
;   - Error counters
;   - Protocol statistics
;   - Debug information
;-----------------------------------------------------------------------------
%endmacro

;=============================================================================
; API INTERFACE TEMPLATE
;
; Template for documenting Packet Driver API functions with parameter
; validation, error handling, and compatibility considerations.
;=============================================================================

%macro API_FUNC_HEADER 5
;-----------------------------------------------------------------------------
; Packet Driver API: %1
;
; Function Code: %2
; API Version: %3
; Standard: %4 (PKT-DRV-11/EXTENDED)
; 
; Purpose: %5
;
; API Parameters:
;   - Input register assignments
;   - Output register assignments
;   - Memory buffer requirements
;   - Callback function specifications
;
; Parameter Validation:
;   - Input parameter checking
;   - Range validation
;   - Handle validation
;   - Memory address verification
;
; Standard Compliance:
;   - Packet Driver Specification compliance
;   - Compatibility with existing applications
;   - Extension documentation
;   - Behavior differences from standard
;
; Error Codes:
;   - Standard Packet Driver error codes
;   - Extended error information
;   - Error recovery suggestions
;   - Debugging information
;
; Multi-Handle Support:
;   - Handle management
;   - Resource allocation per handle
;   - Handle cleanup procedures
;   - Handle priority management
;
; Multi-NIC Integration:
;   - NIC selection algorithms
;   - Load balancing implementation
;   - Failover mechanisms
;   - Performance optimization
;
; Backward Compatibility:
;   - DOS application compatibility
;   - Legacy protocol support
;   - Standard API behavior
;   - Migration path documentation
;
; Performance Considerations:
;   - API call overhead
;   - Memory allocation impact
;   - CPU utilization
;   - Interrupt latency effects
;
; Thread Safety:
;   - Reentrancy considerations
;   - Critical section management
;   - DOS interrupt handling
;   - State consistency
;-----------------------------------------------------------------------------
%endmacro

;=============================================================================
; INITIALIZATION SEQUENCE TEMPLATE
;
; Template for documenting initialization sequences with dependencies,
; error recovery, and system integration considerations.
;=============================================================================

%macro INIT_SEQ_HEADER 4
;-----------------------------------------------------------------------------
; Initialization Sequence: %1
;
; Phase: %2 (EARLY/MAIN/LATE/CLEANUP)
; Dependencies: %3
; 
; Purpose: %4
;
; Initialization Steps:
;   1. Document exact initialization sequence
;   2. Include dependency resolution
;   3. Note critical timing requirements
;   4. Document verification steps
;
; Dependency Management:
;   - Required system resources
;   - Hardware detection requirements
;   - Memory allocation dependencies
;   - Service initialization order
;
; Error Recovery:
;   - Initialization failure handling
;   - Partial initialization recovery
;   - Resource cleanup procedures
;   - Graceful degradation options
;
; System Integration:
;   - DOS memory management
;   - Interrupt vector installation
;   - TSR registration
;   - Device driver integration
;
; Hardware Discovery:
;   - Hardware detection algorithms
;   - Configuration parameter handling
;   - Auto-configuration capabilities
;   - Manual configuration support
;
; Resource Allocation:
;   - Memory pool initialization
;   - Buffer allocation
;   - Hardware resource assignment
;   - Interrupt allocation
;
; Validation Procedures:
;   - Self-test implementation
;   - Hardware functionality verification
;   - Configuration validation
;   - Performance baseline establishment
;
; Cleanup Procedures:
;   - Resource deallocation
;   - Hardware reset procedures
;   - System state restoration
;   - Error condition cleanup
;-----------------------------------------------------------------------------
%endmacro

;=============================================================================
; DEBUG AND DIAGNOSTICS TEMPLATE
;
; Template for documenting debug functions with trace capabilities,
; performance monitoring, and troubleshooting support.
;=============================================================================

%macro DEBUG_FUNC_HEADER 4
;-----------------------------------------------------------------------------
; Debug Function: %1
;
; Debug Level: %2 (TRACE/INFO/WARN/ERROR/FATAL)
; Category: %3 (HARDWARE/MEMORY/PROTOCOL/API)
; 
; Purpose: %4
;
; Debug Output:
;   - Output format specification
;   - Information content description
;   - Verbosity level control
;   - Output destination handling
;
; Performance Impact:
;   - Runtime overhead measurement
;   - Memory usage impact
;   - CPU utilization effects
;   - Optimization for production builds
;
; Trace Capabilities:
;   - Function entry/exit tracing
;   - Parameter value logging
;   - State change tracking
;   - Performance timing
;
; Error Diagnosis:
;   - Error condition detection
;   - Root cause analysis support
;   - Recovery procedure guidance
;   - System state dumping
;
; Statistics Collection:
;   - Performance metrics gathering
;   - Resource utilization tracking
;   - Error frequency monitoring
;   - Trend analysis support
;
; Configuration Control:
;   - Runtime debug level control
;   - Category-specific enabling
;   - Output filtering capabilities
;   - Performance monitoring toggle
;
; Integration with Tools:
;   - Debugger compatibility
;   - Log analysis tool support
;   - Performance profiler integration
;   - Automated testing hooks
;
; Production Considerations:
;   - Debug code removal for release
;   - Minimal performance impact
;   - Optional diagnostic retention
;   - Field troubleshooting support
;-----------------------------------------------------------------------------
%endmacro

;=============================================================================
; EXAMPLE FUNCTION DOCUMENTATION
;
; Complete example showing proper use of templates for a hardware initialization
; function with full defensive programming documentation.
;=============================================================================

; Example usage of ASM_FUNC_HEADER macro
ASM_FUNC_HEADER hardware_init_3c509b, \
               "Initialize 3C509B hardware with defensive programming", \
               "Called during driver initialization phase", \
               "BX = NIC instance (0-1), CX = I/O base address, DX = IRQ number", \
               "AX = 0 for success, non-zero error code", \
               "AX, BX, CX, DX, SI, DI, flags"

;=============================================================================
; MACRO UTILITY FUNCTIONS
;
; Helper macros for common documentation patterns and formatting.
;=============================================================================

; Macro for documenting register bit field operations
%macro REG_BITFIELD_DOC 4
;   Bit Field: %1
;   Register: %2
;   Bits: %3
;   Purpose: %4
;   Access: Document read/write/clear operations
;   Side Effects: Document any side effects of bit manipulation
%endmacro

; Macro for documenting timing-critical sections
%macro TIMING_CRITICAL_DOC 3
;   Critical Section: %1
;   Duration: %2 microseconds maximum
;   Rationale: %3
;   Implementation: Document exact timing implementation
;   Verification: Methods for timing validation
%endmacro

; Macro for documenting multi-NIC instance handling
%macro MULTI_NIC_DOC 2
;   Multi-NIC Function: %1
;   Instance Management: %2
;   Context Switching: Document instance context management
;   Resource Sharing: Document shared vs. per-instance resources
;   Synchronization: Document any required synchronization
%endmacro

;=============================================================================
; END OF DOCUMENTATION TEMPLATES
;=============================================================================