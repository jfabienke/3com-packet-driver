# Expected Map File Patterns

**Last Updated:** 2026-01-27 16:45:00 UTC

This document shows what to look for in `build/3cpd.map` to verify correct overlay placement.

---

## 1. Good Patterns (What Success Looks Like)

### ROOT Object Placement

Objects listed BEFORE the `BEGIN` overlay block are ROOT-resident:

```
# From 3cpd.lnk - these should appear OUTSIDE BEGIN/END blocks in map

FILE build/hardware.obj      <-- ROOT (good)
FILE build/3c509b.obj        <-- ROOT (good)
FILE build/3c515.obj         <-- ROOT (good)
FILE build/logging.obj       <-- ROOT (good)

BEGIN                        <-- OVERLAY AREA STARTS HERE
    SECTION                  <-- INIT_EARLY overlay
    FILE build/main.obj
    ...
END
```

### Segment Table

Look for the segment table section. Good placement shows:

```
Segment                Class          Group          Address         Size
=======                =====          =====          =======         ====

_TEXT                  CODE           AUTO           0000:0000       XXXXX
_DATA                  DATA           DGROUP         XXXX:0000       XXXXX
CONST                  CONST          DGROUP         XXXX:XXXX       XXXXX

# Overlay data segments - should NOT be in DGROUP
OVL_EARLY_D            FAR_DATA       INIT_EARLY     XXXX:0000       XXX
OVL_DETECT_D           FAR_DATA       INIT_DETECT    XXXX:0000       XXX
OVL_FINAL_D            FAR_DATA       INIT_FINAL     XXXX:0000       XXX
```

### Symbol Placement

Runtime symbols should resolve to ROOT segment addresses:

```
# Good - these are in ROOT (_TEXT or similar, NOT in INIT_*)
logging_init           _TEXT:0A4F
log_info               _TEXT:0B12
log_error              _TEXT:0C34
g_3c509b_ops           _DATA:0100
_3c509b_send_packet    _TEXT:1234
```

---

## 2. Bad Patterns (What Failure Looks Like)

### CRITICAL: vtable in Overlay

```
# BAD - g_3c509b_ops should be ROOT, not INIT_DETECT!
g_3c509b_ops           INIT_DETECT_DATA:0000   <-- CRASH WAITING TO HAPPEN
```

### CRITICAL: Logging in Overlay

```
# BAD - logging.obj must be ROOT!
BEGIN
    SECTION              # INIT_EARLY
    FILE build/logging.obj   <-- WILL CRASH AT RUNTIME
```

### CRITICAL: Overlay Data Folded into DGROUP

```
# BAD - OVL_DETECT_D should be in overlay, not DGROUP!
OVL_DETECT_D           FAR_DATA       DGROUP    XXXX:0000    XXX
                                      ^^^^^^
                                      Should be INIT_DETECT, not DGROUP
```

### CRITICAL: NIC Driver in Overlay

```
BEGIN
    SECTION              # INIT_DETECT
    FILE build/3c509b.obj    <-- WILL CRASH WHEN nic->ops->send_packet() CALLED
```

---

## 3. Watcom WLINK Overlay Section Format

The map file for overlays typically shows:

```
Overlay Manager Summary
=======================
Overlay Area:     64K bytes
Number of overlays: 3

Overlay 0 (INIT_EARLY)
----------------------
  Segment: INIT_EARLY_TEXT    Size: XXXX
  Segment: OVL_EARLY_D        Size: XXX
  Files: main.obj, init.obj, config.obj, ...

Overlay 1 (INIT_DETECT)
-----------------------
  Segment: INIT_DETECT_TEXT   Size: XXXX
  Segment: OVL_DETECT_D       Size: XXX
  Files: chipdet.obj, 3cpcidet.obj, pciscan.obj, ...

Overlay 2 (INIT_FINAL)
----------------------
  Segment: INIT_FINAL_TEXT    Size: XXXX
  Segment: OVL_FINAL_D        Size: XXX
  Files: unwind.obj, diag.obj, ...
```

---

## 4. Quick Grep Commands for Verification

```bash
# Check if forbidden objects are in overlays
grep -E "BEGIN|END|SECTION" build/3cpd.map | head -20

# Find where logging.obj is placed
grep -i "logging" build/3cpd.map

# Find where NIC drivers are placed
grep -iE "3c509b|3c515|hardware" build/3cpd.map

# Check for OVL_* segment placement
grep -i "OVL_" build/3cpd.map

# Check DGROUP contents
grep -i "DGROUP" build/3cpd.map

# Find vtable symbols
grep -iE "g_3c509b_ops|g_3c515_ops|_ops" build/3cpd.map
```

---

## 5. Automated Verification

Run the verification script:

```bash
python3 tools/verify_map.py build/3cpd.map
```

Add `--verbose` for detailed segment/symbol dump:

```bash
python3 tools/verify_map.py build/3cpd.map --verbose
```

---

## 6. If Segments Are Wrong

### OVL_* Segments in DGROUP

If `__based(__segname("OVL_*"))` data ends up in DGROUP:

1. Check `-zdf` flag is set (DS != DGROUP)
2. Verify `const` keyword is removed (Watcom may ignore pragmas for const)
3. Try explicit segment pragma:
   ```c
   #pragma data_seg("OVL_DETECT_D", "FAR_DATA")
   ```

### Objects in Wrong Overlay

If an object appears in an overlay when it should be ROOT:

1. Check `3cpd.lnk` - object must be listed BEFORE the `BEGIN` block
2. Verify no duplicate FILE entries (one in ROOT, one in overlay)

### Symbols Resolving to Overlay

If a runtime-called symbol resolves to an overlay segment:

1. The source file is in an overlay - move it to ROOT
2. Or split the source file (init-only vs runtime functions)
