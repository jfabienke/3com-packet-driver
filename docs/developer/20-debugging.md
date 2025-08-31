# Debugging and Troubleshooting

## Debug Build

Build with debug symbols:
```bash
wmake debug
```

## Diagnostic Features

### Logging
Enable comprehensive logging:
```
DEVICE=C:\NET\3CPD.COM /LOG=ON
```

### Statistics
View runtime statistics:
- Packet counters per NIC
- Error rates and types
- Memory usage statistics

## Common Issues

### Hardware Detection
- Verify NIC is properly seated
- Check for IRQ conflicts
- Ensure compatible processor (80286+)

### Memory Issues
- Load HIMEM.SYS for XMS support
- Check conventional memory availability
- Verify UMB configuration

### Performance Issues
- Enable bus mastering on 3C515-TX
- Check for IRQ sharing conflicts
- Verify network cable connections

## Tools

### Hardware Testing
See [nic-testing.md](nic-testing.md) for comprehensive hardware validation procedures.

### Memory Analysis
Use DOS memory utilities to verify driver memory usage and detect conflicts.

For user-level troubleshooting, see [../user/troubleshooting.md](../user/troubleshooting.md).