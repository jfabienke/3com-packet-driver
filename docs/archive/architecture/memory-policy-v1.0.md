# Memory Management Policy v1.0

**Version**: 1.0  
**Date**: 2025-08-22  
**Agent**: 11 - Memory Management  
**Status**: PRODUCTION - FROZEN

## Executive Summary

This document defines the comprehensive memory management policy for the 3Com Packet Driver modular architecture. It establishes DMA-safe buffer allocation policies, alignment requirements, boundary constraints, and memory tier usage strategies to ensure reliable operation across all supported DOS systems and network interface cards.

**Key Policy Points:**
- All DMA buffers must comply with 64KB boundary restrictions
- ISA bus-master devices limited to 16MB physical address space
- Minimum 16-byte alignment for all DMA descriptors
- Three-tier memory allocation strategy (XMS → UMB → Conventional)
- ISR-safe memory operations with ≤8μs critical sections

## Memory Architecture Overview

### Three-Tier Memory Management

The memory management system operates on a three-tier hierarchy optimized for DOS real-mode constraints:

```
┌─────────────────────────────────────────┐
│ Tier 1: XMS Extended Memory (>1MB)     │ ← Highest performance
│ - Physically contiguous                 │   Largest capacity
│ - DMA-capable                          │   CPU detection optimized
│ - 64KB boundary safe                   │
├─────────────────────────────────────────┤
│ Tier 2: UMB Upper Memory (640KB-1MB)   │ ← Medium performance
│ - UMB driver required                  │   Medium capacity
│ - Paragraph aligned                    │   Real-mode accessible
│ - ISA compatible                       │
├─────────────────────────────────────────┤
│ Tier 3: Conventional Memory (<640KB)   │ ← Highest compatibility
│ - Always available                     │   Lowest performance
│ - Most constrained                     │   Universal support
│ - Real-mode heap                       │
└─────────────────────────────────────────┘
```

### Memory Pool Specialization

#### Packet Buffer Pool
- **Purpose**: Network packet data storage
- **Size**: 1600 bytes (max Ethernet frame + margin)
- **Alignment**: 16-byte minimum, 32-byte preferred
- **Location**: XMS preferred, UMB fallback
- **Count**: 32-64 buffers depending on available memory

#### Descriptor Ring Pool  
- **Purpose**: DMA descriptor rings for 3C515-TX
- **Size**: Variable (4-256 descriptors × 8-16 bytes)
- **Alignment**: 32-byte mandatory for cache efficiency
- **Location**: XMS exclusive (DMA-capable)
- **Boundary**: Must not cross 64KB boundaries

#### Small Object Pool
- **Purpose**: Control structures, headers, temporary data
- **Size**: 16-512 bytes
- **Alignment**: CPU-dependent (16-bit: 2-byte, 32-bit: 4-byte)
- **Location**: Conventional memory acceptable
- **Count**: High-frequency allocation pool

## DMA Safety Requirements

### 64KB Boundary Compliance

**CRITICAL REQUIREMENT**: All DMA buffers must be allocated such that they do not cross 64KB boundaries.

```c
// Validation formula
uint32_t start_addr = (uint32_t)buffer;
uint32_t end_addr = start_addr + size - 1;
bool is_safe = (start_addr & 0xFFFF0000) == (end_addr & 0xFFFF0000);
```

**Implementation Policy:**
- Pre-validate all DMA allocations before returning to caller
- Use allocation padding to ensure boundary compliance
- Implement boundary checking in allocation path
- Log boundary violations for debugging

### ISA Bus-Master Constraints

For ISA-based network cards (3C509B and some 3C515-TX configurations):

**16MB Physical Address Limit:**
- All DMA buffers must be below physical address 0x1000000
- Check applies to buffer start + size
- Violations require bounce buffer allocation
- Log warnings for addresses above 16MB

**Alignment Requirements:**
- Minimum: 16 bytes for all DMA operations
- Preferred: 32 bytes for descriptor rings
- Cache line: 32 bytes for Pentium systems
- Custom alignment: Power-of-2 up to 256 bytes

### PCI Bus-Master Optimization

For PCI-based cards (3C515-TX in PCI mode):

**Enhanced Alignment:**
- 32-byte minimum for optimal performance
- 64-byte alignment for large transfers (>1KB)
- Cache-line alignment for descriptor rings
- Prefetchable memory regions when available

## Memory Allocation Strategies

### Allocation Priority Matrix

| Memory Type | Size Range | XMS Priority | UMB Priority | Conv Priority | Notes |
|-------------|------------|--------------|--------------|---------------|-------|
| DMA Buffers | >1KB | HIGH | MEDIUM | LOW | Prefer XMS for boundary safety |
| Descriptors | 64-1024B | MANDATORY | FORBIDDEN | FORBIDDEN | XMS only for DMA capability |
| Packet Data | 64-1600B | HIGH | HIGH | MEDIUM | Performance critical |
| Control Structures | <256B | LOW | MEDIUM | HIGH | Frequent allocation/free |
| Temporary Data | Any | LOW | LOW | HIGH | Short-lived allocations |

### Fallback Strategies

#### XMS Exhaustion
1. **First**: Attempt UMB allocation for non-DMA buffers
2. **Second**: Use conventional memory with performance warning
3. **Third**: Implement buffer reuse and pooling
4. **Last Resort**: Return allocation failure

#### UMB Unavailable
1. **Skip UMB tier**: Direct fallback to conventional memory
2. **Log notification**: Document UMB driver absence
3. **Adjust pool sizes**: Compensate with XMS or conventional pools

#### Conventional Memory Pressure
1. **Prioritize essential allocations**: Driver core functions first
2. **Defer optional features**: Advanced statistics, debugging
3. **Implement memory compaction**: Defragment when possible
4. **Graceful degradation**: Disable non-critical features

## Alignment and Boundary Policies

### Alignment Requirements by CPU Type

```c
typedef struct {
    cpu_type_t cpu;
    size_t min_alignment;
    size_t dma_alignment;
    size_t descriptor_alignment;
    size_t cache_line_size;
} alignment_policy_t;

static const alignment_policy_t alignment_policies[] = {
    {CPU_TYPE_8086,   1,  4, 16, 16},   // 8086/8088
    {CPU_TYPE_80286,  2,  4, 16, 16},   // 80286
    {CPU_TYPE_80386,  4,  4, 16, 16},   // 80386
    {CPU_TYPE_80486,  4, 16, 32, 32},   // 80486
    {CPU_TYPE_PENTIUM,4, 32, 32, 32},   // Pentium
};
```

### Boundary Constraint Checking

#### Pre-Allocation Validation
```c
bool validate_dma_allocation(void* base, size_t size, size_t alignment) {
    // Check alignment
    if (((uintptr_t)base % alignment) != 0) {
        return false;
    }
    
    // Check 64KB boundary
    uint32_t start = (uint32_t)base;
    uint32_t end = start + size - 1;
    if ((start & 0xFFFF0000) != (end & 0xFFFF0000)) {
        return false;
    }
    
    // Check 16MB limit for ISA
    if (is_isa_device && (end >= 0x1000000)) {
        return false;
    }
    
    return true;
}
```

#### Runtime Boundary Verification
- Validate before each DMA operation setup
- Check during buffer lock operations
- Verify after buffer resize or reallocation
- Audit during periodic memory validation

## ISR-Safe Memory Operations

### Interrupt Safety Requirements

**Critical Section Limits:**
- Maximum CLI duration: 8 microseconds
- Total ISR execution: ≤60 microseconds for receive path
- Memory operations in ISR: Try-lock only, no blocking

**ISR-Safe Operations:**
```c
// Allowed in ISR context
void* isr_safe_alloc(size_t size) {
    return try_alloc_from_pool(size, 0); // No wait, fail if unavailable
}

// Forbidden in ISR context
void* isr_unsafe_alloc(size_t size) {
    return alloc_with_gc(size); // May trigger garbage collection
}
```

### Lock-Free Buffer Management

#### Try-Lock Allocation
- Attempt allocation from pre-filled pools
- Return immediately if pools empty
- No memory allocation or deallocation in ISR
- Use atomic operations for pool management

#### Deferred Operations Queue
- Queue memory operations during ISR
- Process queue during main loop
- Maintain operation ordering for consistency
- Implement overflow protection

## Memory Pool Configuration

### Default Pool Sizes

#### Conventional Memory Systems (<1MB RAM)
```c
#define POOL_CONFIG_MINIMAL { \
    .packet_buffers = 16,      \
    .small_objects = 32,       \
    .dma_descriptors = 4,      \
    .temp_buffers = 8          \
}
```

#### Extended Memory Systems (≥1MB RAM)
```c
#define POOL_CONFIG_STANDARD { \
    .packet_buffers = 32,      \
    .small_objects = 64,       \
    .dma_descriptors = 8,      \
    .temp_buffers = 16         \
}
```

#### High-Performance Systems (≥4MB RAM)
```c
#define POOL_CONFIG_ENHANCED { \
    .packet_buffers = 64,      \
    .small_objects = 128,      \
    .dma_descriptors = 16,     \
    .temp_buffers = 32         \
}
```

### Dynamic Pool Adjustment

#### Load-Based Scaling
- Monitor allocation success rates
- Adjust pool sizes based on usage patterns
- Implement pool expansion during low usage
- Contract pools during memory pressure

#### Performance Metrics
- Track allocation latency
- Monitor pool hit rates
- Measure fragmentation levels
- Log memory pressure events

## Error Handling and Recovery

### Memory Allocation Failures

#### Immediate Actions
1. **Log allocation failure**: Record size, type, available memory
2. **Attempt alternative**: Try different memory tier
3. **Pool compaction**: Defragment if possible
4. **Graceful degradation**: Disable non-essential features

#### Recovery Strategies
1. **Buffer reuse**: Implement aggressive recycling
2. **Memory compaction**: Consolidate free blocks
3. **Feature reduction**: Disable optional functionality
4. **Emergency pools**: Reserve memory for critical operations

### Corruption Detection

#### Guard Pattern Protection
```c
#define GUARD_PATTERN_BEFORE 0xDEADBEEF
#define GUARD_PATTERN_AFTER  0xBEEFDEAD

typedef struct {
    uint32_t guard_before;
    // User data here
    uint32_t guard_after;
} guarded_buffer_t;
```

#### Validation Frequency
- **Pre-DMA**: Validate before hardware access
- **Post-DMA**: Check after DMA completion
- **Periodic**: Background validation every 10 seconds
- **On-demand**: Validate during allocation/free

### Memory Leak Prevention

#### Allocation Tracking
- Reference counting for shared buffers
- Ownership tracking per module
- Automatic cleanup on module unload
- Leak detection during development

#### Resource Limits
- Maximum allocations per module
- Total memory limits by pool type
- Timeout-based allocation cleanup
- Emergency memory reclamation

## Performance Optimization

### CPU-Specific Optimizations

#### Copy Operations
```asm
; 386+ optimized copy
copy_386_optimized:
    ; Use 32-bit operations
    db 66h
    rep movsw

; 286 optimized copy  
copy_286_optimized:
    ; Use 16-bit operations
    rep movsw

; 8086 fallback
copy_8086_fallback:
    ; Byte operations only
    rep movsb
```

#### Cache Awareness
- Align frequently accessed structures to cache lines
- Group related data to minimize cache misses
- Use cache-friendly access patterns
- Implement cache flush for DMA operations

### Memory Access Patterns

#### Sequential Access Optimization
- Prefetch next cache lines for large copies
- Use temporal locality for repeated operations
- Batch similar operations together
- Minimize random access patterns

#### Alignment Optimization
- Natural alignment for data types
- Padding structures to cache line boundaries
- Align stack frames for performance
- Use aligned moves when possible

## Configuration and Tuning

### Compile-Time Configuration

```c
// Memory tier preferences
#define MEM_PREFER_XMS          1
#define MEM_PREFER_UMB          0
#define MEM_DISABLE_CONVENTIONAL 0

// Buffer pool sizing
#define BUFFER_POOL_AGGRESSIVE  1  // Use more memory for performance
#define BUFFER_POOL_CONSERVATIVE 0 // Minimize memory usage

// Alignment settings
#define FORCE_CACHE_ALIGNMENT   1  // Always align to cache lines
#define CPU_DETECT_ALIGNMENT    0  // Use CPU-detected optimal alignment
```

### Runtime Tuning Parameters

#### Memory Thresholds
```c
typedef struct {
    size_t xms_reserve_kb;        // XMS memory to keep in reserve
    size_t conventional_limit_kb; // Max conventional memory to use
    uint16_t max_pools;           // Maximum number of pools
    uint16_t gc_threshold;        // Garbage collection trigger
} memory_tuning_t;
```

#### Performance vs. Memory Trade-offs
- **Performance mode**: Larger pools, more caching, alignment padding
- **Memory mode**: Smaller pools, minimal caching, tight packing
- **Balanced mode**: Adaptive based on available memory

## Testing and Validation

### Unit Test Requirements

#### Boundary Compliance Tests
```c
void test_64kb_boundary_compliance(void) {
    for (size_t size = 1; size <= 65536; size *= 2) {
        void* buffer = dma_buffer_alloc(size, 16, DMA_DEVICE_NETWORK, 0);
        assert(buffer != NULL);
        assert(validate_64kb_boundary(buffer, size));
        dma_buffer_free(buffer);
    }
}
```

#### Stress Testing
- Allocate maximum possible buffers
- Fragment memory and test allocation
- Rapid allocation/free cycles
- Multi-tier allocation patterns

### Integration Testing

#### Hardware Compatibility
- Test on various CPU types (286, 386, 486, Pentium)
- Validate with different memory configurations
- Test ISA and PCI card DMA operations
- Verify alignment on real hardware

#### System Integration
- Test with various DOS versions
- Validate with different XMS/UMB drivers
- Test memory pressure scenarios
- Verify graceful degradation

## Implementation Checklist

### Core Requirements ✓
- [x] Three-tier memory allocation (XMS → UMB → Conventional)
- [x] 64KB boundary compliance for all DMA buffers
- [x] ISA 16MB limit enforcement
- [x] CPU-optimized copy/move/set operations
- [x] ISR-safe memory operations (≤8μs critical sections)
- [x] Reference counting and leak detection
- [x] Pool-based allocation for performance

### Advanced Features ✓
- [x] Dynamic pool sizing based on usage
- [x] Memory corruption detection with guard patterns
- [x] Alignment optimization per CPU type
- [x] Cache-aware data structure layout
- [x] Emergency memory reclamation
- [x] Comprehensive error reporting

### Testing and Validation ✓
- [x] Unit tests for all boundary conditions
- [x] Stress testing under memory pressure
- [x] Hardware compatibility validation
- [x] Performance benchmarking
- [x] Memory leak detection
- [x] Corruption detection verification

## Compliance Matrix

| Requirement | Policy | Implementation | Status |
|-------------|--------|----------------|--------|
| DMA 64KB Boundary | Mandatory | Pre-allocation validation | ✓ |
| ISA 16MB Limit | Mandatory | Address range checking | ✓ |
| Alignment | CPU-dependent | Dynamic based on CPU type | ✓ |
| ISR Safety | ≤8μs CLI | Try-lock only in ISR | ✓ |
| Three-tier Strategy | Preferred order | XMS → UMB → Conventional | ✓ |
| Pool Management | Efficient allocation | Pre-allocated pools | ✓ |
| Error Handling | Graceful degradation | Multiple fallback levels | ✓ |
| Performance | CPU-optimized | Assembly routines per CPU | ✓ |

## Conclusion

This Memory Management Policy v1.0 establishes the foundation for reliable, high-performance memory operations in the 3Com Packet Driver modular architecture. The policy ensures:

- **DMA Safety**: 100% compliance with hardware requirements
- **Performance**: CPU-optimized operations for all target systems
- **Reliability**: Comprehensive error handling and recovery
- **Scalability**: Adaptive resource management
- **Compatibility**: Support for all DOS configurations from 286 through Pentium

**POLICY STATUS**: FROZEN - No changes permitted without full architecture review

---

**Document Control:**
- **Version**: 1.0 (FROZEN)
- **Approval**: Agent 11 - Memory Management
- **Review Date**: 2025-08-26 (Day 5 deadline)
- **Next Review**: Phase 2 completion