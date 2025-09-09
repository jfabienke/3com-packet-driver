# Strategic Decisions for Stage 0-3

## Questions & Recommendations

### 1. 3C515 DMA Enabling Policy

**Question**: After Stage 1, enable only behind a runtime toggle plus sticky "last known safe" flag?

**Recommendation**: **YES - Implement Three-Layer Safety**

```c
typedef struct {
    uint8_t runtime_enable;      // User can toggle via INT 60h
    uint8_t validation_passed;   // Set by Stage 1 bus master test  
    uint8_t last_known_safe;     // Persistent across reboots
} dma_enable_state_t;

// DMA enabled only when ALL three are true:
bool can_use_dma() {
    return state.runtime_enable && 
           state.validation_passed && 
           state.last_known_safe;
}
```

**Implementation**:
1. Stage 1 sets `validation_passed` after successful test
2. First successful DMA operation sets `last_known_safe`
3. User can disable via `runtime_enable` for testing
4. Any DMA failure clears `last_known_safe`

**Persistence Options**:
- EEPROM offset 0x40-0x43 (if available)
- File: C:\3CPKT\DMA.SAF
- Environment variable: SET 3C515_DMA_SAFE=1

### 2. SMC Registry vs Externs

**Question**: Do you prefer a small module registry (more future-proof) over extern header aliases?

**Recommendation**: **HYBRID - Registry for Discovery, Externs for Performance**

```c
// Module registry for future expansion (cold section)
typedef struct {
    char name[16];
    module_header_t* header;
    uint16_t flags;
} module_registry_entry_t;

module_registry_entry_t module_registry[] = {
    {"packet_api", &packet_api_module_header, MODULE_HOT},
    {"nic_irq",    &nic_irq_module_header,    MODULE_HOT},
    {"hardware",   &hardware_module_header,   MODULE_HOT},
    {NULL, NULL, 0}
};

// But keep direct externs for hot path (no indirection)
extern module_header_t packet_api_module_header;  // Fast
```

**Benefits**:
- Registry allows runtime module discovery
- Registry enables dynamic module loading (future)
- Direct externs avoid pointer indirection in hot path
- Best of both worlds

### 3. Copy-Break/Mitigation Strategy

**Question**: Keep off until DMA is validated, or run A/B with perf tool?

**Recommendation**: **STAGED ENABLEMENT - Off by Default, A/B Testing in Stage 2**

**Phase 1** (Current - Stage 0):
```c
#define COPY_BREAK_ENABLED 0  // Completely off
#define INTERRUPT_MITIGATION_OFF
```

**Phase 2** (Stage 1 - After DMA validation):
```c
#define COPY_BREAK_ENABLED 1
#define COPY_BREAK_THRESHOLD 256  // Conservative
// Still no interrupt mitigation
```

**Phase 3** (Stage 2 - With diagnostics):
```c
// A/B testing via runtime toggle
if (perf_test_mode) {
    copy_break_threshold = test_config.threshold;
    interrupt_coalesce = test_config.coalesce_us;
}
// Collect metrics for both modes
```

**Phase 4** (Stage 3 - Production):
```c
// Optimal values from A/B testing
#define COPY_BREAK_THRESHOLD 128  // Tuned value
#define INTERRUPT_COALESCE_US 100 // 100μs batching
```

## Additional Strategic Decisions

### 4. Size Budget Enforcement

**Decision**: **HARD FAIL in CI if resident >8KB**

```makefile
check-size: $(TARGET)
    @size=$(shell size $(TARGET) | grep resident); \
    if [ $$size -gt 8192 ]; then \
        echo "ERROR: Resident size $$size exceeds 8KB limit!"; \
        exit 1; \
    fi
```

### 5. ISR Latency Guards

**Decision**: **Add PIT-based measurement in debug builds**

```assembly
%ifdef DEBUG
    rdtsc           ; Start timestamp
    mov [isr_start_tsc], eax
%endif
    ; ... ISR code ...
%ifdef DEBUG
    rdtsc           ; End timestamp  
    sub eax, [isr_start_tsc]
    cmp eax, MAX_ISR_CYCLES
    ja  log_slow_isr
%endif
```

### 6. Patch Site Checksum

**Decision**: **Implement in Stage 1 as part of bus master test**

```c
uint16_t calc_patch_checksum() {
    uint16_t sum = 0;
    sum += checksum_region(&PATCH_3c515_transfer, 5);
    sum += checksum_region(&PATCH_dma_boundary_check, 5);
    sum += checksum_region(&PATCH_cache_flush_pre, 5);
    return sum;
}

// Store at init, verify before DMA enable
uint16_t expected_checksum = 0x1234;  // Calculated at patch time
```

### 7. Multi-NIC Failover Policy

**Decision**: **Simple Primary/Backup in Stage 3C, Load Balance in Future**

Stage 3C (Simple):
```c
if (primary_nic_failed) {
    send_garp(backup_nic);      // Gratuitous ARP
    active_nic = backup_nic;    // Switch over
    log_failover_event();       // Record for diagnostics
}
```

Future (v2.0):
- Round-robin load balancing
- Flow affinity tracking  
- Automatic recovery when primary returns

## Risk Management Matrix

| Stage | Risk | Probability | Impact | Mitigation |
|-------|------|-------------|--------|------------|
| 0 | API breaks packet driver | Low | High | Extensive testing with mTCP |
| 1 | False positive on DMA test | Medium | Medium | Test on real hardware only |
| 2 | Diagnostics impact perf | Low | Medium | Compile-time disable in prod |
| 3A | Seqlock causes stalls | Low | High | Extensive testing, fallback |
| 3B | XMS migration corrupts | Medium | High | Full state validation |
| 3C | GARP not seen by switch | Medium | Low | Send multiple GARPs |

## Go/No-Go Criteria for Each Stage

### Stage 0: Extension API
- **GO if**: Resident growth ≤45B AND zero ISR impact
- **NO-GO if**: Any ABI break OR ISR cycles increase

### Stage 1: Bus Master Test  
- **GO if**: Test passes on 3+ real machines AND PIO fallback works
- **NO-GO if**: Any data corruption OR false positives

### Stage 2: Diagnostics
- **GO if**: Tools work without driver changes OR minimal cold code
- **NO-GO if**: Any hot path instrumentation required

### Stage 3: Advanced Features
- **GO if**: Each sub-stage individually stable AND under budget
- **NO-GO if**: Total resident >8KB OR ISR >120μs

---
**Status**: STRATEGY DEFINED  
**Decisions**: DOCUMENTED  
**Next Action**: Implement Stage 0 with registry hybrid approach