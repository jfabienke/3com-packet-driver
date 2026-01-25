# Design: Safety Stubs Completion (safestub.asm)

**Date:** January 25, 2026 08:33:24
**Status:** IMPLEMENTED

## Implementation Status (2026-01-25)

| Stub | Status | Description |
|------|--------|-------------|
| `release_vds_buffer` | DONE | Linear search, clears in_use flag |
| `check_64kb_stub` | DONE | Pure detector, returns CF=1 on boundary cross |
| `pio_fallback_stub` | DONE | Calls dispatch table handler |
| `pio_transfer` | DONE | Uses insw_handler from nicirq.asm |
| `allocate_bounce_buffer` | DONE | Calls _dma_get_rx_bounce_buffer from C |

---

## 1. Problem Statement
The `src/asm/safestub.asm` file contains critical safety hooks for DMA operations (VDS locking, bounce buffers) that are currently unimplemented (`TODO` stubs). This renders the "Defensive Programming" architecture ineffective for DMA boundary violations and V86 mode safety.

## 2. Requirements

1.  **VDS Pool Management:** Implement `release_vds_buffer` to prevent resource leaks.
2.  **Bounce Buffer Logic:** Implement `use_bounce_for_64kb` to handle DMA boundary crossings transparently.
3.  **Data Sharing:** Establish a consistent interface for `_vds_pool` and `_bounce_pool` between C and Assembly.

## 3. Data Structures

To ensure compatibility, we define the memory layout expected by `safestub.asm`.

### 3.1 VDS Pool Entry (16 bytes)
The ASM code (`get_vds_buffer`) assumes a 16-byte structure size (`shl bx, 4`).
```asm
offset 0:  physical_addr (4 bytes)
offset 4:  virtual_addr  (4 bytes / far ptr)
offset 8:  size          (4 bytes)
offset 12: handle        (2 bytes)
offset 14: flags         (1 byte) - Bit 0: in_use
offset 15: reserved      (1 byte)
```
**Constraint:** The C definition in `vdssafe.c` must match this layout or the ASM must be updated.

### 3.2 Bounce Buffer Entry (16 bytes)
```asm
offset 0:  physical_addr (4 bytes)
offset 4:  linear_addr   (4 bytes) - Segment:Offset for real mode
offset 8:  size          (4 bytes)
offset 12: reserved      (2 bytes)
offset 14: flags         (1 byte) - Bit 0: in_use
offset 15: reserved      (1 byte)
```

## 4. Implementation Logic

### 4.1 `release_vds_buffer`
**Input:** `DX:AX` = Physical Address of buffer to release (or `BX` index if changed)
**Logic:**
1.  Iterate through `_vds_pool`.
2.  Compare `[entry + 0]` (physical_addr) with `DX:AX`.
3.  If match, clear `[entry + 14]` (in_use flag).
4.  If no match, log error (optional/debug).

### 4.2 `use_bounce_for_64kb`
This is called when `check_64kb_stub` detects a boundary crossing.
**Input:**
*   `DX:AX` = Original Physical Address
*   `CX` = Length
**Output:**
*   `DX:AX` = New Physical Address (Bounce Buffer)
*   `CF` = 0 (Success) or 1 (Failure)

**Logic:**
1.  Call `get_bounce_buffer` to find a free slot.
    *   If fail: Return `CF=1` (force PIO fallback).
2.  **TX Path (Copy to Bounce):**
    *   Since this stub is inside the DMA setup path, we need access to the source data.
    *   *Challenge:* `check_64kb_stub` only gets physical address. It doesn't inherently know the virtual source address to copy *from*.
    *   *Solution:* The caller of `check_64kb_stub` (the SMC patch site) typically has the source `DS:SI` or `ES:DI` available. The patch site might need adjustment to pass the virtual source pointer.
3.  **RX Path:**
    *   Just return the bounce buffer address.
    *   Must record the mapping so the ISR can copy back later.

### 4.3 `get_bounce_buffer`
1.  Iterate `_bounce_pool`.
2.  Find entry with `in_use == 0`.
3.  Set `in_use = 1`.
4.  Return physical address.

## 5. C-Side Integration

We need to export the pools from C.

**File:** `src/c/safety_data.c` (New file)
```c
#include "safety_defs.h"

/* Global pools visible to ASM */
vds_pool_entry_t _vds_pool[VDS_POOL_SIZE];
bounce_pool_entry_t _bounce_pool[BOUNCE_POOL_SIZE];
```

## 6. Revised `safestub.asm` Logic

### `use_bounce_for_64kb` Revised Strategy
Because `check_64kb_stub` is inserted at the "Physical Address Check" stage, converting the physical address *back* to a virtual address to perform a copy is difficult/impossible (aliasing).

**Alternative:** The bounce buffer logic should be invoked *earlier* or explicitly by the driver when setting up the descriptor, where the virtual address is known.

If we stick to the stub:
1.  The stub allocates the bounce buffer.
2.  It returns the bounce physical address to the hardware.
3.  It **MUST** perform the copy.
    *   If it's TX, it needs the source.
    *   If the patch site doesn't provide source `seg:off`, this stub cannot work for TX.

**Conclusion:** `check_64kb_stub` is best used for **RX** (where the card simply DMA's to the bounce buffer) or for validating **TX** safety (failing if crossed, triggering PIO fallback). For TX bounce buffering, the driver should likely handle it explicitly before calling the hardware setup.

**Recommendation for Stub:**
For `check_64kb_stub`, if it crosses:
1.  Return Failure (`STC`).
2.  Let the caller handle the fallback (e.g., to PIO or explicit bounce buffer code).
3.  Do *not* try to transparently fix it inside this tiny stub for TX, as the data copy complexity is too high.

For RX, transparent bounce is possible if we can queue the "copy-back" operation.

## 7. Plan of Action

1.  **Implement `release_vds_buffer`**: Simple linear search and free.
2.  **Simplify `check_64kb_stub`**: Make it a pure detector. If it fails, the patch site jumps to `pio_fallback_stub`.
3.  **Implement `pio_fallback_stub`**: This calls `pio_transfer`.
4.  **Implement `pio_transfer`**: This needs to replicate the `rep outsw` logic locally or call back into the driver's PIO routine.

This simplified approach ensures safety (by rejecting unsafe DMAs) without requiring a complex transparent bounce mechanism inside the ISR/hot-path.
