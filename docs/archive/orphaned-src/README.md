# Orphaned Source Code Archive

This directory contains source files that are not part of the active build but may contain valuable code for future reference or integration.

## Notable Orphaned Files

### INT 60h Handler (`src/asm/unified_interrupt.asm`)
- **Status**: Not compiled/linked in current build
- **Reason**: Legacy interrupt handler with issues:
  - Used 32-bit registers (EAX) in .8086 context
  - CF flag handling semantics were incorrect
  - Replaced by more focused interrupt handling in active modules
- **Potential Value**: Contains interrupt dispatching logic that could be refactored if unified INT 60h support is needed

### Tiny ISR Implementation
- **Files**: Various ISR-related files
- **Value**: Compact interrupt service routine implementation
- **Future Use**: Could reduce resident footprint if integrated

### Interrupt Mitigation
- **Files**: Interrupt coalescing and mitigation code
- **Value**: Reduces interrupt overhead for high packet rates
- **Future Use**: Performance optimization for 100Mbps operation

### XMS Buffer Migration
- **Files**: XMS memory management code
- **Value**: Allows packet buffers in extended memory
- **Future Use**: Could enable larger buffer pools without consuming conventional memory

### RX Batch Refill
- **Files**: Batch buffer allocation code
- **Value**: Improves efficiency of receive buffer management
- **Future Use**: Performance optimization for sustained high throughput

## Integration Guidelines

Before reintegrating any orphaned code:
1. Review for compatibility with current architecture
2. Fix any assembly context issues (.8086 vs .386)
3. Update to use current APIs and data structures
4. Test thoroughly with both 3C509B and 3C515-TX
5. Verify resident size remains within budget