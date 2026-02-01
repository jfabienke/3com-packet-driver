# Known Limitations

**Last Updated:** 2026-01-28 02:30:00 UTC
**Phase:** 5 - Stub Symbol Cleanup Complete

This document lists known limitations in the current build that may affect functionality in specific scenarios.

---

## TX Buffer Management (Async DMA Path)

**File:** `src/asm/linkasm.asm:138-150`
**Symbol:** `free_tx_buffer`
**Status:** Stub (no-op)

### Impact
- `nicirq.asm` calls `free_tx_buffer` on TX_COMPLETE interrupt
- Without implementation, bus master DMA TX will leak buffers under load
- Does **not** affect PIO mode TX (synchronous path in api.c/hardware.c)

### Workaround
Use PIO mode for TX operations. The C code path (api.c, hardware.c) performs synchronous TX: allocate → send → immediate free.

### To Implement
1. Add `tx_pending` tracking in a shared data segment
2. Store buffer pointer when TX is initiated via DMA
3. Call `buffer_free_any()` in `free_tx_buffer` to release

---

## PCI BIOS Shim

**File:** `src/asm/linkasm.asm:198-205`
**Symbol:** `_pci_shim_handler_c`
**Status:** Returns 0xFFFF (not handled)

### Impact
- `pciisr.asm` intercepts INT 1Ah for PCI BIOS calls
- Current stub returns "not handled" causing chain to real BIOS
- PCI BIOS workarounds for broken BIOSes are **disabled**

### Workaround
PCI configuration works via direct mechanism 1/2 access in `pci_bios.c`. The shim is only needed for systems with broken PCI BIOS implementations.

### To Implement
Create C function matching pciisr.asm's expected interface:
```c
int far pci_shim_handler_c(pci_regs_t far *regs);
// Returns: 0xFFFF = not handled, else = handled status
```

---

## Packet API Entry (Segment Constraint)

**File:** `src/asm/linkasm.asm:157-158`
**Symbol:** `packet_api_entry`
**Status:** Near jump to `packet_driver_isr`

### Constraint
- Uses `jmp packet_driver_isr` (near jump, same segment)
- Requires `_TEXT` segment to remain under 64KB
- If `_TEXT` exceeds 64KB, jump will fail silently

### Monitoring
Check `build/3cpd.map` after linking:
```
grep "_TEXT" build/3cpd.map | head -5
```
Ensure segment size is under 0x10000 (65536) bytes.

### To Fix (if needed)
Either:
1. Use far jump: `jmp far packet_driver_isr`
2. Move `packet_api_entry` into `pktapi.asm` as an alias

---

## Summary Table

| Limitation | Affects | Severity | Workaround Available |
|------------|---------|----------|---------------------|
| free_tx_buffer stub | Async DMA TX | Medium | Use PIO mode |
| PCI shim disabled | Broken BIOS systems | Low | Direct PCI access works |
| Near jump constraint | Large builds | Low | Monitor map file |

---

## Phase 5 Completion Checklist

- [x] Packet API entry points to real ISR
- [x] Packet receive path functional (single & multi-NIC)
- [x] Hardware ASM modules integrated (hwpkt, hwcfg, hwcoord, hwinit, cacheops)
- [x] Cache operations trampolined to real ASM
- [x] 3C515 init for recovery path
- [x] Duplicate symbol warnings resolved
- [ ] Async TX buffer management
- [ ] PCI shim adapter for pciisr.asm
