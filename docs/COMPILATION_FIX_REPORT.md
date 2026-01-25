# 3Com DOS Packet Driver - Compilation Fix Report

**Date:** 2026-01-25 00:12:21
**Target Compiler:** Open Watcom C 2.0 (16-bit DOS, C89/ANSI mode)
**Compiler Flags:** `wcc -0 -ms -s -ox -wx`

---

## Executive Summary

Successfully fixed all compilation errors in the 3Com DOS packet driver project, achieving **100% compilation success** (75/75 source files) from an initial state of only 21% (16/75 files).

| Metric | Before | After | Change |
|--------|--------|-------|--------|
| Files Compiling | 16 | 75 | +59 |
| Files Failing | 59 | 0 | -59 |
| Success Rate | 21% | 100% | +79% |
| Total Errors | ~1001 | 0 | Eliminated |

---

## Project Overview

The 3Com DOS packet driver is a 16-bit DOS TSR (Terminate and Stay Resident) driver supporting:
- **3C509B** - ISA PIO-based Ethernet adapter
- **3C515-TX** - ISA bus-mastering Ethernet adapter
- **3C59x/3C90x** - PCI Vortex/Boomerang/Cyclone/Tornado variants

The codebase consists of 75 C source files and ~100 header files implementing:
- Packet Driver API (interrupt 0x60-0x80)
- DMA and bus-mastering support
- VDS (Virtual DMA Services) for memory managers
- PCI BIOS interface
- Hardware abstraction layer

---

## Issues Found and Fixed

### 1. C89 Declaration Order Violations (~500+ occurrences)

**Problem:** C89/ANSI C requires all variable declarations at the beginning of a block, before any executable statements. The code had declarations mixed with statements throughout.

**Before (Invalid C89):**
```c
int func(void) {
    do_something();
    uint32_t x = value;  /* ERROR: declaration after statement */
    if (condition) {
        int y = 0;       /* ERROR: declaration after statement */
    }
}
```

**After (Valid C89):**
```c
int func(void) {
    uint32_t x;
    do_something();
    x = value;
    if (condition) {
        int y;
        y = 0;
    }
}
```

**Files affected:** Nearly all 75 source files

---

### 2. `static inline` Not Supported in C89

**Problem:** The `inline` keyword is a C99 feature. Open Watcom in C89 mode doesn't recognize it.

**Fix:** Changed all `static inline` to `static` throughout the codebase.

**Files affected:** Most header files including:
- `portabl.h`, `common.h`, `regacc.h`, `pci_bios.h`
- `dmabnd.h`, `cacheche.h`, `bufaloc.h`, `packet.h`

---

### 3. GCC Inline Assembly Syntax

**Problem:** GCC-style inline assembly (`__asm__ volatile`) is not supported by Open Watcom.

**Before (GCC):**
```c
__asm__ volatile ("cli" ::: "memory");
__asm__ volatile ("sti" ::: "memory");
```

**After (Watcom):**
```c
#ifdef __GNUC__
    __asm__ volatile ("cli" ::: "memory");
#elif defined(__WATCOMC__)
    _disable();
#endif
```

For complex assembly, used `#pragma aux`:
```c
#pragma aux save_flags_cli = \
    "pushf" \
    "cli" \
    "pop ax" \
    value [ax] \
    modify [ax];
```

**Files affected:** `smcserl.c`, `dmacap.c`, `cachemgt.c`, `tsrmgr.c`, `bufaloc.c`, `dmabnd.c`

---

### 4. Struct Field Name Mismatches (~100+ occurrences)

**Problem:** Code referenced struct fields that didn't exist or had different names.

**Examples:**
| Wrong | Correct | Struct |
|-------|---------|--------|
| `g_cpu_info.type` | `g_cpu_info.cpu_type` | `cpu_info_t` |
| `config->interrupt` | `config->interrupt_vector` | `config_t` |
| `desc.physical_addr` | `desc.physical` | `vds_dds_t` |
| `nic->nic_info.io_base` | `nic->io_base` | `nic_context_t` |
| `ctx->base.stats.tx_packets` | `ctx->base.packets_tx` | `boomerang_context_t` |

**Files affected:** `init.c`, `memory.c`, `api.c`, `3cboom.c`, `3cvortex.c`, `bmtest.c`, `pciintg.c`, many others

---

### 5. CPU Type Constant Naming

**Problem:** Code used `CPU_TYPE_*` constants but the header defines `CPU_DET_*`.

**Fix:**
```c
/* Before */
if (g_cpu_info.cpu_type < CPU_TYPE_80386) { ... }

/* After */
if (g_cpu_info.cpu_type < CPU_DET_80386) { ... }
```

**Constants renamed:**
- `CPU_TYPE_8086` → `CPU_DET_8086`
- `CPU_TYPE_80286` → `CPU_DET_80286`
- `CPU_TYPE_80386` → `CPU_DET_80386`
- `CPU_TYPE_80486` → `CPU_DET_80486`
- `CPU_TYPE_PENTIUM` → `CPU_DET_CPUID_CAPABLE`

**Files affected:** `memory.c`, `bmtest.c`, `pciintg.c`, `dmapol.c`, `dmacap.c`

---

### 6. Logging Function Name Case

**Problem:** Code used uppercase `LOG_INFO`, `LOG_ERROR`, etc. but the functions are lowercase.

**Fix:** Added macro aliases or changed calls directly:
```c
#define LOG_INFO    log_info
#define LOG_ERROR   log_error
#define LOG_WARNING log_warning
#define LOG_DEBUG   log_debug
```

**Files affected:** `routing.c`, `3cboom.c`, `3cvortex.c`, `3cpcidet.c`, `dmapol.c`, `nicsafe.c`, many others

---

### 7. Macro/Enum Naming Conflicts

**Problem:** Macros defined values that conflicted with enum members.

**Example:** `NIC_TYPE_3C515_TX` defined as macro `2` in `common.h`, but also as enum value in `nic_defs.h`.

**Fix:** Added `#undef` directives before enum definitions:
```c
#ifdef NIC_TYPE_3C515_TX
#undef NIC_TYPE_3C515_TX
#endif

typedef enum {
    NIC_TYPE_UNKNOWN = 0,
    NIC_TYPE_3C509B = 1,
    NIC_TYPE_3C515_TX = 2,
    /* ... */
} nic_type_t;
```

**Files affected:** `nic_defs.h`, `common.h`, `pltprob.h`

---

### 8. C99 Enum Initializers

**Problem:** C89 doesn't support explicit enum initializers like `ENUM_VAL = 0`.

**Before:**
```c
typedef enum {
    BUS_TYPE_UNKNOWN = 0,
    BUS_TYPE_ISA = 1,
} bus_type_t;
```

**After:**
```c
typedef enum {
    BUS_TYPE_UNKNOWN,    /* 0 */
    BUS_TYPE_ISA,        /* 1 */
} bus_type_t;
```

**Files affected:** `common.h`, various header files

---

### 9. C99 Designated Initializers

**Problem:** C89 doesn't support `.field = value` struct initializers.

**Before:**
```c
static config_t default_config = {
    .magic = CONFIG_MAGIC,
    .io1_base = 0x300,
    .verbose = false
};
```

**After:**
```c
static config_t default_config = {
    CONFIG_MAGIC,    /* magic */
    0x300,           /* io1_base */
    0                /* verbose */
};
```

**Files affected:** `config.c`, `pcitest.c`, `3cpcidet.c`

---

### 10. DOS Interrupt Handling

**Problem:** Incorrect usage of `int86()` vs `int86x()` and segment register access.

**Before (Wrong):**
```c
union REGS regs;
regs.w.es = segment;  /* ERROR: es is in SREGS, not REGS */
int86(0x21, &regs, &regs);
```

**After (Correct):**
```c
union REGS regs;
struct SREGS sregs;
segread(&sregs);
sregs.es = segment;
int86x(0x21, &regs, &regs, &sregs);
```

**Files affected:** `dmasafa.c`, `dmabnd.c`, `vds.c`

---

### 11. Missing Header Includes

**Problem:** Files referenced types/functions without including the necessary headers.

**Common missing includes:**
- `#include "nicctx.h"` - for `nic_context_t` full definition
- `#include "diag.h"` - for LOG_* macros
- `#include <i86.h>` - for `int86()`, `REGS`, `SREGS`
- `#include <malloc.h>` - for `_fmalloc()`, `halloc()`
- `#include "portabl.h"` - for C89 type definitions

**Files affected:** `3cvortex.c`, `3cpcidet.c`, `dmasafa.c`, `hwchksm.c`, `pcmmgr.c`

---

### 12. Source File Corruption (Embedded `\n`)

**Problem:** Several source files had literal `\n` text instead of actual newlines, causing parse errors.

**Before (Corrupted):**
```c
log_info("Message");\n    \n    /* Comment */\n    result = func();
```

**After (Fixed):**
```c
log_info("Message");

/* Comment */
result = func();
```

**Files affected:** `init.c`, `routing.c`, `3c515.c`

---

### 13. Function Signature Mismatches

**Problem:** Function implementations didn't match header declarations.

**Examples:**
| Function | Issue | Fix |
|----------|-------|-----|
| `hardware_send_packet()` | `uint16_t` vs `size_t` | Changed to `size_t` |
| `dma_build_safe_sg()` | Wrong return type | Changed `int` to `dma_sg_list_t*` |
| `routing_cleanup()` | Returns `void` not `int` | Removed result assignment |

**Files affected:** `hardware.c`, `dmasafe.c`, `init.c`

---

### 14. Invalid Hex Constants

**Problem:** Invalid hexadecimal constant `0x3C0M` (M is not a hex digit).

**Fix:** Changed to valid hex `0x3C05`:
```c
#define CONFIG_MAGIC 0x3C05
```

**Files affected:** `config.h`

---

### 15. 486+ Instructions in 8086 Mode

**Problem:** Using `wbinvd` instruction (486+) when compiling for 8086 (`-0` flag).

**Fix:** Wrapped in assembler mode directives:
```c
_asm {
    .486
    wbinvd
    .8086
}
```

**Files affected:** `dmacap.c`

---

### 16. Huge Array Declarations

**Problem:** Arrays exceeding 64KB segment limit in small memory model.

**Fix:** Added `HUGE` keyword for large arrays:
```c
static promisc_packet_buffer_t HUGE g_promisc_buffers[64];
```

**Files affected:** `promisc.c`, `promisc.h`

---

### 17. Function Name Conflicts

**Problem:** Local static functions conflicted with library or header declarations.

**Examples:**
- `delay_ms()` - renamed to `bm_delay_ms()`
- `stricmp()` - renamed to `cfg_stricmp()`
- `memory_free_dma()` - renamed to `memory_cleanup_dma_pool()`

**Files affected:** `bmtest.c`, `config.c`, `memory.c`

---

## Files Modified

### Source Files (75 total)
All 75 `.c` files in `src/c/` were modified to fix compilation errors.

### Header Files (~40 modified)
Key headers modified:
- `include/portabl.h` - C89 compatibility layer
- `include/common.h` - Core definitions
- `include/cpudet.h` - CPU detection (added `extern g_cpu_info`)
- `include/config.h` - Fixed CONFIG_MAGIC
- `include/nic_defs.h` - Added #undef guards
- `include/hardware.h` - Added missing fields
- `include/nicctx.h` - Full nic_context_t definition
- `include/dmabnd.h` - Changed bool to int returns
- `include/3c515.h` - Added missing register definitions
- `include/3c509b.h` - Added missing register definitions

---

## Verification

Final compilation test:
```bash
export WATCOM="/path/to/open-watcom-v2/rel"
export PATH="$WATCOM/armo64:$PATH"
for f in src/c/*.c; do
    wcc -0 -ms -s -ox -wx -fo=build/$(basename "$f" .c).obj \
        -i=include -i="$WATCOM/h" "$f"
done
```

**Result:** All 75 files compile successfully with 0 errors.

---

## Recommendations for Future Development

1. **Use C89-compliant coding style** - Declare all variables at block start
2. **Avoid `inline` keyword** - Use `static` for internal functions
3. **Use portable assembly** - Wrap compiler-specific assembly in `#ifdef`
4. **Consistent naming** - Follow established conventions (`log_*`, `CPU_DET_*`)
5. **Include guards** - Ensure all headers have proper include guards
6. **Test with target compiler** - Regularly compile with Open Watcom during development

---

## Conclusion

The 3Com DOS packet driver project now successfully compiles with the Open Watcom C compiler in C89/ANSI mode. All 75 source files produce valid object files ready for linking into a DOS executable or TSR.

The primary issues were C89 compliance violations (declaration ordering, inline keyword) and inconsistent naming between code and headers. These systemic issues have been resolved, establishing a solid foundation for continued development.
