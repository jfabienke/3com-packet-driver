# Project Overview

This project is a sophisticated packet driver for DOS, supporting the 3Com 3C515-TX (100 Mbps Fast Ethernet) and 3Com 3C509B (10 Mbps Ethernet) network interface cards. It is designed for retro computing enthusiasts and vintage systems requiring robust networking capabilities.

The driver is written in a mix of C and Assembly, which is common for DOS TSR (Terminate and Stay Resident) programs. The core logic is written in C for maintainability, while performance-critical sections and low-level hardware interactions are implemented in Assembly.

The project has a clear and well-organized structure, with separate directories for source code (`src`), header files (`include`), documentation (`docs`), and tests (`tests`).

## Building and Running

The project is built using Open Watcom C/C++ and NASM. The build process is managed by a `Makefile`.

**Key Build Commands:**

*   `wmake`: Build the release version of the driver.
*   `wmake debug`: Build the debug version with symbols.
*   `wmake clean`: Clean the build directory.
*   `wmake test`: Build the driver and run the test suite.

The build output is `build/3cpd.com`, a DOS COM executable.

**Running the Driver:**

The driver is installed by adding a `DEVICE` line to `CONFIG.SYS`:

```
DEVICE=C:\PATH\TO\3CPD.COM [options]
```

The driver supports a wide range of configuration options, including I/O base addresses, IRQ assignments, network speed, and duplex mode.

## Development Conventions

The codebase is well-documented, with header files providing clear function prototypes and comments. The code is organized into modules with a clear separation of concerns.

The project includes a comprehensive test suite with over 85% code coverage. The tests are located in the `tests` directory and can be run using the `run_tests.sh` script.

The project has a layered architecture, with a hardware abstraction layer, a core driver, and a packet driver API. This modular design allows for easy extension and maintenance.
