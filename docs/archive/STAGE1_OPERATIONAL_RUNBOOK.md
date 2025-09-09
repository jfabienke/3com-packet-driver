# Stage 1 Operational Runbook - DMA Validation Testing

## Quick Reference Decision Matrix

| Criteria | Enable DMA | Keep PIO |
|----------|------------|----------|
| Boundary violations | 0 | Any |
| Stale cache reads | 0 | Any |
| CLI disable ticks | ≤8 | >8 |
| Max latency | <100μs | ≥100μs |
| DMA vs PIO throughput | ≥PIO | <PIO |
| Stress test rollbacks | 0 | Any |
| Policy persistence | Success | Failed |
| IRQ cascade (slave) | OK | Broken |

## Pre-Run Checklist

### Environment Verification
```
□ DOS version (run: VER)
  - Target: DOS 6.22
  - Minimum: DOS 3.3

□ Memory configuration (run: MEM /C)
  - Conventional: >500KB free
  - XMS: HIMEM.SYS loaded
  - EMS: Note if EMM386/QEMM present
  - UMB: Note availability

□ BIOS cache settings (check BIOS setup)
  - L1 cache: Enabled/Disabled
  - L2 cache: Enabled/Disabled
  - Write-back/Write-through mode

□ VDS availability (run: BMTEST -v)
  - Check "VDS: Available" message

□ Driver initial state (run: LOADDRV)
  - PIO mode enforced initially
  - No previous policy file
  - All counters zeroed
```

### Hardware Configuration
```
□ 3C515-TX NIC installed
  - IO Base: 0x300 (typical)
  - IRQ: 10 or 11 (typical)
  - ISA PnP: Enabled in BIOS

□ CPU type verification
  - 486: Minimum for bus mastering
  - Pentium: Optimal performance
  
□ Chipset compatibility
  - ISA DMA controller present
  - 24-bit addressing limit understood
```

## Execution Sequence

### Phase 1: Initial Setup
```bash
# 1. Load packet driver
LOADDRV
# Expected: "Driver loaded successfully at INT 60h"

# 2. Verify extension API
BMTEST -v
# Expected: Version 1.1+, patches active

# 3. Clear any previous policy
DEL C:\3CPKT\DMA.SAF
```

### Phase 2: PIO Baseline
```bash
# Run PIO baseline test
BMTEST -d -j > BASELINE.JSON

# Verify PIO metrics:
# - Throughput: Record value
# - CPU usage: Record value
# - Max latency: <100μs expected
```

### Phase 3: Boundary Validation
```bash
# Test DMA boundaries with quiesced driver
BMTEST -d -seed 0xDEADBEEF -v

# Check for:
# - "Aligned buffer: 0 bounces, 0 violations"
# - "Cross-64KB: N bounces, 0 violations"
# - "Above 16MB: Rejected" (if applicable)
```

### Phase 4: Cache Coherency
```bash
# Verify cache management
BMTEST -d -v | grep -i cache

# Expected results by CPU:
# - 486: "WBINVD supported, using tier 2"
# - Pentium: "WBINVD supported, using tier 2"
# - 386: "Using tier 1 (conservative)"
```

### Phase 5: Performance Comparison
```bash
# Compare DMA vs PIO throughput
BMTEST -d -j > PERF.JSON

# Decision criteria:
# - DMA >= PIO: Continue
# - DMA < PIO: Investigate chipset issues
```

### Phase 6: Stress Testing
```bash
# 10-minute stress test with fixed seed
BMTEST -s -seed 0x12345678 -rate 100 -j > STRESS.JSON

# Monitor for:
# - Error rate: <0.01%
# - Rollbacks: 0
# - Health checks: All pass
# - Variance: <20% (not HIGH_VARIANCE)
```

### Phase 7: Negative Testing (Optional)
```bash
# Force failure to verify rollback
BMTEST -n -v

# Expected:
# - "DMA disabled on error" or
# - "Health degraded on error"
# - Clean recovery after test
```

### Phase 8: Soak Testing (Optional)
```bash
# 30-minute extended test
BMTEST -S 30 -seed 0x87654321 -j > SOAK.JSON

# Success criteria:
# - No degradation over time
# - Memory usage stable
# - No rollbacks
```

## Post-Run Verification

### Driver State Check
```bash
# 1. Verify patches still active
BMTEST -v | grep "Patches active"

# 2. Check IRQ cascade (for IRQ 8-15)
BMTEST -v | grep "cascade_ok"

# 3. Confirm quiesce cleared
BMTEST -v | grep "driver_quiesced: 0"
```

### Policy Verification
```bash
# Check policy file created
DIR C:\3CPKT\DMA.SAF

# Verify policy matches results
TYPE BMTEST.RPT | grep "Policy"
```

### Resident Memory Check
```bash
# Verify no memory growth
MEM /C | grep "3C515"

# Expected: <6,886 bytes resident
```

## JSON Output Analysis

### Critical Fields to Check
```json
{
  "schema_version": "1.2",
  "results": {
    "boundary_violations": 0,      // MUST be 0
    "cache_stale_reads": 0,        // MUST be 0
    "cli_max_ticks": 6,            // MUST be ≤8
    "latency_max_us": 85,          // MUST be <100
    "rollbacks": 0                 // MUST be 0
  },
  "variance_analysis": {
    "high_variance": false,        // MUST be false
    "variance_coefficient": 0.15   // SHOULD be <0.20
  },
  "telemetry": {
    "cascade_ok": true,            // MUST be true for IRQ 8-15
    "loopback_supported": true     // Note capability
  },
  "smoke_gate_decision": {
    "passed": true,
    "recommendation": "ENABLE_DMA"
  }
}
```

## Troubleshooting Guide

### Common Issues and Solutions

| Symptom | Likely Cause | Solution |
|---------|--------------|----------|
| Driver won't load | IO/IRQ conflict | Try different IO base (0x300, 0x320, 0x340) |
| Boundary violations | EMM386 active | Boot without EMM386, test pure DOS |
| High CLI ticks | Slow chipset | Reduce target rate, check BIOS settings |
| DMA < PIO | ISA bus issues | Check DMA channel, try different slot |
| Rollbacks during stress | Marginal hardware | Reduce packet rate, check cooling |
| JSON malformed | Disk full | Clear space, check C:\TEMP |
| Cascade broken | PIC misconfigured | Check BIOS IRQ settings |
| Policy save fails | Write-protected | Check disk attributes |

### Emergency Recovery

If system becomes unstable:

1. **Immediate Recovery**
   ```bash
   # Force PIO mode
   BMTEST -n
   
   # Delete policy file
   DEL C:\3CPKT\DMA.SAF
   
   # Reboot system
   CTRL+ALT+DEL
   ```

2. **Clean Boot**
   ```
   # Boot with F8, choose "Safe mode command prompt"
   # Or boot from floppy without AUTOEXEC.BAT
   ```

3. **Driver Removal**
   ```bash
   # Remove from CONFIG.SYS
   EDIT C:\CONFIG.SYS
   # Comment out: REM DEVICE=C:\3C515PD.COM
   ```

## Performance Baselines

### Expected Results by System

| System | PIO (Kbps) | DMA (Kbps) | CPU (PIO/DMA) | Decision |
|--------|------------|------------|---------------|----------|
| 486DX2-66 | 400-500 | 450-550 | 40%/25% | Enable DMA |
| Pentium-100 | 600-700 | 750-900 | 35%/15% | Enable DMA |
| Pentium-166 | 700-800 | 900-1100 | 30%/10% | Enable DMA |
| 386DX-40 | 300-400 | 280-350 | 60%/50% | Keep PIO |

## Reporting Template

```
STAGE 1 DMA VALIDATION REPORT
=============================
Date: [DATE]
System: [CPU TYPE/SPEED]
DOS Version: [VERSION]
Memory Config: [CONVENTIONAL/XMS/EMS]

TEST RESULTS
------------
PIO Baseline: [XXX] Kbps @ [XX]% CPU
DMA Performance: [XXX] Kbps @ [XX]% CPU
Boundary Tests: [PASS/FAIL]
Cache Coherency: [PASS/FAIL]
Stress Test: [PASS/FAIL]
Error Rate: [X.XX]%
Rollbacks: [X]

DECISION
--------
[X] Enable DMA - All criteria met
[ ] Keep PIO - [Reason]

NOTES
-----
[Any observations or issues]

Signed: _______________
```

## Automated Test Script

Save as `AUTOTEST.BAT`:
```batch
@ECHO OFF
ECHO Stage 1 DMA Validation Test Suite
ECHO ==================================
ECHO.
ECHO Starting at %TIME% on %DATE%
ECHO.

REM Phase 1: Setup
CALL LOADDRV
IF ERRORLEVEL 1 GOTO ERROR

REM Phase 2: Cooldown
ECHO Cooldown period (5 seconds)...
CHOICE /T 5 /D Y > NUL

REM Phase 3: Standard tests
ECHO Running standard DMA validation...
BMTEST -d -seed 0xABCD1234 -j > STANDARD.JSON
IF ERRORLEVEL 1 GOTO ERROR

REM Phase 4: Cooldown
ECHO Cooldown period (5 seconds)...
CHOICE /T 5 /D Y > NUL

REM Phase 5: Stress test
ECHO Running 10-minute stress test...
BMTEST -s -seed 0x12345678 -rate 100 -j > STRESS.JSON
IF ERRORLEVEL 1 GOTO ERROR

REM Phase 6: Results
ECHO.
ECHO Test completed at %TIME%
ECHO Results saved to:
ECHO   STANDARD.JSON
ECHO   STRESS.JSON
GOTO END

:ERROR
ECHO ERROR: Test failed!
ECHO Check logs for details.

:END
```

## Contact for Issues

For problems or questions about Stage 1 testing:
- Review GPT5_CRITICAL_FIXES.md for known issues
- Check IMPLEMENTATION_TRACKER.md for current status
- Consult TESTING_STRATEGY.md for test specifications