# Stage 0: Extension API Implementation Plan

## Definition of Done (DoD)
- **Scope**: AH=80h–9Fh introspection only
- **Resident Impact**: ≤45B (trampolines only)
- **ISR Changes**: ZERO
- **Risk Level**: LOW
- **Testing**: External test client validates each call

## Design Constraints

### Memory Budget (45 bytes maximum)
```assembly
; Resident trampoline (hot)
extension_dispatch:     ; 15 bytes
    cmp ah, 80h
    jb  original_api
    cmp ah, 9Fh
    ja  original_api
    call far ptr extension_handler_cold
    ret

; Jump table stub      ; 30 bytes (2 bytes per entry x 15 functions max)
extension_vectors:
    dw offset get_safety_state      ; AH=80h
    dw offset get_patch_stats       ; AH=81h
    dw offset get_memory_map        ; AH=82h
    ; ... up to 15 entries
```

### Cold Section Handlers (discardable)
All actual implementation in COLD section:
- Parameter validation
- Data gathering
- Response formatting
- Error handling

## API Specification

### AH=80h: Get Safety State
**Purpose**: Query current safety toggles  
**Input**: None  
**Output**:
- BX = Safety flags (16-bit)
  - Bit 0: PIO forced
  - Bit 1: DMA enabled  
  - Bit 2: Cache ops active
  - Bit 3: Boundary checks on
  - Bit 4: Patches verified
- CX = ISR stack free bytes
- DX = Patch count

### AH=81h: Get Patch Statistics  
**Purpose**: Query SMC patch metrics  
**Input**: None  
**Output**:
- BX = Patches applied
- CX = Maximum CLI ticks
- DX = Module count

### AH=82h: Get Memory Map
**Purpose**: Query resident memory usage  
**Input**: ES:DI = 64-byte buffer  
**Output**:
- Buffer filled with memory map
- AX = Total resident bytes

### AH=83h: Get Version Info
**Purpose**: Query driver version and capabilities  
**Input**: None  
**Output**:
- AX = Version (BCD, e.g., 0x0100 = 1.00)
- BX = Capabilities flags
- CX = Maximum packet size
- DX = NIC type (0=3C509B, 1=3C515)

### AH=84h: Get Performance Counters
**Purpose**: Query packet statistics  
**Input**: None  
**Output**:
- BX:CX = Packets received (32-bit)
- SI:DI = Packets transmitted (32-bit)

### AH=85h-8Fh: Reserved for future use

### AH=90h-9Fh: Vendor-specific (3Com)
Reserved for 3Com-specific extensions

## Implementation Steps

### Step 1: Add Extension Dispatcher (5 bytes resident)
```assembly
; In packet_api_smc.asm hot section
extension_check:
    cmp ah, 80h
    jae extension_dispatch  ; 2 bytes
    ; Original API continues
```

### Step 2: Create Jump Table (32 bytes resident)
```assembly
; Minimal jump table in hot section
extension_table:
    db 80h          ; First supported function
    db 84h          ; Last supported function
    dw offset cold_get_safety_state
    dw offset cold_get_patch_stats
    dw offset cold_get_memory_map
    dw offset cold_get_version_info
    dw offset cold_get_perf_counters
```

### Step 3: Implement Cold Handlers (0 bytes resident)
All handlers in `src/loader/extension_api.c`:
```c
#pragma code_seg("COLD_TEXT", "CODE")

int cold_get_safety_state(regs_t* r) {
    r->bx = (global_force_pio_mode ? 1 : 0) |
            (USE_3C515_DMA ? 2 : 0) |
            (CACHE_FLUSH_ENABLED ? 4 : 0);
    r->cx = isr_stack_free();
    r->dx = patch_count;
    return SUCCESS;
}
```

### Step 4: Create Test Client
```c
// test/test_extension_api.c
void test_safety_state() {
    union REGS r;
    r.h.ah = 0x80;
    int86(0x60, &r, &r);
    
    printf("Safety State:\n");
    printf("  PIO Forced: %s\n", (r.x.bx & 1) ? "Yes" : "No");
    printf("  Stack Free: %d bytes\n", r.x.cx);
    assert(r.x.dx == 12);  // Expected patch count
}
```

## Acceptance Criteria

### Functional Requirements
- [ ] All AH=80h-84h calls return valid data
- [ ] Invalid AH values pass through to original API
- [ ] No crashes or hangs under any input
- [ ] All responses match specification

### Performance Requirements  
- [ ] Zero additional ISR cycles
- [ ] <100 cycles for any extension call
- [ ] No impact on packet processing

### Memory Requirements
- [ ] ≤45 bytes resident growth
- [ ] Verified via: `grep "extension" build/3cpd.map | awk '{sum+=$3} END {print sum}'`
- [ ] No increase in stack usage

### Compatibility Requirements
- [ ] Works with existing packet driver clients
- [ ] No ABI breaks for AH=00h-7Fh
- [ ] Clean integration with INT 60h chain

## Risk Mitigation

### Risk 1: Table Alignment
**Issue**: Jump table might cross segment  
**Mitigation**: Force paragraph alignment
```assembly
align 16
extension_table:
```

### Risk 2: ABI Drift
**Issue**: Structure packing differences  
**Mitigation**: Use only primitive types, no structs in API

### Risk 3: Cold/Hot Reference
**Issue**: Hot code calling cold directly  
**Mitigation**: Use far call through vector table only

## Test Plan

### Unit Tests
1. Each extension function with valid inputs
2. Each extension function with invalid inputs
3. Boundary conditions (AH=7Fh, 80h, 9Fh, A0h)
4. Buffer overflow attempts
5. Null pointer checks

### Integration Tests
1. Run with mTCP after extension
2. Verify no impact on throughput
3. Check memory map unchanged for AH<80h
4. Stress test with rapid calls

### Validation Command
```bash
# Build with extension
make clean && make ENABLE_EXTENSION=1

# Check resident growth
size build/3cpd.exe | grep extension

# Run test suite  
./test/test_extension_api.exe

# Verify no ISR impact
./test/perf_baseline.exe
```

## Success Metrics
- Resident growth: ≤45 bytes ✅
- ISR cycles delta: 0 ✅
- Test pass rate: 100% ✅
- Risk level: LOW ✅

---
**Status**: READY TO IMPLEMENT  
**Estimated Time**: 2 hours  
**Dependencies**: None  
**Next**: Stage 1 Bus Master Test