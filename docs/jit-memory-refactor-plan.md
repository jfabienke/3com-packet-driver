# JIT Memory Allocation Refactor Plan

**Created:** 2026-01-27 23:45:00 UTC
**Status:** PROPOSED

## Problem Statement

DGROUP is at 62,448 bytes with only 3,088 bytes headroom. The top 4 consumers alone account for **41,247 bytes (66% of DGROUP)**:

| Symbol | Size | Current Location |
|--------|------|------------------|
| `_g_nics` | 20,974 bytes | Static BSS (hwstubs.c) |
| `packet_buffer` | 12,169 bytes | Static BSS (nicirq.asm) |
| `_g_nic_infos` | 4,092 bytes | Static BSS (hardware.c) |
| `_g_routing_table` | 4,012 bytes | Static BSS |

**Target:** Remove these from DGROUP entirely using JIT allocation at TSR commit time.

---

## Existing Infrastructure

The codebase already has:

1. **Three-tier memory allocator** (`memory.c`):
   - XMS allocation (`memory_alloc_xms_tier()`)
   - UMB allocation (`memory_alloc_umb_tier()`)
   - Conventional allocation (`memory_alloc_conventional_tier()`)
   - Strategy selection: XMS → UMB → Conventional

2. **XMS API** (`xms_allocate()`, `xms_free()`, `xms_copy()`)

3. **UMB detection** (`memory_detect_umb()`)

4. **Buffer pool system** (`bufaloc.c`) with VDS support

---

## Two-Phase Memory Model

### Phase 1: EXE Init (Installer/Probing)

During this phase, DGROUP contains only:
- Small control structures (pointers, flags, counters)
- Init context for stage communication (~2.5 KB)
- Overlay manager state

**No NIC buffers exist yet.**

### Phase 2: TSR Commit (JIT Build)

When committing to TSR:
1. Determine memory placement (UMB preferred, then Conventional)
2. Allocate single contiguous slab
3. Lay out sub-buffers at offsets within slab
4. Patch TSR image with final pointers
5. Free overlay area via INT 21h/4Ah

---

## Proposed Structures

### NIC Memory Contract

```c
/* include/nic_mem.h */

typedef enum {
    NIC_MEM_CONVENTIONAL = 0,  /* DOS conventional memory (<640KB) */
    NIC_MEM_UMB = 1,           /* Upper Memory Block (640KB-1MB) */
    NIC_MEM_XMS = 2            /* Extended Memory (>1MB) - requires bounce */
} nic_mem_kind_t;

/* Memory layout within allocated slab */
typedef struct nic_mem_layout {
    uint16_t packet_buffer_offset;   /* Offset to 1536-byte packet buffer */
    uint16_t nic_info_offset;        /* Offset to nic_info_t array */
    uint16_t nic_context_offset;     /* Offset to nic_context_t array */
    uint16_t routing_table_offset;   /* Offset to routing table */
    uint16_t tx_ring_offset;         /* Offset to TX descriptor ring */
    uint16_t rx_ring_offset;         /* Offset to RX descriptor ring */
    uint16_t total_size;             /* Total slab size */
} nic_mem_layout_t;

/* Runtime memory contract - passed to TSR builder */
typedef struct nic_mem {
    nic_mem_kind_t kind;             /* Memory type */

    /* For CONV/UMB: direct far pointer */
    void far *base;                  /* Base address of slab */
    uint16_t segment;                /* Segment for far pointer construction */

    /* For XMS: handle-based access */
    uint16_t xms_handle;             /* XMS handle (0 if not XMS) */
    uint32_t xms_linear_addr;        /* Linear address in XMS */

    /* Layout within slab */
    nic_mem_layout_t layout;

    /* Derived pointers (set after allocation) */
    uint8_t far *packet_buffer;      /* -> packet buffer */
    nic_info_t far *nic_infos;       /* -> NIC info array */
    nic_context_t far *nic_contexts; /* -> NIC context array */
    void far *routing_table;         /* -> routing table */
} nic_mem_t;

/* Global - only the contract struct is in DGROUP (~50 bytes) */
extern nic_mem_t g_nic_mem;

/* Allocation API */
int nic_mem_allocate(nic_mem_t *mem, int num_nics, uint32_t flags);
void nic_mem_free(nic_mem_t *mem);
int nic_mem_validate(const nic_mem_t *mem);
```

### Memory Slab Layout

```
┌────────────────────────────────────────────────────────────┐
│ CONTIGUOUS MEMORY SLAB (UMB or Conventional)               │
├────────────────────────────────────────────────────────────┤
│ Offset 0x0000: packet_buffer (1,536 bytes, 32-byte aligned)│
├────────────────────────────────────────────────────────────┤
│ Offset 0x0600: nic_info_t[MAX_NICS] (~1,000 bytes)         │
├────────────────────────────────────────────────────────────┤
│ Offset 0x0A00: nic_context_t[MAX_NICS] (~2,000 bytes)      │
├────────────────────────────────────────────────────────────┤
│ Offset 0x1200: routing_table (~4,000 bytes)                │
├────────────────────────────────────────────────────────────┤
│ Offset 0x2200: TX descriptor ring (16 × 16 = 256 bytes)    │
├────────────────────────────────────────────────────────────┤
│ Offset 0x2300: RX descriptor ring (16 × 16 = 256 bytes)    │
├────────────────────────────────────────────────────────────┤
│ Total: ~9,000 bytes for basic operation                     │
│ (vs 41,247 bytes currently in DGROUP)                      │
└────────────────────────────────────────────────────────────┘
```

---

## DMA Considerations

### DMA-Critical Buffers (MUST be below 1MB)

| Buffer | DMA Required? | Placement |
|--------|---------------|-----------|
| `packet_buffer` | Yes - ISR RX/TX | UMB or Conventional |
| TX descriptor ring | Yes - bus master | UMB or Conventional |
| RX descriptor ring | Yes - bus master | UMB or Conventional |
| `nic_info_t` | No | UMB preferred |
| `nic_context_t` | No | UMB preferred |
| `routing_table` | No | XMS acceptable |

### Placement Priority

1. **UMB** (preferred): Leaves conventional free for DOS apps, still directly addressable
2. **Conventional** (fallback): If no UMB available
3. **XMS** (for non-DMA only): Large tables, diagnostics, logs

---

## Implementation Steps

### Step 1: Create `nic_mem.h` and `nic_mem.c`

New files:
- `include/nic_mem.h` - Contract structure definitions
- `src/c/nic_mem.c` - Allocation/deallocation implementation

### Step 2: Modify Static Declarations

**Before:**
```c
/* hardware.c */
nic_info_t g_nic_infos[MAX_NICS];

/* hwstubs.c */
nic_info_t g_nics[MAX_NICS];

/* nicirq.asm */
packet_buffer resb 1536
```

**After:**
```c
/* nic_mem.c - only contract struct in DGROUP */
nic_mem_t g_nic_mem = {0};

/* Macros for access (after allocation) */
#define NIC_INFO(i)    (g_nic_mem.nic_infos[i])
#define NIC_CTX(i)     (g_nic_mem.nic_contexts[i])
#define PACKET_BUF     (g_nic_mem.packet_buffer)
```

### Step 3: Add Allocation Call to TSR Commit

In `init_main.c` or `tsrmgr.c`, after hardware detection but before TSR commit:

```c
int result = stage_tsr_relocate(init_context_t far *ctx) {
    nic_mem_t mem;

    /* Determine how many NICs were detected */
    int num_nics = ctx->num_nics;

    /* Allocate NIC memory slab (prefers UMB, falls back to conventional) */
    if (nic_mem_allocate(&mem, num_nics, NIC_MEM_FLAG_DMA_SAFE) != 0) {
        LOG_ERROR("Failed to allocate NIC runtime memory");
        return ERROR_OUT_OF_MEMORY;
    }

    /* Store in global contract */
    g_nic_mem = mem;

    /* Now patch TSR image with these pointers... */
    tsr_patch_nic_pointers(&g_nic_mem);

    return SUCCESS;
}
```

### Step 4: Update ASM ISR to Use Indirect Access

**Before (nicirq.asm):**
```nasm
packet_buffer resb 1536
; ...
mov di, packet_buffer
```

**After:**
```nasm
; In data segment - only the pointer (4 bytes)
extern _g_nic_mem

; In code - load from contract
les di, [_g_nic_mem + NIC_MEM_PACKET_BUFFER]  ; Load far pointer
; ES:DI now points to packet buffer in UMB/conventional
```

### Step 5: Add Build-Time Guard

In `tools/verify_map.py`, add check:

```python
# Forbidden symbols in DGROUP (should be JIT-allocated)
FORBIDDEN_DGROUP_SYMBOLS = [
    'packet_buffer',      # Should be JIT-allocated
    '_g_nics',            # Should be JIT-allocated
    '_g_nic_infos',       # Should be JIT-allocated
    '_g_routing_table',   # Should be JIT-allocated
]

def check_forbidden_dgroup_symbols(symbols):
    errors = []
    for sym in FORBIDDEN_DGROUP_SYMBOLS:
        if sym in dgroup_symbols:
            errors.append(f"FORBIDDEN: {sym} in DGROUP (should be JIT-allocated)")
    return errors
```

---

## Expected DGROUP Savings

| Change | Savings |
|--------|---------|
| Remove `_g_nics` | 20,974 bytes |
| Remove `packet_buffer` | 12,169 bytes |
| Remove `_g_nic_infos` | 4,092 bytes |
| Remove `_g_routing_table` | 4,012 bytes |
| Add `nic_mem_t` contract | +50 bytes |
| **Net Savings** | **~41,200 bytes** |

**New DGROUP estimate:** 62,448 - 41,200 = **~21,200 bytes** (67% reduction!)

---

## Risk Mitigation

1. **DMA Safety**: All DMA-critical buffers placed in UMB/Conventional only
2. **ISR Safety**: Packet buffer pointer validated at init, never changes at runtime
3. **Fallback**: If UMB unavailable, gracefully fall back to conventional
4. **Validation**: `nic_mem_validate()` checks alignment, accessibility, DMA constraints

---

## Files to Modify

| File | Action |
|------|--------|
| `include/nic_mem.h` | **CREATE** - Contract structures |
| `src/c/nic_mem.c` | **CREATE** - Allocation implementation |
| `src/c/hardware.c` | MODIFY - Remove static `g_nic_infos[]` |
| `src/c/hwstubs.c` | MODIFY - Remove static `g_nics[]` |
| `src/asm/nicirq.asm` | MODIFY - Use indirect packet_buffer access |
| `src/c/tsrmgr.c` | MODIFY - Add `nic_mem_allocate()` call |
| `tools/verify_map.py` | MODIFY - Add forbidden symbol check |
| `Makefile.wat` | MODIFY - Add nic_mem.obj |

---

## Verification

After implementation:

```bash
# Build
wmake -f Makefile.wat clean && wmake -f Makefile.wat

# Verify DGROUP reduction
python3 tools/verify_map.py build/3cpd.map --release

# Expected output:
# DGROUP: 0x5200 (~21,000 bytes)
# Headroom: ~43,000 bytes (comfortable!)
```
