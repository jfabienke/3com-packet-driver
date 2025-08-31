# Review: Defensive TSR Programming Coverage

This report analyzes the implementation of defensive programming techniques in the 3Com Packet Driver source code, comparing it against the best practices outlined in `refs/dos-references/defensive-tsr-programming.md`.

## Executive Summary

The project contains two parallel implementations of defensive programming:

1.  A **highly robust, textbook implementation** in `src/asm/tsr_common.asm` that appears to be a self-contained library of best practices, covering nearly all advanced techniques (private stacks, deferred processing, DOS re-entrancy checks).
2.  A **less robust, but functional implementation** in the main, integrated driver files (`main.asm`, `nic_irq.asm`, `packet_api.asm`).

The main driver correctly implements some essential safety features like segment hygiene and uses standard APIs for presence checks and TSR termination. However, it lacks several critical defensive layers that are recommended in the reference document and are ironically present in the unused `tsr_common.asm` file. The driver appears to be in a state where core functionality was implemented, but the advanced defensive programming patterns, while designed, have not yet been fully integrated.

---

## Detailed Coverage Analysis

### 1. Minimal ISR Work & Deferred Processing

*   **Concept:** Keep hardware interrupt service routines (ISRs) extremely short. Defer heavy processing to a safer context like the `INT 28h` (DOS Idle) or `INT 1Ch` (Timer Tick) hooks.
*   **Coverage Analysis:** **Partial.**
    *   The file `src/asm/tsr_common.asm` explicitly implements this pattern. It defines an `int28_handler` that checks a `work_pending` flag and processes a `work_queue`. This is a classic and robust implementation of deferred processing.
    *   However, the main C-based driver code (`3c509b.c`, `3c515.c`) and the corresponding assembly (`nic_irq.asm`) do **not** appear to use this deferred work queue. The `nic_irq.asm` handlers (`nic_irq_handler_3c509b`, `nic_irq_handler_3c515`) perform their work directly within the hardware ISR context.
    *   **Conclusion:** The foundational logic exists in `tsr_common.asm`, but it is not integrated into the primary driver logic.

---

### 2. DOS Re-entrancy Gate (InDOS + CritErr)

*   **Concept:** Never call a DOS function (`int 21h`) when DOS is already busy. This is checked via the `InDOS` and `Critical Error` flags.
*   **Coverage Analysis:** **Present but Not Integrated.**
    *   `src/asm/tsr_common.asm` correctly implements this. The `initialize_tsr_defense` procedure calls `get_indos_address` and `get_critical_error_flag` to retrieve and store the pointers to these critical DOS flags. The `CHECK_DOS_COMPLETELY_SAFE` macro uses these pointers.
    *   The C code and main assembly files do not appear to use this mechanism. `main.asm` calls `int 21h` for printing strings and TSR termination, but this is during the initial, non-resident setup phase. The hardware ISRs in `nic_irq.asm` do not call DOS functions, which is good, but they also don't use the formal `InDOS` check.

---

### 3. Known-Good Private Stack

*   **Concept:** Immediately switch to a private, driver-owned stack upon entering any ISR or API handler to avoid relying on the caller's potentially small or corrupt stack.
*   **Coverage Analysis:** **Present but Not Integrated.**
    *   `src/asm/tsr_common.asm` defines a `driver_stack` and a `SAFE_STACK_SWITCH` macro that correctly saves the caller's stack pointer (`SS:SP`) and loads the driver's private stack.
    *   The primary interrupt handlers in `nic_irq.asm` and `packet_api.asm` do **not** perform a stack switch. They operate directly on the caller's stack. This is a significant deviation from the defensive reference.

---

### 4. Segment Hygiene (DS/ES Normalization)

*   **Concept:** Do not trust the `DS` and `ES` segment registers provided by the caller. Immediately set them to the driver's own data segment.
*   **Coverage Analysis:** **Present and Correctly Implemented.**
    *   This is one area with good coverage. The interrupt handlers in `nic_irq.asm` (`nic_irq_handler_3c509b`, `nic_irq_handler_3c515`) and `packet_api.asm` (`packet_int_handler`) correctly push the current `DS`/`ES`, and then load their own data segment (`push cs`, `pop ds`). This is a critical safety measure that has been implemented correctly.

---

### 5. Critical Sections (pushf/cli ... popf)

*   **Concept:** Use `pushf`/`cli` to enter a critical section and `popf` to exit, which correctly preserves the original interrupt flag state.
*   **Coverage Analysis:** **Not Implemented in Main Driver.**
    *   The code in `tsr_common.asm` defines `ENTER_CRITICAL` and `EXIT_CRITICAL` macros that correctly use `pushf`/`popf`.
    *   However, the rest of the assembly code does not use this pattern. The ISRs in `nic_irq.asm` use `pusha`/`popa` (or manual register pushes) and `iret`, but there is no evidence of `pushf`/`popf` for managing critical sections within the handlers.

---

### 6. Interrupt Vector Hygiene

*   **Concept:** Safely install and uninstall interrupt vectors by saving the old vector, chaining to it if the interrupt is not yours, and verifying ownership before uninstalling.
*   **Coverage Analysis:** **Partial.**
    *   `main.asm` correctly saves the old `INT 60h` vector in `install_interrupts` and restores it in `uninstall_interrupts`.
    *   `tsr_common.asm` shows a more robust implementation with `install_multiplex_handler` which saves and sets `INT 2Fh`.
    *   However, there is no evidence of **chaining**. The `packet_handler` in `main.asm` and the hardware ISRs in `nic_irq.asm` handle the interrupt and then `iret`, assuming they are the sole owner. There is also no ownership check before uninstalling.

---

### 7. PIC/IRQ Correctness (EOI)

*   **Concept:** Send a proper End-Of-Interrupt (EOI) signal to the Programmable Interrupt Controller (PIC) after servicing an interrupt.
*   **Coverage Analysis:** **Present but Incomplete.**
    *   The hardware ISRs in `nic_irq.asm` correctly issue an EOI to the master PIC (`out PIC1_COMMAND, al`).
    *   However, the code contains a `TODO` comment indicating that it does not handle sending an EOI to the slave PIC for IRQs 8-15. This is an incomplete implementation that would cause issues for higher IRQs.

---

### 8. Timeouts on Hardware Waits

*   **Concept:** Never wait indefinitely for hardware. Use bounded loops with timeouts to prevent system lockups.
*   **Coverage Analysis:** **Present in C code, Not Found in Assembly.**
    *   The C code in `3c509b.c` and `3c515.c` shows evidence of this pattern (e.g., `_3c509b_wait_for_cmd_busy` has a timeout).
    *   However, the assembly code in `src/asm` does not contain any explicit timeout loops for hardware I/O waits.

---

### 9. Shared Data Integrity (Signatures, Checksums)

*   **Concept:** Use magic signatures and checksums on resident data structures to detect memory corruption.
*   **Coverage Analysis:** **Present.**
    *   `main.asm` uses a `DRIVER_MAGIC` and an installation check mechanism (`INSTALL_CHECK_AX`/`INSTALL_RESP_AX`) via the `INT 60h` vector. This serves as a signature to detect if the driver is already loaded.
    *   `tsr_common.asm` also defines a `driver_signature`. This is a well-covered technique.

---

### 10. Presence/Versioning API

*   **Concept:** Provide a clear way for other software to detect the driver, its version, and its capabilities.
*   **Coverage Analysis:** **Present.**
    *   The `packet_api.asm` module implements the standard Packet Driver API, including `API_DRIVER_INFO` (Function 1), which is the standard way to get driver version, class, and type.
    *   `tsr_common.asm` implements the AMIS `INT 2Fh` installation check, which is another standard for TSRs.

---

### Other Techniques

*   **Deferred-work queue:** Implemented in `tsr_common.asm` but not used by the main driver.
*   **Memory discipline (TSR-keep):** `main.asm` correctly uses `int 21h, ah=31h` to terminate and stay resident.
*   **Diagnostics build mode:** No evidence of a `DEBUG_BUILD` switch or diagnostic counters in the assembly code.
*   **Safe uninstall:** The `uninstall_interrupts` function restores vectors but does not verify ownership first.
*   **Packet-driver etiquette:** The `packet_api.asm` file is a skeleton for the standard packet driver interface, showing clear intent to follow the specification.

