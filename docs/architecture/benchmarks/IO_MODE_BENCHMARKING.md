# I/O Mode Benchmarking for 8086-Compatible Systems

**Created:** 2026-01-25 08:33:24
**Status:** FRAMEWORK READY - Awaiting real hardware/emulator results
**Source:** DESIGN_REVIEW_JAN_2026.md Recommendation 3

---

## 1. Overview

This document describes the benchmarking methodology and results for CPU-specific I/O optimization in the 3Com Packet Driver. The driver uses a dispatch table (`insw_handler`/`outsw_handler`) to select the optimal I/O routine based on detected CPU.

### I/O Handlers in nicirq.asm

| Handler | CPU | Description | Location |
|---------|-----|-------------|----------|
| `insw_8086_byte_mode` | 8086/8088 | Byte-at-a-time for <64 bytes | lines 1240-1252 |
| `insw_8086_unrolled` | 8086/8088 | 4x unrolled word I/O | lines 1065-1092 |
| `insw_286_direct` | 186/286 | Pure REP INSW | lines 1051-1054 |
| `insw_386_wrapper` | 386+ | REP INSD with word-count API | lines 1183-1202 |

---

## 2. Theoretical Cycle Analysis

### 2.1 8086/8088 Cycle Counts

**Standard loop (not unrolled):**
| Instruction | Cycles |
|-------------|--------|
| IN AX, DX | 15 |
| STOSW | 9 |
| LOOP | 17 |
| **Total** | **41 cycles/word** |

**4x Unrolled loop (insw_8086_unrolled):**
| Instructions | Cycles |
|--------------|--------|
| 4x (IN AX + STOSW) | 4 x 24 = 96 |
| SUB CX, 4 | 3 |
| CMP CX, 4 | 3 |
| JAE (taken) | 4 |
| **Total** | **103 cycles/4 words = 25.75 cycles/word** |

**Improvement:** 37% faster than standard loop

### 2.2 186/286 Cycle Counts

**REP INSW (insw_286_direct):**
| Component | Cycles |
|-----------|--------|
| Setup | 2 |
| Per word | ~4 |
| **Total** | **~4 cycles/word** |

### 2.3 386+ Cycle Counts

**REP INSD (insw_386_wrapper):**
| Component | Cycles |
|-----------|--------|
| Setup | 2 |
| Per dword | ~4 |
| **Total** | **~2 cycles/word (via 32-bit transfer)** |

---

## 3. Dispatch Table Overhead

The dispatch mechanism in nicirq.asm uses function pointers to avoid per-call CPU detection:

**Old approach (per-call detection):**
```asm
mov     bl, [current_cpu_opt]    ; 4+ cycles
test    bl, OPT_16BIT            ; 2 cycles
jnz     use_rep_insw             ; 4 cycles (taken)
; ... additional branching ...
; Total: ~38 cycles overhead per call
```

**New approach (dispatch table):**
```asm
call    [insw_handler]           ; ~8 cycles (indirect call)
; Total: 8 cycles overhead per call
```

**Savings:** 30 cycles per I/O operation = **~80% reduction in dispatch overhead**

---

## 4. 64-Byte Threshold Analysis

The driver uses a 64-byte threshold for byte vs word mode on 8086 (nicirq.asm:449-461):

```asm
cmp     cx, 64                  ; Small packet threshold
ja      c509_use_word_io        ; Large packet: use word I/O
call    insw_8086_byte_mode     ; Small packet on 8086
```

### Theoretical Crossover Point

**Byte mode:** 12 + 5 = 17 cycles/byte (IN AL + STOSB)
**Word mode:** 25.75 cycles/word / 2 bytes = 12.9 cycles/byte + loop overhead

For small packets, byte mode avoids:
- Word/byte alignment overhead
- Odd byte handling
- Additional register saves

**Expected crossover:** ~50-70 bytes depending on exact instruction mix

---

## 5. Benchmark Test Framework

### 5.1 Test File Location

`tests/performance/test_perf_io_modes.c`

### 5.2 Test Packet Sizes

Per DESIGN_REVIEW_JAN_2026.md:

| Size | Packet Type | Notes |
|------|-------------|-------|
| 28 bytes | Minimum ARP | Byte mode candidate |
| 40 bytes | TCP ACK | Byte mode candidate |
| 60 bytes | Min Ethernet | Near threshold |
| 64 bytes | Threshold | Crossover point |
| 128 bytes | Small data | Word mode optimal |
| 256 bytes | Medium | Word mode optimal |
| 512 bytes | UDP DNS | Word mode optimal |
| 1024 bytes | Large | Word/dword mode |
| 1514 bytes | Max Ethernet | Dword mode optimal |

### 5.3 Measurement Method

**PIT Timer (Portable):**
- Uses 8254 PIT channel 0 (1.19318 MHz)
- Latches counter, reads low/high bytes
- Calculates elapsed ticks (counter counts down)

**RDTSC (Pentium+):**
- Available on Pentium and later
- Provides cycle-accurate measurements
- Must calibrate against known reference

---

## 6. Testing Requirements

### 6.1 Emulator Testing

| Emulator | Purpose | Notes |
|----------|---------|-------|
| DOSBox | Functional testing | Not cycle-accurate |
| 86Box | Cycle-accurate | Recommended for benchmarks |
| PCem | Alternative | Supports older CPUs |

### 6.2 Real Hardware Testing

| CPU | System | Test Priority |
|-----|--------|---------------|
| 8086/8088 | IBM PC/XT clone | HIGH - Primary optimization target |
| 286 | IBM AT clone | MEDIUM - REP INSW baseline |
| 386DX | 386 system | MEDIUM - 32-bit baseline |
| 486DX | 486 system | LOW - Verify no regression |
| Pentium | Pentium system | LOW - Verify dispatch efficiency |

### 6.3 NIC Requirements

| NIC | I/O Mode | Test Type |
|-----|----------|-----------|
| 3C509B | PIO only | Primary I/O benchmark |
| 3C515-TX | DMA + PIO | DMA fallback testing |

---

## 7. Expected Results Template

### 7.1 CPU Matrix Results

```
=================================================================
              CPU I/O Mode Performance (cycles/byte)
=================================================================
| Packet Size | 8086 Byte | 8086 Word | 286 INSW | 386 INSD |
|-------------|-----------|-----------|----------|----------|
| 28 bytes    | TBD       | TBD       | N/A      | N/A      |
| 40 bytes    | TBD       | TBD       | TBD      | N/A      |
| 64 bytes    | TBD       | TBD       | TBD      | TBD      |
| 1514 bytes  | TBD       | TBD       | TBD      | TBD      |
=================================================================
```

### 7.2 Dispatch Overhead Results

```
=================================================================
              Dispatch Table Overhead
=================================================================
| Measurement          | Old (per-call) | New (dispatch) | Saved |
|---------------------|----------------|----------------|-------|
| Overhead (cycles)    | ~38            | ~8             | ~30   |
| Per MTU packet      | TBD            | TBD            | TBD   |
=================================================================
```

---

## 8. Validation Criteria

### 8.1 Success Metrics

1. **8086 Improvement:** Unrolled loop should be 25-40% faster than standard loop
2. **Dispatch Overhead:** Should be <10 cycles per call
3. **Threshold Validation:** 64-byte threshold should be within 10 bytes of measured crossover
4. **No Regressions:** 386+ modes should not be slower than 286 mode

### 8.2 Action Items

| Finding | Action |
|---------|--------|
| Threshold too high | Reduce to measured crossover point |
| Threshold too low | Increase to measured crossover point |
| Dispatch overhead >10 cycles | Investigate instruction sequence |
| 8086 unroll not 25%+ faster | Verify loop alignment |

---

## 9. Related Documentation

- [ARCHITECTURE_REVIEW.md](../../ARCHITECTURE_REVIEW.md) - Section 4.1 SMC Dispatch Table
- [DESIGN_REVIEW_JAN_2026.md](../../DESIGN_REVIEW_JAN_2026.md) - Recommendation 3
- [nicirq.asm](../../../src/asm/nicirq.asm) - I/O handler implementations

---

## 10. Revision History

| Date | Change |
|------|--------|
| 2026-01-25 | Initial document creation per design review |
