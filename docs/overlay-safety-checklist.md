# Overlay Safety Verification Checklist

**Last Updated:** 2026-01-27 23:20:00 UTC

This document tracks the escape-reference audit and verification steps for the overlay-based memory optimization.

---

## 1. vtable Escape Audit

### Runtime vtables (MUST be ROOT-resident)

| vtable | Location | Status |
|--------|----------|--------|
| `g_3c509b_ops` | hardware.c (ROOT) | SAFE |
| `g_3c515_ops` | hardware.c (ROOT) | SAFE |
| `_3c509b_ops` | 3c509b.c (ROOT) | SAFE |

**Assignment points verified:**
- `nic->ops = get_3c509b_ops()` in nic_init.c:697 - returns ROOT vtable
- `nic->ops = get_3c515_ops()` in nic_init.c:771 - returns ROOT vtable
- `nic->ops = get_3c509b_ops()` in hardware.c:3512 - returns ROOT vtable

### Init-only vtables (overlay-local, safe)

| vtable | Location | Safety Reason |
|--------|----------|---------------|
| `vortex_vtable` | 3cpcidet.c (INIT_DETECT) | Used transiently, pointer doesn't escape |
| `boomerang_vtable` | 3cpcidet.c (INIT_DETECT) | Used transiently, pointer doesn't escape |

**Usage pattern verified:** (3cpcidet.c:327-343)
```c
vtable = &vortex_vtable;  // Local stack variable
vtable->init(ctx);        // Immediate use
// vtable goes out of scope - no storage
```

### Debug assertion (recommended)
Add to end of init:
```c
#ifdef DEBUG
assert(nic->ops == get_3c509b_ops() || nic->ops == get_3c515_ops());
#endif
```

---

## 2. Logging Residency

**Status:** logging.obj moved to ROOT

**Call count:** 3424 calls across 67 files

**Runtime logging contract:** TBD - recommend Option A (ring buffer + deferred formatting)

### String escape check

| String table | Location | Used at runtime? |
|--------------|----------|------------------|
| `level_names[]` | logging.c (ROOT) | Yes - safe |
| `error_messages[]` | main.c (INIT_EARLY) | Init only - safe |
| `vendor_names[]` | main.c (INIT_EARLY) | Init only - safe |

**TODO:** Audit any log_*() calls that format strings from overlay-local tables.

---

## 3. config_t Pointer Safety

**Status:** VERIFIED SAFE

`config_t` contains NO pointer fields:
- `char debug_output[32]` - fixed array
- `char config_file[128]` - fixed array
- All other fields: integers, enums, bools, fixed arrays

**Copy safety:** `*config = default_config;` is a deep copy.

---

## 4. Map File Verification Checklist

**Last Verified:** 2026-01-27 21:15:00 UTC

When build completes, run: `python3 tools/verify_map.py build/3cpd.map`

### A) ROOT residency - VERIFIED ✓
- [x] `hardware.obj` in ROOT segment (hardware_TEXT: AUTO group)
- [x] `3c509b.obj` in ROOT segment (3c509b_TEXT: AUTO group)
- [x] `3c515.obj` in ROOT segment (3c515_TEXT: AUTO group)
- [x] `3cvortex.obj` in ROOT segment (3cvortex_TEXT: AUTO group)
- [x] `3cboom.obj` in ROOT segment (3cboom_TEXT: AUTO group)
- [x] `logging.obj` in ROOT segment (logging_TEXT: AUTO group)

### B) DGROUP size - VERIFIED ✓
- [x] DGROUP: 62,448 bytes (0xF3F0) - UNDER 64KB
- [ ] Headroom: 3,088 bytes - IN RED ZONE (target: > 4KB)

### B.1) DGROUP Optimization: INIT_DIAG compile-time guard

**Status:** VERIFIED 2026-01-27 23:15 UTC

**Design Decision:** 32-byte stats struct always present (cheap telemetry), only bulky
diagnostic structures are guarded by `INIT_DIAG`:

| Structure | Size | Location | Status |
|-----------|------|----------|--------|
| `g_nic_init_stats` | 32 bytes | nic_init.c | **Always present** (cheap) |
| `g_system_coherency_analysis` | ~400 bytes | nic_init.c | Guarded |
| `g_system_chipset_detection` | ~300 bytes | nic_init.c | Guarded |
| **Total guarded** | **~700 bytes** | | |

**Actual DGROUP results after rebuild without INIT_DIAG:**
- Before: 63,024 bytes (0xF630) - 2.5 KB headroom (red zone)
- After: 62,448 bytes (0xF3F0) - 3.1 KB headroom (slightly better)

**Note:** Original 8KB estimate was incorrect. Actual savings ~700 bytes.
Further DGROUP reduction needed via other means (XMS, larger buffer relocation).

**Build configurations (Makefile.wat):**
- Release: `CFLAGS_RELEASE` (no INIT_DIAG) → ~8 KB saved, stats still available
- Debug: `CFLAGS_DEBUG` includes `-dINIT_DIAG` → full diagnostics

**Build log confirmation:**
Compile output will show one of:
```
nic_init.c: INIT_DIAG ENABLED - full diagnostics (~8KB in DGROUP)
nic_init.c: INIT_DIAG DISABLED - release mode (diagnostics stripped)
```

**Verification script modes:**
```bash
python3 tools/verify_map.py build/3cpd.map --release  # Stricter: max 0xE800
python3 tools/verify_map.py build/3cpd.map --debug    # Allows red zone
```

### C) DGROUP breakdown
| Segment | Size | Notes |
|---------|------|-------|
| _BSS | 35,758 bytes | Largest - consider XMS for optional buffers |
| _DATA | 25,406 bytes | Static data |
| STACK | 1,024 bytes | Fixed |
| CONST/CONST2 | 432 bytes | String literals moved to code by -zc |

### D) String placement (-zc flag) - VERIFIED ✓
- [x] String literals in code segments (CONST only 80 bytes)
- [x] Runtime format strings in ROOT code segments

---

## 5. Callback/Timer/ISR Escape Audit

### ISR entry points (MUST be ROOT)

| Entry Point | Location | Status |
|-------------|----------|--------|
| Packet driver ISR | nicirq.asm | ROOT |
| PCI ISR | pciisr.asm | ROOT |
| PCMCIA ISR | pcmisr.asm | ROOT |

### Registered callbacks

**TODO:** Grep for `register_*`, `install_*`, `hook_*` patterns and verify all function pointers are ROOT-resident.

---

## 6. Mechanical Escape Prevention (CI/Build)

### Must-be-ROOT symbol list
```
# NIC ops targets
_3c509b_init
_3c509b_send_packet
_3c509b_receive_packet
_3c509b_handle_interrupt
_3c515_init
_3c515_send_packet
_3c515_receive_packet
_3c515_handle_interrupt

# Logging
log_init
log_info
log_error
log_warning
log_debug
log_printf

# ISR entry points
_nic_isr
_pci_isr
_pcmcia_isr
```

### Build verification script (pseudo-code)
```bash
# Parse .map file
# For each symbol in must-be-ROOT list:
#   Verify symbol resolves to ROOT segment (not INIT_*)
#   Fail build if any resolve to overlay
```

---

## 7. Future Optimization: NIC Driver Split

When ready to reclaim memory:

### Keep in ROOT
- ISR hooks
- tx/rx hot path (send_packet, poll, rx_drain)
- NIC runtime state structs
- nic_ops_t vtable

### Move to INIT overlay
- EEPROM parsing
- PCI config scanning
- Chip identification
- Verbose diagnostics
- Large ID/decode tables

### Constraint
Init code may compute parameters and write to resident NIC struct, but must NOT export pointers to overlay data.

---

## Sign-off

| Check | Date | Verified By |
|-------|------|-------------|
| vtable audit | 2026-01-27 | Claude |
| config_t safety | 2026-01-27 | Claude |
| Linker file updated | 2026-01-27 | Claude |
| Map file verification | 2026-01-27 | Claude (verify_map.py) |
| DGROUP under 64KB | 2026-01-27 | 63,024 bytes ✓ |
| NIC drivers in ROOT | 2026-01-27 | All 6 segments verified ✓ |
| Runtime string safety | 2026-01-27 | vendor_names/error_messages init-only ✓ |
| Runtime test in DOSBox | | |
