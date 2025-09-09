# Changelog

All notable changes to the 3Com DOS Packet Driver will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- Initial alpha release of 3Com DOS Packet Driver
- Support for 65+ NIC models across ISA, PCI, and PCMCIA buses
- Vtable-based hardware abstraction layer (HAL)
- Three-tier memory architecture (Conventional/UMB/XMS)
- Multi-NIC support under single interrupt
- Bus mastering DMA for both ISA and PCI
- Packet Driver API v1.11 compliance
- Self-modifying code (SMC) for CPU optimization
- ARP cache with dynamic resolution
- Static routing tables with netmask support
- Flow-aware routing for connection symmetry
- Cache coherency management (4-tier system)
- VDS (Virtual DMA Services) support
- Comprehensive diagnostic and logging system
- TSR footprint under 6KB resident memory

### Known Issues
- Requires real hardware testing for validation
- Build requires Open Watcom C/C++ 1.9+ and NASM
- Some advanced features untested on actual hardware

## [0.1.0-alpha] - 2025-01-09

### Initial Features
- Core packet driver implementation
- Basic hardware detection and initialization
- Memory management subsystem
- Interrupt handling framework
- Configuration system via CONFIG.SYS parameters

### Hardware Support
- 3C509B (10 Mbps ISA)
- 3C515-TX (100 Mbps ISA with bus mastering)
- 3C589/3C589B (PCMCIA)
- Vortex/Boomerang/Cyclone PCI family

### Documentation
- Comprehensive feature documentation
- Architecture and design documents
- API reference
- Hardware compatibility matrix