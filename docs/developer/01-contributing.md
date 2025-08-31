# Contributing Guidelines

## Development Environment

### Required Tools
- Open Watcom C/C++ 1.9+
- NASM (Netwide Assembler)
- GNU Make
- DOS testing environment (DOSBox, QEMU, or real hardware)

### Code Standards
- Follow existing code style and formatting
- Comment assembly code thoroughly
- Use descriptive variable and function names
- Maintain DOS compatibility (real mode only)

## Testing

Run the comprehensive test suite before submitting changes:
```bash
cd tests
./run_tests.sh
```

See [../testing/](../testing/) for detailed testing procedures.

## Documentation

Update relevant documentation when making changes:
- Architecture documents for design changes
- User documentation for feature additions
- Test documentation for new test procedures

## Submission Process

1. Test thoroughly on target hardware
2. Verify compatibility with existing DOS networking software
3. Update documentation as needed
4. Follow existing commit message conventions

## Architecture Guidelines

- Maintain modular design principles
- Preserve performance-critical assembly optimizations
- Ensure memory efficiency (TSR size constraints)
- Maintain Packet Driver Specification compliance