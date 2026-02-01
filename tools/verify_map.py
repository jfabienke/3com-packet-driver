#!/usr/bin/env python3
"""
verify_map.py - Watcom Linker Map File Verification
3Com Packet Driver - Overlay Safety Verification

Last Updated: 2026-02-01 12:41:33 CET

Parses build/3cpd.map and verifies:
1. DGROUP size is under 64KB (with safety margin warning)
2. Required symbols exist and are in ROOT segment
3. Required TSR infrastructure segments exist and are in ROOT
4. Optional symbols, if present, are in ROOT (not overlays)
5. No unexpected segment folding into DGROUP

Usage:
    python3 tools/verify_map.py build/3cpd.map
    python3 tools/verify_map.py build/3cpd.map --verbose

Exit codes:
    0 = All checks passed
    1 = Verification failures found
"""

import sys
import re
from pathlib import Path

# ============================================================================
# DGROUP SIZE THRESHOLDS
# ============================================================================
DGROUP_MAX_SIZE = 0x10000       # 64KB hard limit (segment overflow)

# Build-mode specific thresholds
# Release (INIT_DIAG off): stricter, expect ~8KB savings from guarded diagnostics
DGROUP_RELEASE_MAX = 0xE800     # ~59KB - release must be well under limit
DGROUP_RELEASE_WARN = 0xE000    # ~57KB - release warning threshold

# Production (NO_LOGGING, NO_STATS, NDEBUG): tightest limits, smallest footprint
DGROUP_PRODUCTION_MAX = 0xE000  # ~57KB - production must be minimal
DGROUP_PRODUCTION_WARN = 0xD800 # ~55KB - production warning threshold

# Debug (INIT_DIAG on): allow red zone since diagnostics are included
DGROUP_DEBUG_MAX = 0x10000      # 64KB hard limit
DGROUP_DEBUG_WARN = 0xF200      # ~62KB warning threshold (safety margin)

# Default (no mode specified) - use debug thresholds for backwards compatibility
DGROUP_WARN_SIZE = 0xF200       # ~62KB warning threshold (safety margin)
DGROUP_TARGET_SIZE = 0xF000     # ~61KB ideal target

# ============================================================================
# REQUIRED SYMBOLS - Must exist AND must be in ROOT
# Failure to find these is an ERROR (naming changed / build broken)
# ============================================================================
REQUIRED_SYMBOLS = {
    # Packet Driver API entry (INT 60h) - at least one must exist
    'packet_api': [
        'packet_driver_isr',
        '_packet_driver_isr',
        'packet_api_entry',
    ],
    # Multiplex API entry (INT 2Fh) - at least one must exist
    'multiplex_api': [
        'multiplex_handler_',
        'int2f_handler',
        'multiplex_handler',
        '_int2f_isr',
    ],
    # PCI BIOS shim entry (INT 1Ah) - at least one must exist
    'pci_shim': [
        '_pci_shim_isr',
        'pci_shim_handler',
        'pci_shim_handler_',
    ],
    # NIC IRQ dispatcher - at least one must exist
    'nic_irq': [
        'nic_irq_handler',
        '_nic_isr',
        'hardware_handle_3c509b_irq',
        'hardware_handle_3c515_irq',
    ],
    # Install/uninstall lifecycle - at least one must exist
    'lifecycle': [
        'install_packet_api_vector',
        'install_interrupts',
        'initialize_tsr_defense',
    ],
}

# ============================================================================
# REQUIRED SEGMENTS - Must exist AND must be in ROOT (AUTO group)
# These are the TSR infrastructure modules that must be resident
# ============================================================================
REQUIRED_ROOT_SEGMENTS = [
    # Main ASM code segment (contains pktapi, nicirq, tsrcom, pciisr, etc.)
    ('_TEXT', 'Main ASM modules (pktapi, nicirq, tsrcom, pciisr)'),
    # Phase 6: 15 *_rt.c consolidated into rt_stubs.c
    ('rt_stubs_TEXT', 'Consolidated runtime stubs (replaces 15 *_rt modules)'),
]

# ============================================================================
# OPTIONAL SYMBOLS - If present, must be in ROOT (not overlay)
# "Not found" is OK; "found in overlay" is ERROR
# ============================================================================
OPTIONAL_SYMBOLS = [
    # NIC vtables
    'g_3c509b_ops', 'g_3c515_ops', '_3c509b_ops',

    # NIC ops functions (runtime only - init functions are in overlay by design)
    '_3c509b_send_packet_', '_3c509b_receive_packet_',
    '_3c515_send_packet_', '_3c515_receive_packet_',

    # Logging API
    'log_info_', 'log_error_', 'log_warning_', 'log_debug_',

    # Additional ISR/handler symbols
    'nic_3c509_handler', 'nic_3c515_handler',
    'pcmcia_irq_isr', 'pcmcia_isr_install', 'pcmcia_isr_uninstall',
    '_set_chain_vector', 'chain_to_bios',

    # Deferred work
    'queue_deferred_work', 'deferred_work_queue_process',

    # Vector management
    'validate_interrupt_vectors', 'safe_port_read',
]

# ============================================================================
# FORBIDDEN OVERLAY OBJECTS
# ============================================================================
FORBIDDEN_IN_OVERLAY = [
    # Runtime objects must never be in overlay (called from ISR/packet path)
    'rt_stubs.obj',  # Phase 6: consolidated runtime stubs
    '3cvortex.obj', '3cboom.obj',
]


class MapVerifier:
    def __init__(self, map_file, build_mode='debug'):
        self.map_file = Path(map_file)
        self.build_mode = build_mode
        self.content = ''
        self.errors = []
        self.warnings = []
        self.info = []
        self.dgroup_size = 0
        self.segments = {}
        self.symbols = {}
        self.overlay_segments = set()

        # Set thresholds based on build mode
        if build_mode == 'production':
            self.dgroup_max = DGROUP_PRODUCTION_MAX
            self.dgroup_warn = DGROUP_PRODUCTION_WARN
        elif build_mode == 'release':
            self.dgroup_max = DGROUP_RELEASE_MAX
            self.dgroup_warn = DGROUP_RELEASE_WARN
        else:  # debug or default
            self.dgroup_max = DGROUP_DEBUG_MAX
            self.dgroup_warn = DGROUP_DEBUG_WARN

    def parse(self):
        """Parse the Watcom map file."""
        if not self.map_file.exists():
            self.errors.append(f"Map file not found: {self.map_file}")
            return False

        self.content = self.map_file.read_text()
        self._parse_dgroup_size()
        self._parse_all_segments()
        self._parse_symbol_table()
        self._identify_overlay_segments()
        return True

    def _parse_dgroup_size(self):
        """Extract DGROUP size from map file."""
        match = re.search(
            r'DGROUP\s+[0-9A-Fa-f]+:[0-9A-Fa-f]+\s+([0-9A-Fa-f]+)',
            self.content
        )
        if match:
            self.dgroup_size = int(match.group(1), 16)

    def _parse_all_segments(self):
        """Parse all segment definitions from map file."""
        segment_pattern = re.compile(
            r'^(\w+)\s+(CODE|DATA|BSS|STACK|BEGDATA|FAR_DATA|EMU|CONST|_OVLCODE)\s+'
            r'(\w+)\s+([0-9A-Fa-f]+:[0-9A-Fa-f]+)\s+([0-9A-Fa-f]+)',
            re.MULTILINE
        )

        for match in segment_pattern.finditer(self.content):
            seg_name = match.group(1)
            seg_class = match.group(2)
            seg_group = match.group(3)
            seg_addr = match.group(4)
            seg_size = match.group(5)

            self.segments[seg_name] = {
                'class': seg_class,
                'group': seg_group,
                'address': seg_addr,
                'size': int(seg_size, 16),
            }

    def _parse_symbol_table(self):
        """Parse symbol addresses from map file."""
        # Pattern: "0000:1234*     symbol_name" or "0000:1234      symbol_name"
        symbol_pattern = re.compile(
            r'^([0-9A-Fa-f]+):([0-9A-Fa-f]+)[*+]?\s+(\S+)',
            re.MULTILINE
        )

        for match in symbol_pattern.finditer(self.content):
            segment = match.group(1)
            offset = match.group(2)
            symbol = match.group(3)
            self.symbols[symbol] = {
                'segment': segment,
                'offset': offset,
                'full_addr': f"{segment}:{offset}"
            }

    def _identify_overlay_segments(self):
        """Identify overlay segment addresses."""
        # Find overlay section markers
        for match in re.finditer(r'Overlay section \d+ address ([0-9A-Fa-f]+):', self.content):
            self.overlay_segments.add(match.group(1).upper())

    def _is_symbol_in_overlay(self, symbol_info):
        """Check if a symbol is in an overlay segment."""
        seg = symbol_info['segment'].upper()
        return seg in self.overlay_segments

    def _is_segment_root(self, seg_name):
        """Check if a segment is in ROOT (AUTO group, CODE class)."""
        if seg_name not in self.segments:
            return None  # Not found
        seg_info = self.segments[seg_name]
        return seg_info.get('group') == 'AUTO' and seg_info.get('class') == 'CODE'

    def verify_dgroup_size(self):
        """Verify DGROUP size is within limits."""
        print(f"\n=== Checking DGROUP Size ({self.build_mode.upper()} mode) ===")

        if self.dgroup_size == 0:
            self.warnings.append("Could not parse DGROUP size from map file")
            return

        size_kb = self.dgroup_size / 1024
        headroom = DGROUP_MAX_SIZE - self.dgroup_size
        print(f"  DGROUP: 0x{self.dgroup_size:X} ({self.dgroup_size:,} bytes, {size_kb:.1f} KB)")
        print(f"  Headroom: {headroom:,} bytes ({headroom/1024:.1f} KB)")
        print(f"  Mode thresholds: max=0x{self.dgroup_max:X}, warn=0x{self.dgroup_warn:X}")

        if self.dgroup_size >= self.dgroup_max:
            self.errors.append(
                f"CRITICAL: DGROUP 0x{self.dgroup_size:X} EXCEEDS {self.build_mode} limit 0x{self.dgroup_max:X}!"
            )
        elif self.dgroup_size >= self.dgroup_warn:
            self.warnings.append(
                f"DGROUP 0x{self.dgroup_size:X} in WARNING ZONE for {self.build_mode} build! "
                f"Only {headroom:,} bytes headroom."
            )
        else:
            print(f"  Status: OK")

    def verify_required_symbols(self):
        """Verify required symbol groups - at least one per group must exist in ROOT."""
        print("\n=== Checking Required Symbols ===")

        for group_name, symbol_list in REQUIRED_SYMBOLS.items():
            found_any = False
            found_in_root = False
            found_symbol = None

            for symbol in symbol_list:
                if symbol in self.symbols:
                    found_any = True
                    found_symbol = symbol
                    if not self._is_symbol_in_overlay(self.symbols[symbol]):
                        found_in_root = True
                        break

            if not found_any:
                self.errors.append(
                    f"REQUIRED [{group_name}]: No symbol found from {symbol_list}\n"
                    f"  Build may be broken or symbols not exported."
                )
                print(f"  FAIL: {group_name} - no symbols found")
            elif not found_in_root:
                self.errors.append(
                    f"REQUIRED [{group_name}]: Found '{found_symbol}' but it's in OVERLAY!\n"
                    f"  This will crash after overlay is discarded."
                )
                print(f"  FAIL: {group_name} - '{found_symbol}' in overlay!")
            else:
                print(f"  OK: {group_name} - '{found_symbol}' in ROOT")

    def verify_required_segments(self):
        """Verify required TSR infrastructure segments are in ROOT."""
        print("\n=== Checking Required ROOT Segments ===")

        for seg_name, description in REQUIRED_ROOT_SEGMENTS:
            is_root = self._is_segment_root(seg_name)

            if is_root is None:
                self.warnings.append(f"Segment {seg_name} not found ({description})")
                print(f"  WARN: {seg_name} not found")
            elif is_root:
                size = self.segments[seg_name]['size']
                print(f"  OK: {seg_name} in ROOT ({size:,} bytes)")
            else:
                seg_info = self.segments[seg_name]
                self.errors.append(
                    f"CRITICAL: {seg_name} NOT in ROOT!\n"
                    f"  {description}\n"
                    f"  Found: group={seg_info['group']}, class={seg_info['class']}"
                )
                print(f"  FAIL: {seg_name} not in ROOT!")

    def verify_optional_symbols(self):
        """Verify optional symbols, if present, are in ROOT."""
        print("\n=== Checking Optional Symbols ===")

        found_count = 0
        overlay_count = 0

        for symbol in OPTIONAL_SYMBOLS:
            # Try exact match and common variants
            for variant in [symbol, symbol.rstrip('_'), symbol + '_']:
                if variant in self.symbols:
                    found_count += 1
                    if self._is_symbol_in_overlay(self.symbols[variant]):
                        overlay_count += 1
                        self.errors.append(
                            f"Optional symbol '{variant}' found in OVERLAY!"
                        )
                    break

        print(f"  Found: {found_count}, In overlay (FAIL): {overlay_count}")

    def verify_forbidden_in_overlay(self):
        """Verify forbidden objects are NOT in overlay sections."""
        print("\n=== Checking Forbidden Overlay Placements ===")

        for obj in FORBIDDEN_IN_OVERLAY:
            obj_base = obj.replace('.obj', '')
            seg_name = f"{obj_base}_TEXT"

            if seg_name in self.segments:
                seg_info = self.segments[seg_name]
                if seg_info.get('class') == '_OVLCODE':
                    self.errors.append(f"CRITICAL: {obj} is in OVERLAY!")
                else:
                    print(f"  OK: {obj} in ROOT")
            else:
                print(f"  INFO: {obj} segment not found (may be in _TEXT)")

    def verify_dgroup_folding(self):
        """Check for unexpected segment folding into DGROUP."""
        print("\n=== Checking DGROUP Composition ===")

        dgroup_segments = []
        for seg_name, info in self.segments.items():
            if info.get('group') == 'DGROUP':
                dgroup_segments.append((seg_name, info.get('size', 0)))

        # Check for overlay segments folded into DGROUP
        for seg_name, size in dgroup_segments:
            if 'OVL_' in seg_name or 'INIT_' in seg_name:
                self.errors.append(
                    f"CRITICAL: Overlay segment {seg_name} folded into DGROUP!"
                )

        # Show composition
        dgroup_segments.sort(key=lambda x: -x[1])
        print(f"  Segments: {len(dgroup_segments)}")
        for seg_name, size in dgroup_segments[:5]:
            print(f"    {seg_name}: {size:,} bytes")

    def print_summary(self):
        """Print verification summary."""
        print("\n" + "=" * 60)
        print(f"VERIFICATION SUMMARY ({self.build_mode.upper()} mode)")
        print("=" * 60)

        headroom = DGROUP_MAX_SIZE - self.dgroup_size
        print(f"\nDGROUP: 0x{self.dgroup_size:X} ({self.dgroup_size:,} bytes)")
        print(f"Headroom: {headroom:,} bytes ({headroom/1024:.1f} KB)")
        print(f"Mode limits: max=0x{self.dgroup_max:X}, warn=0x{self.dgroup_warn:X}")

        if self.errors:
            print(f"\n[ERRORS] ({len(self.errors)}):")
            for err in self.errors:
                print(f"  - {err}")

        if self.warnings:
            print(f"\n[WARNINGS] ({len(self.warnings)}):")
            for warn in self.warnings:
                print(f"  - {warn}")

        if not self.errors:
            if self.warnings:
                print("\n  PASSED with warnings")
            else:
                print("\n  All checks PASSED!")

        print("\n" + "=" * 60)
        return len(self.errors) == 0

    def dump_verbose(self):
        """Dump detailed info for debugging."""
        print("\n=== Overlay Segments ===")
        print(f"  Addresses: {sorted(self.overlay_segments)}")

        print("\n=== All Code Segments ===")
        for seg, info in sorted(self.segments.items()):
            if info.get('class') == 'CODE':
                print(f"  {seg}: {info['group']} @ {info['address']} ({info['size']:X})")


def main():
    if len(sys.argv) < 2:
        print("Usage: python3 verify_map.py <map_file> [--release|--debug|--production] [--verbose]")
        print("\nVerifies overlay safety for 3Com Packet Driver TSR:")
        print("  - DGROUP size within limits (mode-specific thresholds)")
        print("  - Required symbols exist and are in ROOT")
        print("  - Required TSR segments in ROOT")
        print("  - Optional symbols not in overlay")
        print("\nBuild modes:")
        print("  --production  Tightest limits (NO_LOGGING, NO_STATS, NDEBUG)")
        print("                max=0xE000 (~57KB), warn=0xD800 (~55KB)")
        print("  --release     Stricter limits (INIT_DIAG off, expect ~8KB savings)")
        print("                max=0xE800 (~59KB), warn=0xE000 (~57KB)")
        print("  --debug       Allows red zone (INIT_DIAG on, full diagnostics)")
        print("                max=0x10000 (64KB), warn=0xF200 (~62KB)")
        print("  (default)     Same as --debug for backwards compatibility")
        sys.exit(1)

    # Determine build mode
    build_mode = 'debug'  # default
    if '--production' in sys.argv:
        build_mode = 'production'
    elif '--release' in sys.argv:
        build_mode = 'release'
    elif '--debug' in sys.argv:
        build_mode = 'debug'

    verifier = MapVerifier(sys.argv[1], build_mode=build_mode)

    if not verifier.parse():
        print("Failed to parse map file")
        sys.exit(1)

    verifier.verify_dgroup_size()
    verifier.verify_required_symbols()
    verifier.verify_required_segments()
    verifier.verify_optional_symbols()
    verifier.verify_forbidden_in_overlay()
    verifier.verify_dgroup_folding()

    if '--verbose' in sys.argv:
        verifier.dump_verbose()

    success = verifier.print_summary()
    sys.exit(0 if success else 1)


if __name__ == '__main__':
    main()
