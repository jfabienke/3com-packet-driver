# Makefile.wat - wmake-compatible Makefile for 3Com Packet Driver
# Use: wmake -f Makefile.wat [target]
# Requires Open Watcom C/C++ and NASM
#
# Last Updated: 2026-01-23 16:52 UTC

# --- Directories ---
SRCDIR     = src
CDIR       = $(SRCDIR)\c
ASMDIR     = $(SRCDIR)\asm
INCDIR     = include
BUILDDIR   = build

# --- Tools ---
CC         = wcc
ASM        = nasm
LINK       = wlink

# --- Compiler Flags (C) ---
# -zq: Quiet mode
# -ms: Small memory model
# -s:  Remove stack overflow checks
# -os: Optimize for space
# -ot: Optimize for time
# -zp1: Pack structures on 1-byte boundaries
# -zdf: DS != DGROUP
# -zu: SS != DS, no stack probes (critical for TSR)

CFLAGS_DEBUG   = -zq -ms -s -0 -zp1 -zdf -zu -i=$(INCDIR) -fr=$(BUILDDIR)\ -wcd=201 -d2
CFLAGS_RELEASE = -zq -ms -s -os -ot -zp1 -zdf -zu -i=$(INCDIR) -fr=$(BUILDDIR)\ -wcd=201 -d0
CFLAGS_PRODUCTION = -zq -ms -s -os -zp1 -zdf -zu -i=$(INCDIR) -d0 &
                    -oe=100 -ol+ -ox -wcd=201 -we &
                    -dPRODUCTION -dNO_LOGGING -dNO_STATS -dNDEBUG

# --- Assembler Flags (NASM) ---
AFLAGS_DEBUG   = -f obj -i$(INCDIR)\ -l $(BUILDDIR)\ -g
AFLAGS_RELEASE = -f obj -i$(INCDIR)\ -l $(BUILDDIR)\

# --- Linker Flags ---
LFLAGS_DEBUG   = system dos option map=$(BUILDDIR)\3cpd.map, caseexact, quiet, stack=1024
LFLAGS_RELEASE = system dos option map=$(BUILDDIR)\3cpd.map, caseexact, quiet, stack=1024
LFLAGS_PRODUCTION = system dos option map=$(BUILDDIR)\3cpd.map, caseexact, quiet, stack=1024

# --- Default flags (release) ---
CFLAGS = $(CFLAGS_RELEASE)
AFLAGS = $(AFLAGS_RELEASE)
LFLAGS = $(LFLAGS_RELEASE)

# --- Target ---
TARGET     = $(BUILDDIR)\3cpd.exe

# --- PCI Utilities ---
PCI_UTILS = $(BUILDDIR)\pcitest.exe &
            $(BUILDDIR)\pciscan.exe &
            $(BUILDDIR)\pcictl.exe &
            $(BUILDDIR)\pcidump.exe

# --- HOT SECTION - Resident Assembly Objects ---
HOT_ASM_OBJS = $(BUILDDIR)\packet_api_smc.obj &
               $(BUILDDIR)\nic_irq_smc.obj &
               $(BUILDDIR)\hardware_smc.obj &
               $(BUILDDIR)\pcmcia_isr.obj &
               $(BUILDDIR)\flow_routing.obj &
               $(BUILDDIR)\direct_pio.obj &
               $(BUILDDIR)\packet_ops.obj &
               $(BUILDDIR)\packet_copy_c_wrapper.obj &
               $(BUILDDIR)\tsr_common.obj &
               $(BUILDDIR)\tsr_c_wrappers.obj &
               $(BUILDDIR)\pci_io.obj &
               $(BUILDDIR)\pci_shim_isr.obj

# --- Main loader ---
LOADER_OBJ = $(BUILDDIR)\tsr_loader.obj

# --- HOT SECTION - Resident C Objects ---
# Note: packet_ops is in HOT_ASM_OBJS (assembly implementation preferred)
HOT_C_OBJS = $(BUILDDIR)\api.obj &
             $(BUILDDIR)\routing.obj &
             $(BUILDDIR)\pci_shim.obj &
             $(BUILDDIR)\pci_multiplex.obj &
             $(BUILDDIR)\dma_mapping.obj &
             $(BUILDDIR)\dma_boundary.obj &
             $(BUILDDIR)\hw_checksum.obj &
             $(BUILDDIR)\dos_idle.obj &
             $(BUILDDIR)\irq_bind.obj &
             $(BUILDDIR)\runtime_config.obj &
             $(BUILDDIR)\interrupt_mitigation.obj &
             $(BUILDDIR)\rx_batch_refill.obj &
             $(BUILDDIR)\tx_lazy_irq.obj

# --- COLD SECTION - Initialization Assembly Objects ---
COLD_ASM_OBJS = $(BUILDDIR)\cpu_detect.obj &
                $(BUILDDIR)\pnp.obj &
                $(BUILDDIR)\promisc.obj &
                $(BUILDDIR)\smc_patches.obj &
                $(BUILDDIR)\safety_stubs.obj &
                $(BUILDDIR)\quiesce.obj

# --- COLD SECTION - Initialization C Objects ---
COLD_C_OBJS_BASE = $(BUILDDIR)\main.obj &
                   $(BUILDDIR)\init.obj &
                   $(BUILDDIR)\config.obj &
                   $(BUILDDIR)\pcmcia_manager.obj &
                   $(BUILDDIR)\pcmcia_snapshot.obj &
                   $(BUILDDIR)\flow_control.obj &
                   $(BUILDDIR)\pcmcia_pe_backend.obj &
                   $(BUILDDIR)\pcmcia_ss_backend.obj &
                   $(BUILDDIR)\memory.obj &
                   $(BUILDDIR)\xms_detect.obj &
                   $(BUILDDIR)\umb_loader.obj &
                   $(BUILDDIR)\eeprom.obj &
                   $(BUILDDIR)\buffer_alloc.obj &
                   $(BUILDDIR)\buffer_autoconfig.obj &
                   $(BUILDDIR)\static_routing.obj &
                   $(BUILDDIR)\arp.obj &
                   $(BUILDDIR)\nic_init.obj &
                   $(BUILDDIR)\hardware.obj &
                   $(BUILDDIR)\hardware_stubs.obj &
                   $(BUILDDIR)\3c515.obj &
                   $(BUILDDIR)\3c509b.obj &
                   $(BUILDDIR)\entry_validation.obj &
                   $(BUILDDIR)\platform_probe_early.obj &
                   $(BUILDDIR)\dma_capability_test.obj &
                   $(BUILDDIR)\tsr_manager.obj &
                   $(BUILDDIR)\dma_tests.obj &
                   $(BUILDDIR)\dma_safety.obj &
                   $(BUILDDIR)\vds_core.obj &
                   $(BUILDDIR)\vds_safety.obj &
                   $(BUILDDIR)\vds_manager.obj &
                   $(BUILDDIR)\extension_api.obj &
                   $(BUILDDIR)\unwind.obj &
                   $(BUILDDIR)\chipset_detect.obj &
                   $(BUILDDIR)\busmaster_test.obj &
                   $(BUILDDIR)\loader_cpu_detect.obj &
                   $(BUILDDIR)\loader_patch_apply.obj &
                   $(BUILDDIR)\pci_bios.obj &
                   $(BUILDDIR)\3com_pci_detect.obj &
                   $(BUILDDIR)\3com_vortex.obj &
                   $(BUILDDIR)\3com_boomerang.obj &
                   $(BUILDDIR)\pci_integration.obj &
                   $(BUILDDIR)\pci_shim_enhanced.obj &
                   $(BUILDDIR)\smc_safety_patches.obj &
                   $(BUILDDIR)\smc_serialization.obj &
                   $(BUILDDIR)\cache_management.obj &
                   $(BUILDDIR)\dma_policy.obj &
                   $(BUILDDIR)\vds.obj

# --- Debug-only objects ---
DEBUG_C_OBJS = $(BUILDDIR)\diagnostics.obj &
               $(BUILDDIR)\logging.obj &
               $(BUILDDIR)\stats.obj

# --- Conditional inclusion ---
!ifdef PRODUCTION
COLD_C_OBJS = $(COLD_C_OBJS_BASE)
ALL_OBJS = $(LOADER_OBJ) $(HOT_ASM_OBJS) $(HOT_C_OBJS) $(COLD_ASM_OBJS) $(COLD_C_OBJS)
!else
COLD_C_OBJS = $(COLD_C_OBJS_BASE) $(DEBUG_C_OBJS)
ALL_OBJS = $(LOADER_OBJ) $(HOT_ASM_OBJS) $(HOT_C_OBJS) $(COLD_ASM_OBJS) $(COLD_C_OBJS)
!endif

# --- Suffix Rules ---
.EXTENSIONS: .exe .obj .c .asm

# --- Build Targets ---

all: .SYMBOLIC
    @%make release

release: .SYMBOLIC $(BUILDDIR) $(TARGET)
    @echo Release build complete: $(TARGET)

debug: .SYMBOLIC $(BUILDDIR)
    @set CFLAGS=$(CFLAGS_DEBUG)
    @set AFLAGS=$(AFLAGS_DEBUG)
    @set LFLAGS=$(LFLAGS_DEBUG)
    @%make $(TARGET)
    @echo Debug build complete: $(TARGET)

production: .SYMBOLIC $(BUILDDIR)
    @echo Building PRODUCTION version...
    @set CFLAGS=$(CFLAGS_PRODUCTION)
    @set LFLAGS=$(LFLAGS_PRODUCTION)
    @set PRODUCTION=1
    @%make $(TARGET)
    @echo Production build complete: $(TARGET)

# --- CPU Configuration Targets ---

config-8086: .SYMBOLIC
    @echo =============================================
    @echo Configuring for 8086/8088...
    @echo =============================================
    @echo Features: 3C509B NIC only, PIO mode, no SMC
    @echo =============================================
    @set TARGET_CPU=8086
    @set NIC_SUPPORT=3c509b
    @set ENABLE_BUS_MASTER=0
    @%make release

config-286: .SYMBOLIC
    @echo Configuring for 80286...
    @set TARGET_CPU=286
    @%make release

config-386: .SYMBOLIC
    @echo Configuring for 80386...
    @set TARGET_CPU=386
    @%make release

config-486: .SYMBOLIC
    @echo Configuring for 80486...
    @set TARGET_CPU=486
    @%make release

# --- NIC Configuration Targets ---

config-3c509b: .SYMBOLIC
    @echo Configuring for 3C509B only...
    @set NIC_SUPPORT=3c509b
    @%make release

config-3c515: .SYMBOLIC
    @echo Configuring for 3C515 only...
    @set NIC_SUPPORT=3c515
    @%make release

config-both: .SYMBOLIC
    @echo Configuring for both NICs...
    @set NIC_SUPPORT=both
    @%make release

# --- Optimized Builds ---

build-minimal: .SYMBOLIC
    @echo Building minimal configuration...
    @set NIC_SUPPORT=3c509b
    @set TARGET_CPU=286
    @set ENABLE_LOGGING=0
    @%make release

build-8086-minimal: .SYMBOLIC
    @echo Building ultra-minimal 8086 configuration...
    @set NIC_SUPPORT=3c509b
    @set TARGET_CPU=8086
    @set ENABLE_BUS_MASTER=0
    @set ENABLE_XMS=0
    @set ENABLE_VDS=0
    @%make release

build-full: .SYMBOLIC
    @echo Building full configuration...
    @set NIC_SUPPORT=both
    @set TARGET_CPU=386
    @set ENABLE_LOGGING=1
    @%make release

build-performance: .SYMBOLIC
    @echo Building performance-optimized configuration...
    @set NIC_SUPPORT=both
    @set TARGET_CPU=386
    @set ENABLE_DIRECT_PIO=1
    @%make release

# --- PCI Utilities ---

pci-utils: .SYMBOLIC $(PCI_UTILS)
    @echo All PCI utilities built successfully

$(BUILDDIR)\pcitest.exe: $(BUILDDIR)\pcitest.obj $(BUILDDIR)\pci_bios.obj &
                         $(BUILDDIR)\pci_shim.obj $(BUILDDIR)\pci_shim_enhanced.obj &
                         $(BUILDDIR)\pci_io.obj $(BUILDDIR)\logging.obj
    @echo Linking PCI Test Suite...
    $(LINK) $(LFLAGS) file {$<} name $@

$(BUILDDIR)\pciscan.exe: $(BUILDDIR)\pciscan.obj $(BUILDDIR)\pci_bios.obj &
                         $(BUILDDIR)\pci_shim_enhanced.obj $(BUILDDIR)\pci_io.obj &
                         $(BUILDDIR)\cpu_detect.obj $(BUILDDIR)\logging.obj
    @echo Linking PCI Scanner...
    $(LINK) $(LFLAGS) file {$<} name $@

$(BUILDDIR)\pcictl.exe: $(BUILDDIR)\pcictl.obj
    @echo Linking PCI Control utility...
    $(LINK) $(LFLAGS) file {$<} name $@

$(BUILDDIR)\pcidump.exe: $(BUILDDIR)\pcidump.obj $(BUILDDIR)\pci_bios.obj $(BUILDDIR)\pci_io.obj
    @echo Linking PCI Config Dump utility...
    $(LINK) $(LFLAGS) file {$<} name $@

# --- Build Directory ---

$(BUILDDIR): .SYMBOLIC
    @if not exist $(BUILDDIR) mkdir $(BUILDDIR)
    @if not exist $(BUILDDIR)\loader mkdir $(BUILDDIR)\loader

# --- Main Target ---

$(TARGET): $(ALL_OBJS)
    @echo Linking $@ with hot/cold section separation...
    $(LINK) $(LFLAGS) file {$(ALL_OBJS)} name $@
    @echo Build complete: $@

# --- C Source Compilation Rules ---
# Note: wmake suffix rules use $[* for stem

# Main C files
$(BUILDDIR)\main.obj: $(CDIR)\main.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)\init.obj: $(CDIR)\init.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)\api.obj: $(CDIR)\api.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)\config.obj: $(CDIR)\config.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)\hardware.obj: $(CDIR)\hardware.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)\hardware_stubs.obj: $(CDIR)\hardware_stubs.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)\memory.obj: $(CDIR)\memory.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)\xms_detect.obj: $(CDIR)\xms_detect.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)\buffer_alloc.obj: $(CDIR)\buffer_alloc.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)\buffer_autoconfig.obj: $(CDIR)\buffer_autoconfig.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)\routing.obj: $(CDIR)\routing.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)\static_routing.obj: $(CDIR)\static_routing.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)\arp.obj: $(CDIR)\arp.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)\nic_init.obj: $(CDIR)\nic_init.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)\3c515.obj: $(CDIR)\3c515.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)\3c509b.obj: $(CDIR)\3c509b.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)\diagnostics.obj: $(CDIR)\diagnostics.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)\logging.obj: $(CDIR)\logging.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)\stats.obj: $(CDIR)\stats.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)\pci_shim.obj: $(CDIR)\pci_shim.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)\pci_multiplex.obj: $(CDIR)\pci_multiplex.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)\pci_bios.obj: $(CDIR)\pci_bios.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)\pci_shim_enhanced.obj: $(CDIR)\pci_shim_enhanced.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)\pci_integration.obj: $(CDIR)\pci_integration.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)\3com_pci_detect.obj: $(CDIR)\3com_pci_detect.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)\3com_vortex.obj: $(CDIR)\3com_vortex.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)\3com_boomerang.obj: $(CDIR)\3com_boomerang.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)\dma_mapping.obj: $(CDIR)\dma_mapping.c
    @echo Compiling (DMA): $[@
    $(CC) $(CFLAGS) -dPRODUCTION -dNO_LOGGING -dNDEBUG $[@ -fo=$@

$(BUILDDIR)\dma_boundary.obj: $(CDIR)\dma_boundary.c
    @echo Compiling (DMA): $[@
    $(CC) $(CFLAGS) -dPRODUCTION -dNO_LOGGING -dNDEBUG $[@ -fo=$@

$(BUILDDIR)\hw_checksum.obj: $(CDIR)\hw_checksum.c
    @echo Compiling (DMA): $[@
    $(CC) $(CFLAGS) -dPRODUCTION -dNO_LOGGING -dNDEBUG $[@ -fo=$@

$(BUILDDIR)\dma_safety.obj: $(CDIR)\dma_safety.c
    @echo Compiling (DMA): $[@
    $(CC) $(CFLAGS) -dPRODUCTION -dNO_LOGGING -dNDEBUG $[@ -fo=$@

$(BUILDDIR)\dma_tests.obj: $(CDIR)\dma_tests.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)\dma_policy.obj: $(CDIR)\dma_policy.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)\dma_capability_test.obj: $(CDIR)\dma_capability_test.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)\vds.obj: $(CDIR)\vds.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)\vds_core.obj: $(CDIR)\vds_core.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)\vds_safety.obj: $(CDIR)\vds_safety.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)\vds_manager.obj: $(CDIR)\vds_manager.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)\dos_idle.obj: $(CDIR)\dos_idle.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)\irq_bind.obj: $(CDIR)\irq_bind.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)\runtime_config.obj: $(CDIR)\runtime_config.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)\interrupt_mitigation.obj: $(CDIR)\interrupt_mitigation.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)\rx_batch_refill.obj: $(CDIR)\rx_batch_refill.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)\tx_lazy_irq.obj: $(CDIR)\tx_lazy_irq.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)\pcmcia_manager.obj: $(CDIR)\pcmcia_manager.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)\pcmcia_snapshot.obj: $(CDIR)\pcmcia_snapshot.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)\pcmcia_pe_backend.obj: $(CDIR)\pcmcia_pe_backend.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)\pcmcia_ss_backend.obj: $(CDIR)\pcmcia_ss_backend.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)\flow_control.obj: $(CDIR)\flow_control.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)\umb_loader.obj: $(CDIR)\umb_loader.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)\eeprom.obj: $(CDIR)\eeprom.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)\entry_validation.obj: $(CDIR)\entry_validation.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)\platform_probe_early.obj: $(CDIR)\platform_probe_early.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)\tsr_manager.obj: $(CDIR)\tsr_manager.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)\extension_api.obj: $(CDIR)\extension_api.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)\unwind.obj: $(CDIR)\unwind.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)\chipset_detect.obj: $(CDIR)\chipset_detect.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)\busmaster_test.obj: $(CDIR)\busmaster_test.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)\smc_safety_patches.obj: $(CDIR)\smc_safety_patches.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)\smc_serialization.obj: $(CDIR)\smc_serialization.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)\cache_management.obj: $(CDIR)\cache_management.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

# Loader C files
$(BUILDDIR)\loader_cpu_detect.obj: $(SRCDIR)\loader\cpu_detect.c
    @echo Compiling (loader): $[@
    $(CC) $(CFLAGS) -dCOLD_SECTION $[@ -fo=$@

$(BUILDDIR)\loader_patch_apply.obj: $(SRCDIR)\loader\patch_apply.c
    @echo Compiling (loader): $[@
    $(CC) $(CFLAGS) -dCOLD_SECTION $[@ -fo=$@

# PCI utility standalone compiles
$(BUILDDIR)\pcitest.obj: $(CDIR)\pcitest.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)\pciscan.obj: $(CDIR)\pciscan.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)\pcictl.obj: $(CDIR)\pci_multiplex.c
    @echo Compiling (standalone): $[@
    $(CC) $(CFLAGS) -dSTANDALONE_UTILITY $[@ -fo=$@

$(BUILDDIR)\pcidump.obj: $(CDIR)\pcidump.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

# --- Assembly Source Compilation Rules ---

$(BUILDDIR)\tsr_loader.obj: $(ASMDIR)\tsr_loader.asm
    @echo Assembling: $[@
    $(ASM) $(AFLAGS) $[@ -o $@

$(BUILDDIR)\packet_api_smc.obj: $(ASMDIR)\packet_api_smc.asm
    @echo Assembling: $[@
    $(ASM) $(AFLAGS) $[@ -o $@

$(BUILDDIR)\nic_irq_smc.obj: $(ASMDIR)\nic_irq_smc.asm
    @echo Assembling: $[@
    $(ASM) $(AFLAGS) $[@ -o $@

$(BUILDDIR)\hardware_smc.obj: $(ASMDIR)\hardware_smc.asm
    @echo Assembling: $[@
    $(ASM) $(AFLAGS) $[@ -o $@

$(BUILDDIR)\pcmcia_isr.obj: $(ASMDIR)\pcmcia_isr.asm
    @echo Assembling: $[@
    $(ASM) $(AFLAGS) $[@ -o $@

$(BUILDDIR)\flow_routing.obj: $(ASMDIR)\flow_routing.asm
    @echo Assembling: $[@
    $(ASM) $(AFLAGS) $[@ -o $@

$(BUILDDIR)\direct_pio.obj: $(ASMDIR)\direct_pio.asm
    @echo Assembling: $[@
    $(ASM) $(AFLAGS) $[@ -o $@

$(BUILDDIR)\packet_ops.obj: $(ASMDIR)\packet_ops.asm
    @echo Assembling: $[@
    $(ASM) $(AFLAGS) $[@ -o $@

$(BUILDDIR)\packet_copy_c_wrapper.obj: $(ASMDIR)\packet_copy_c_wrapper.asm
    @echo Assembling: $[@
    $(ASM) $(AFLAGS) $[@ -o $@

$(BUILDDIR)\tsr_common.obj: $(ASMDIR)\tsr_common.asm
    @echo Assembling: $[@
    $(ASM) $(AFLAGS) $[@ -o $@

$(BUILDDIR)\tsr_c_wrappers.obj: $(ASMDIR)\tsr_c_wrappers.asm
    @echo Assembling: $[@
    $(ASM) $(AFLAGS) $[@ -o $@

$(BUILDDIR)\pci_io.obj: $(ASMDIR)\pci_io.asm
    @echo Assembling: $[@
    $(ASM) $(AFLAGS) $[@ -o $@

$(BUILDDIR)\pci_shim_isr.obj: $(ASMDIR)\pci_shim_isr.asm
    @echo Assembling: $[@
    $(ASM) $(AFLAGS) $[@ -o $@

$(BUILDDIR)\cpu_detect.obj: $(ASMDIR)\cpu_detect.asm
    @echo Assembling: $[@
    $(ASM) $(AFLAGS) $[@ -o $@

$(BUILDDIR)\pnp.obj: $(ASMDIR)\pnp.asm
    @echo Assembling: $[@
    $(ASM) $(AFLAGS) $[@ -o $@

$(BUILDDIR)\promisc.obj: $(ASMDIR)\promisc.asm
    @echo Assembling: $[@
    $(ASM) $(AFLAGS) $[@ -o $@

$(BUILDDIR)\smc_patches.obj: $(ASMDIR)\smc_patches.asm
    @echo Assembling: $[@
    $(ASM) $(AFLAGS) $[@ -o $@

$(BUILDDIR)\safety_stubs.obj: $(ASMDIR)\safety_stubs.asm
    @echo Assembling: $[@
    $(ASM) $(AFLAGS) $[@ -o $@

$(BUILDDIR)\quiesce.obj: $(ASMDIR)\quiesce.asm
    @echo Assembling: $[@
    $(ASM) $(AFLAGS) $[@ -o $@

# --- Clean Target ---

clean: .SYMBOLIC
    @echo Cleaning all build artifacts...
    @if exist $(BUILDDIR)\*.obj del $(BUILDDIR)\*.obj
    @if exist $(BUILDDIR)\*.exe del $(BUILDDIR)\*.exe
    @if exist $(BUILDDIR)\*.map del $(BUILDDIR)\*.map
    @if exist $(BUILDDIR)\*.err del $(BUILDDIR)\*.err
    @if exist $(BUILDDIR)\*.lst del $(BUILDDIR)\*.lst
    @if exist $(BUILDDIR)\loader\*.obj del $(BUILDDIR)\loader\*.obj
    @echo Clean complete.

# --- Information Target ---

info: .SYMBOLIC
    @echo === 3Com Packet Driver Build System (wmake) ===
    @echo Target: $(TARGET)
    @echo Build directory: $(BUILDDIR)
    @echo.
    @echo Build Targets:
    @echo   wmake                - Release build (default)
    @echo   wmake debug          - Debug build with symbols
    @echo   wmake production     - Size-optimized production build
    @echo   wmake config-8086    - Build for 8086/8088 (3C509B PIO only)
    @echo   wmake config-286     - Build for 80286
    @echo   wmake config-386     - Build for 80386
    @echo   wmake config-486     - Build for 80486
    @echo   wmake config-3c509b  - Build for 3C509B NIC only
    @echo   wmake config-3c515   - Build for 3C515 NIC only
    @echo   wmake config-both    - Build for both NICs
    @echo   wmake pci-utils      - Build PCI utilities
    @echo   wmake clean          - Clean build artifacts
    @echo   wmake info           - Show this information
