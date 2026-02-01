# Codebase Review: TODOs, Stubs, and Placeholders

**Date:** January 24, 2026
**Reviewer:** Gemini CLI Agent

## Executive Summary

A scan of the codebase has revealed a significant number of `TODO` items, stub implementations, and placeholders. While some are minor "future extension" points, several represent critical gaps in safety and functionality, particularly in the DMA safety and PCMCIA subsystems.

## 1. Critical Safety Gaps

### DMA Safety Stubs (`src/asm/safestub.asm`)
The safety stubs intended to be patched into the hot path for DMA operations are incomplete:
*   `vds_unlock_stub`: Calls `release_vds_buffer` which is a `TODO`.
*   `bounce_tx_stub` / `bounce_rx_stub`: Logic relies on `get_bounce_buffer` and `release_bounce_buffer` which are minimal stubs or `TODO`s.
*   `check_64kb_stub`: Calls `use_bounce_for_64kb` which is a `TODO`.
*   `pio_fallback_stub`: Calls `pio_transfer` which is a `TODO`.

**Impact:** The sophisticated "SMC patching" architecture for DMA safety is currently patching in code that may not fully function or properly manage resources. The 64KB boundary check, if triggered, will try to use a bounce buffer mechanism that is unimplemented.

### DMA Cache Coherency (`src/c/dmasafe.c`)
*   `sync_bounce_buffer`: Contains comments "In protected mode, would flush/invalidate cache here" but no implementation code.

**Impact:** Bounce buffer operations may suffer from the same cache coherency issues as the PIO path (now fixed for 3C509B) if the bounce buffers are cached.

## 2. PCMCIA Subsystem

The PCMCIA support appears to be largely skeletal:
*   `src/c/pcmssbe.c`: "Socket Services backend - minimal detection stubs". Presence check returns 0 by default.
*   `src/c/pcmpebe.c`: "Power control stubs", "Map attribute memory ... (best-effort stub)".
*   `src/c/pcmmgr.c`: "Enable power and assign default resources (placeholder)".

**Impact:** PCMCIA/CardBus functionality is likely non-functional beyond basic detection logic.

## 3. Hardware HAL Stubs

### 3C515-TX Assembly HAL (`src/asm/hardware.asm`)
There is a block of assembly functions labeled "Remaining 3C515 HAL vtable functions (stubs for now)" which simply return `HAL_SUCCESS`:
*   `asm_3c515_configure_media`
*   `asm_3c515_enable_interrupts`
*   `asm_3c515_start_transceiver`
*   `asm_3c515_stop_transceiver`
*   `asm_3c515_get_statistics`
*   `asm_3c515_set_multicast`
*   `asm_3c515_set_promiscuous`

**Impact:** 3C515-TX support via the assembly HAL is incomplete. Functions like multicast filtering and promiscuous mode will report success but do nothing.

## 4. Other Functional Gaps

*   **VDS Pool:** `src/asm/safestub.asm` has a `TODO` for `release_vds_buffer`, implying VDS buffers (used for DMA in V86 mode) are leaked.
*   **Hardware Checksum:** `src/c/hwchksm.c` contains a placeholder for future NIC support.
*   **Diagnostics:** `src/c/diag.c` and `src/c/logging.c` have placeholders for "Network output".

## 5. Build & Test Artifacts

*   `test/dos_test_stub.c`: A test stub exists and is referenced in build scripts.
*   `src/c/hwstubs.c`: Contains temporary stub implementations for hardware functions to allow refactored boot sequence to compile.

## Recommendations

1.  **Prioritize DMA Safety Stubs:** The `TODO` items in `src/asm/safestub.asm` should be the next highest priority. A boundary check that patches in a broken fallback is dangerous.
2.  **Verify 3C515 Functionality:** The assembly HAL stubs suggest 3C515 support might be less complete than the C implementation (`src/c/3c515.c`). The relationship between these two needs clarification.
3.  **PCMCIA Review:** Clearly mark PCMCIA support as "Experimental/Incomplete" in documentation given the extensive stubbing.
