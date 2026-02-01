# TODO Implementation Status

**Last Updated:** January 24, 2026
**Scope:** Completion status of identified TODOs and stubs from design review

---

## Implementation Status Summary

| Finding | Category | Status | Notes |
|---------|----------|--------|-------|
| DMA Safety Stubs | Critical | **COMPLETE** | Core stubs implemented in safestub.asm |
| DMA Cache Coherency | Critical | **COMPLETE** | sync_bounce_buffer implemented in dmasafe.c |
| VDS Pool Leak | Medium | **COMPLETE** | release_vds_buffer implemented in safestub.asm |
| VDS Pool Structure | Medium | **COMPLETE** | nicsafe.c refactored to match ASM layout |
| 3C515-TX HAL Stubs | Medium | **DOCUMENTED** | Marked as legacy, C vtable is authoritative |
| PCMCIA Subsystem | Low | **OUTSTANDING** | Intentionally skeletal |
| Diagnostics | Low | **OUTSTANDING** | Network output placeholders remain |

---

## 1. Completed Items

### 1.1 DMA Cache Coherency (sync_bounce_buffer)

**File:** `src/c/dmasafe.c`
**Status:** COMPLETE

Implementation includes:
- `cache_management_dma_prepare()` for CPU→Device transfers
- `cache_management_dma_complete()` for Device→CPU transfers
- `cache_flush_486()` WBINVD fallback
- Proper includes and extern declarations added

### 1.2 DMA Safety Stubs (safestub.asm)

**File:** `src/asm/safestub.asm`
**Status:** COMPLETE

| Stub | Implementation |
|------|----------------|
| `pio_transfer` | Calls `insw_handler` from dispatch table |
| `check_64kb_stub` | Pure detector: CF=1 on boundary cross |
| `release_vds_buffer` | Linear search through `_vds_pool`, clears in_use flag |
| `pio_fallback_stub` | Wrapper calling `pio_transfer` |

### 1.3 VDS Pool Integration

**File:** `src/c/nicsafe.c`
**Status:** COMPLETE

- `vds_pool_entry_t` struct padded to 16 bytes to match ASM `shl bx, 4` logic.
- Physical address moved to offset 0 to match ASM access.
- `vds_pool` renamed to `_vds_pool` and made global for linker visibility.

---

## 2. Outstanding Items (Low Priority)

### 2.1 PCMCIA Subsystem

**Status:** SKELETAL
Intentionally left as minimal detection stubs. PCMCIA support is experimental.

### 2.2 Diagnostics

**Status:** PLACEHOLDER
Network output for logging is not yet implemented. Console/Serial logging is functional.

### 2.3 3C515 Assembly HAL

**Status:** LEGACY/WRAPPER
The assembly HAL contains stubs, but the C implementation (`src/c/3c515.c`) is authoritative and complete. No action required.