# Stage 1 Go/No-Go Checklist

## Pre-Stage-1 Validation (10-Point Checklist)

### 1. ✅ Resident Memory Impact
**Target**: +85 bytes (41 code + 44 data)  
**Validation**:
```bash
# Before Stage 0
size build/3cpd.exe > baseline.txt

# After Stage 0
size build/3cpd.exe > stage0.txt
diff baseline.txt stage0.txt

# Check map file
grep -E "(extension|seqlock|snapshot)" build/3cpd.map
```
**PASS Criteria**: ≤100 bytes growth, no unexpected BSS

### 2. ✅ ISR Overhead Measurement
**Target**: <5 cycles added to non-vendor calls  
**Validation**:
```assembly
; Test on each CPU type
; 286: Use timer 0 reads
; 386+: Use RDTSC if available
test_dispatch_overhead:
    ; Measure 10000 AH=01h calls (bypasses extension)
    ; Measure 10000 AH=81h calls (uses extension)
    ; Delta should be <5% on all CPUs
```
**PASS Criteria**: 
- 286: <10 cycles overhead
- 386: <7 cycles overhead
- 486: <5 cycles overhead
- P5: <3 cycles overhead

### 3. ✅ CLI Duration Compliance
**Target**: All CLI sections <8 PIT ticks  
**Validation**:
```c
// In update_snapshot_safe
cli_start = read_pit();
CLI;
// ... update code ...
STI;
cli_duration = read_pit() - cli_start;
assert(cli_duration < 8);
```
**PASS Criteria**: No CLI section exceeds 8 ticks (~6.7μs)

### 4. ✅ Patch Health Verification
**Target**: All critical patches active  
**Validation**:
```bash
# Run patch verification
./test/verify_foundation.sh

# Check via vendor API
./test/exttest.com
# AH=81h should show patches_verified flag set
# AH=82h should show 12+ patches applied
```
**PASS Criteria**: 
- PATCH_3c515_transfer != NOP
- PATCH_dma_boundary_check != NOP
- Safety state shows patches OK (bit 1)

### 5. ✅ Reentrancy Safety
**Target**: Nested INT 60h calls don't corrupt state  
**Validation**:
```assembly
; Test nested call
test_reentrancy:
    mov ah, 81h         ; Safety state
    int 60h            ; First call
    push ax            ; Save result
    
    ; Nested call from "ISR context"
    pushf
    cli
    mov ah, 81h
    int 60h            ; Nested call
    sti
    popf
    
    pop bx             ; Original result
    cmp ax, bx         ; Should match
```
**PASS Criteria**: No corruption, consistent results

### 6. ✅ Buffer Edge Cases
**Target**: All invalid buffers handled safely  
**Validation Test Cases**:
```c
// NULL buffer
r.x.di = 0; sr.es = 0;
// Result: CF=1, AX=0x7003

// Buffer at segment boundary
r.x.di = 0xFFF8; sr.es = 0xB800;
// Result: Success or CF=1, no wrap

// Invalid segment (would GP fault)
r.x.di = 0; sr.es = 0xFFFF;
// Result: CF=1, no crash

// Zero-length request
r.x.cx = 0;
// Result: CF=1, AX=0x7001
```
**PASS Criteria**: No writes on error, correct error codes

### 7. ✅ AH Space Fuzzing
**Target**: Only 80h-9Fh handled, others pass through  
**Validation**:
```c
for (ah = 0x00; ah <= 0xFF; ah++) {
    r.h.ah = ah;
    int86(0x60, &r, &r);
    
    if (ah < 0x80 || ah > 0x84) {
        // Should either pass through or error appropriately
        assert(ah <= 0x10 || r.x.cflag);
    } else {
        // Our range - should handle
        assert(!r.x.cflag || r.x.ax >= 0x7000);
    }
}
```
**PASS Criteria**: Clean dispatch, no corruption

### 8. ✅ CPU Floor Compliance
**Target**: 286-safe assembly only  
**Validation**:
```bash
# Check for 386+ instructions
grep -E "(PUSHAD|POPAD|MOVZX|MOVSX|BSF|BSR|BT|SHLD|SHRD)" *.asm

# Check for 32-bit opcodes
objdump -d extension_api_final.obj | grep -E "e(ax|bx|cx|dx|si|di|sp|bp)"

# Build with 286 target
make CPU=286 clean all
```
**PASS Criteria**: No 386+ instructions, builds for 286

### 9. ✅ Snapshot Consistency
**Target**: No torn reads under concurrent updates  
**Validation**:
```c
// Hammer test
for (i = 0; i < 10000; i++) {
    // Reader thread
    call_vendor_api();
    validate_snapshot_fields();
    
    // Writer thread (simulated)
    if (i % 100 == 0) {
        trigger_snapshot_update();
    }
}
```
**PASS Criteria**: All fields remain valid, seqlock retries work

### 10. ✅ Health Status Byte
**Target**: Compact status in AH=80h response  
**Implementation**:
```assembly
; Add to AH=80h response
; DH = Health status
HEALTH_OK        equ 00h   ; All systems normal
HEALTH_PIO       equ 01h   ; PIO mode forced
HEALTH_DEGRADED  equ 02h   ; Running with limitations
HEALTH_FAILED    equ 0FFh  ; Critical failure
```
**PASS Criteria**: Tools can read single byte for quick status

## Stage 1 Proof Points

### Bus Master Validation Requirements

**Hardware Test Matrix**:
| CPU | Chipset | Required Tests |
|-----|---------|----------------|
| 486DX2/66 | EISA | Cache tier detection, WBINVD timing |
| Pentium 90 | PCI | Cache line size, boundary behavior |
| Pentium Pro | PCI | Out-of-order effects, coherency |

**Validation Sequence**:
1. Detect cache tier (L1 only, L1+L2, write-through/back)
2. Test 64KB boundary with pattern buffer
3. Measure WBINVD/CLFLUSH impact
4. Verify DMA descriptor alignment
5. Test concurrent DMA + CPU access

**Failure Triggers** (auto-revert to PIO):
- Cache tier detection fails
- Boundary test shows corruption
- WBINVD takes >100μs
- Any data mismatch in patterns
- Descriptor alignment violation

### Three-Layer DMA Policy

```c
typedef struct {
    // Layer 1: Runtime user control
    uint8_t user_enable;        // INT 60h toggle
    
    // Layer 2: Validation status
    uint8_t test_passed;        // Bus master test result
    uint8_t cache_tier_known;   // Cache detection worked
    uint8_t boundary_safe;      // 64KB test passed
    
    // Layer 3: Persistent state
    uint8_t last_known_safe;    // Survives reboot
    uint32_t failure_count;     // Increment on errors
    uint32_t success_count;     // Successful transfers
} dma_policy_t;

// Enable only when ALL conditions met
bool can_use_dma() {
    return policy.user_enable &&
           policy.test_passed &&
           policy.cache_tier_known &&
           policy.boundary_safe &&
           policy.last_known_safe &&
           policy.failure_count < 3;
}
```

### Rollback Mechanism

```assembly
force_pio_rollback:
    ; Set kill switch
    or word ptr [safety_snapshot], 8000h
    
    ; Clear DMA enables
    and word ptr [version_snapshot+2], 0FFFDh
    or word ptr [version_snapshot+2], 0001h
    
    ; Update health status
    mov byte ptr [health_status], HEALTH_DEGRADED
    
    ; Log event
    call log_dma_rollback
    
    ; Update via seqlock
    call update_snapshot_safe
    ret
```

## CI/Build Gates

```makefile
# Size gate in Makefile
check-size: $(TARGET)
    @resident=$$(size $(TARGET) | awk '/text/{print $$1}'); \
    if [ $$resident -gt 8192 ]; then \
        echo "FAIL: Resident $$resident > 8KB limit"; \
        exit 1; \
    fi
    @echo "PASS: Resident $$resident bytes"

# Stage gate
stage1-gate: check-size test-extension test-safety
    @echo "Stage 0 complete, ready for Stage 1"
```

## Final Go/No-Go Decision

### GO Criteria ✅
- [ ] All 10 checklist items PASS
- [ ] EXTTEST.COM shows 100% pass
- [ ] Memory impact ≤100 bytes
- [ ] No 386+ instructions found
- [ ] Health status byte implemented

### NO-GO Criteria ❌
- [ ] Any checklist item FAIL
- [ ] ISR overhead >10 cycles
- [ ] CLI duration >8 ticks
- [ ] Buffer overflow detected
- [ ] Reentrancy corruption

---
**Status**: READY FOR FINAL VALIDATION  
**Next**: Run checklist, fix any issues, then proceed to Stage 1