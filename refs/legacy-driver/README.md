# Legacy Driver Reference

This directory contains the original 3Com packet driver assembly code that was used as reference during the development of the new modular implementation.

## Files

- **3c5x9pd.asm** - Main driver file with memory map documentation
- **code.asm** - Core driver functionality
- **data1.asm** - Initial data structures
- **data2.asm** - Additional data structures

## Purpose

These files serve as:
- Reference implementation for hardware programming sequences
- Examples of real-mode assembly techniques
- Validation source for register definitions and timing

## Build Instructions

Original build command (preserved for reference):
```
nasm -f bin -o 3c5x9pd.com 3c5x9pd.asm
```

## Note

This is legacy code preserved for reference only. The active development uses the modular C/Assembly implementation in the `src/` directory following the implementation plan in `docs/IMPLEMENTATION_PLAN.md`.