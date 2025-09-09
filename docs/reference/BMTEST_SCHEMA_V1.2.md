# BMTEST JSON Schema Version 1.2

## Overview

This document defines the JSON output schema for the Bus Master Test (BMTEST) utility, version 1.2.

## Schema Changes from v1.1

- Added `schema_version` field for version identification
- Added `parameters` object for test configuration
- Added `units` object for explicit unit definitions
- Enhanced `telemetry` object with additional fields
- Added `variance_analysis` for throughput variance reporting
- Added `rollback_details` for audit trail
- Added `smoke_gate_decision` for pass/fail reasoning

## Schema Structure

### Root Object

```json
{
  "schema_version": "1.2",
  "test": "stress|soak|negative|standard",
  "timestamp": "ISO 8601 timestamp",
  "environment": { ... },
  "parameters": { ... },
  "results": { ... },
  "variance_analysis": { ... },
  "rollback_details": { ... },
  "telemetry": { ... },
  "units": { ... },
  "smoke_gate_decision": { ... },
  "result": "PASS|FAIL"
}
```

### Field Definitions

#### `schema_version` (string)
- Current version: "1.2"
- Used to identify schema format for parsing

#### `test` (string)
- Test type identifier
- Values: "stress", "soak", "negative", "standard"

#### `timestamp` (string)
- ISO 8601 formatted timestamp
- Example: "2025-01-03T10:30:00Z"

#### `environment` (object)
```json
{
  "dos_version": "6.22",
  "cpu_family": 5,
  "cpu_model": 2,
  "memory_kb": 16384,
  "ems_present": false,
  "xms_present": true,
  "vds_present": false,
  "himem_version": "3.10",
  "emm386_detected": false,
  "qemm_detected": false,
  "bios_cache_enabled": true
}
```

#### `parameters` (object)
Test configuration parameters:
```json
{
  "seed": "0x12345678",
  "target_rate_pps": 100,
  "duration_sec": 600,
  "packet_sizes": [64, 128, 256, 512, 1024, 1514],
  "distribution": "uniform",
  "cooldown_ms": 500
}
```

#### `results` (object)
Test execution results:
```json
{
  "packets_sent": 60000,
  "packets_failed": 12,
  "bytes_sent": 30720000,
  "throughput_kbps": 409,
  "health_checks": 600,
  "rollbacks": 0,
  "error_rate": 0.0002,
  "boundary_violations": 0,
  "cache_stale_reads": 0,
  "cli_max_ticks": 6,
  "latency_max_us": 85,
  "latency_median_us": 42,
  "latency_p95_us": 71
}
```

#### `variance_analysis` (object)
Statistical variance analysis:
```json
{
  "throughput_samples": 100,
  "throughput_mean_kbps": 410,
  "throughput_median_kbps": 409,
  "throughput_p95_kbps": 425,
  "throughput_std_dev": 8.2,
  "variance_coefficient": 0.02,
  "high_variance": false,
  "variance_reason": "NORMAL"
}
```

#### `rollback_details` (array)
Audit trail of rollback events:
```json
[
  {
    "index": 0,
    "reason_code": 1,
    "event_code": "0x8000",
    "patch_mask": "0xFFFF",
    "description": "Health degraded"
  }
]
```

#### `telemetry` (object)
Driver telemetry snapshot:
```json
{
  "version": 0x0101,
  "cpu_family": 5,
  "cpu_model": 2,
  "cpu_stepping": 4,
  "nic_io_base": "0x0300",
  "nic_irq": 10,
  "nic_type": 1,
  "cache_tier": 2,
  "patches_active": 15,
  "health_flags": "0x0000",
  "loopback_enabled": false,
  "loopback_supported": true,
  "cascade_ok": true,
  "capability_mask": "0xFFFFFFFF",
  "uptime_ticks": 18250
}
```

#### `units` (object)
Explicit unit definitions:
```json
{
  "throughput": "kilobits_per_second",
  "duration": "seconds",
  "bytes": "bytes",
  "latency": "microseconds",
  "rate": "packets_per_second",
  "memory": "kilobytes",
  "ticks": "timer_ticks",
  "variance": "coefficient"
}
```

#### `smoke_gate_decision` (object)
Pass/fail decision reasoning:
```json
{
  "passed": true,
  "reason": "All criteria met",
  "criteria": {
    "boundary_safe": true,
    "cache_coherent": true,
    "performance_ok": true,
    "latency_ok": true,
    "stability_ok": true
  },
  "recommendation": "ENABLE_DMA"
}
```

## Usage Examples

### Successful Stress Test
```json
{
  "schema_version": "1.2",
  "test": "stress",
  "parameters": {
    "seed": "0x12345678",
    "target_rate_pps": 100,
    "duration_sec": 600
  },
  "results": {
    "packets_sent": 60000,
    "packets_failed": 0,
    "error_rate": 0.0000
  },
  "variance_analysis": {
    "high_variance": false
  },
  "result": "PASS"
}
```

### Failed Negative Test with Rollback
```json
{
  "schema_version": "1.2",
  "test": "negative",
  "results": {
    "rollbacks": 1
  },
  "rollback_details": [
    {
      "reason_code": 1,
      "description": "Forced DMA error"
    }
  ],
  "result": "PASS"
}
```

## Validation Rules

1. `schema_version` must be "1.2"
2. `test` must be one of the defined values
3. All numeric values must be non-negative
4. `error_rate` must be between 0.0 and 1.0
5. `high_variance` is true if variance_coefficient > 0.20
6. `result` must be "PASS" or "FAIL"

## Migration from v1.1

To migrate from v1.1:
1. Add `schema_version` field
2. Move test config to `parameters` object
3. Add `units` object for clarity
4. Include `variance_analysis` if applicable
5. Add `telemetry` snapshot