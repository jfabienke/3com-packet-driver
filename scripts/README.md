# Build and Utility Scripts

This directory contains build scripts and utilities for the 3Com Packet Driver project.

## Build Scripts
- `build_error_handling_test.sh` - Error handling test build script

## Script Purpose

These scripts automate various build and testing processes during development:

### Build Automation
- Automated compilation of specific test modules
- Error handling and validation during build process
- Integration with the broader build system

### Usage
Scripts should be executed from the project root directory to ensure proper path resolution and build environment setup.

```bash
# Example usage
./scripts/build_error_handling_test.sh
```

### Development Workflow
These scripts support the iterative development process by providing quick and reliable ways to build and test specific components without requiring full project rebuilds.