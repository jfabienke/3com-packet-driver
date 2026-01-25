# Makefile.wat - wmake-compatible Makefile for 3Com Packet Driver
# Use: wmake -f Makefile.wat [target]
# Requires Open Watcom C/C++ and NASM
#
# Last Updated: 2026-01-25 17:55 UTC

# --- Watcom Configuration ---
# Set WATCOM if not already defined (macOS ARM64 default path)
!ifndef WATCOM
WATCOM = /Users/johnfabienke/Development/macos-open-watcom/open-watcom-v2/rel
!endif

# --- Directories ---
SRCDIR     = src
CDIR       = $(SRCDIR)/c
ASMDIR     = $(SRCDIR)/asm
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

# Watcom header path (must set WATCOM env var or use absolute path)
WATCOM_H = $(WATCOM)/h

# Memory model: -ml (large) allows code and data to exceed 64KB limits
# Required because: code=254KB (190KB over), data=276KB (212KB over)
# Note: This creates far pointers for all code/data - larger executable
CFLAGS_DEBUG   = -zq -ml -s -0 -zp1 -i=$(INCDIR)/ -i=$(WATCOM_H)/ -fr=$(BUILDDIR)/ -wcd=201 -d2
CFLAGS_RELEASE = -zq -ml -s -os -ot -zp1 -i=$(INCDIR)/ -i=$(WATCOM_H)/ -fr=$(BUILDDIR)/ -wcd=201 -d0
CFLAGS_PRODUCTION = -zq -ml -s -os -zp1 -i=$(INCDIR)/ -i=$(WATCOM_H)/ -d0 &
                    -oe=100 -ol+ -ox -wcd=201 -we &
                    -dPRODUCTION -dNO_LOGGING -dNO_STATS -dNDEBUG

# --- Assembler Flags (NASM) ---
AFLAGS_DEBUG   = -f obj -i$(INCDIR)/ -g
AFLAGS_RELEASE = -f obj -i$(INCDIR)/

# --- Linker Flags ---
# Library path for medium model C runtime
WATCOM_LIB = $(WATCOM)/lib286/dos

# Link against clibl.lib (medium model C library) for stdlib functions
LFLAGS_DEBUG   = system dos library $(WATCOM_LIB)/clibl.lib &
                 option map=$(BUILDDIR)/3cpd.map, caseexact, quiet, stack=1024
LFLAGS_RELEASE = system dos library $(WATCOM_LIB)/clibl.lib &
                 option map=$(BUILDDIR)/3cpd.map, caseexact, quiet, stack=1024
LFLAGS_PRODUCTION = system dos library $(WATCOM_LIB)/clibl.lib &
                    option map=$(BUILDDIR)/3cpd.map, caseexact, quiet, stack=1024

# --- Default flags (release) ---
CFLAGS = $(CFLAGS_RELEASE)
AFLAGS = $(AFLAGS_RELEASE)
LFLAGS = $(LFLAGS_RELEASE)

# --- Target ---
TARGET     = $(BUILDDIR)/3cpd.exe

# --- PCI Utilities ---
PCI_UTILS = $(BUILDDIR)/pcitest.exe &
            $(BUILDDIR)/pciscan.exe &
            $(BUILDDIR)/pcictl.exe &
            $(BUILDDIR)/pcidump.exe

# --- HOT SECTION - Resident Assembly Objects ---
HOT_ASM_OBJS = $(BUILDDIR)/pktapi.obj &
               $(BUILDDIR)/main_asm.obj &
               $(BUILDDIR)/nicirq.obj &
               $(BUILDDIR)/hwsmc.obj &
               $(BUILDDIR)/pcmisr.obj &
               $(BUILDDIR)/flowrt.obj &
               $(BUILDDIR)/dirpio.obj &
               $(BUILDDIR)/pktops.obj &
               $(BUILDDIR)/pktcopy.obj &
               $(BUILDDIR)/tsrcom.obj &
               $(BUILDDIR)/tsrwrap.obj &
               $(BUILDDIR)/pci_io.obj &
               $(BUILDDIR)/pciisr.obj

# --- Main loader ---
LOADER_OBJ = $(BUILDDIR)/tsrldr.obj

# --- HOT SECTION - Resident C Objects ---
# Note: pktops is in HOT_ASM_OBJS (assembly implementation preferred)
HOT_C_OBJS = $(BUILDDIR)/api.obj &
             $(BUILDDIR)/routing.obj &
             $(BUILDDIR)/pci_shim.obj &
             $(BUILDDIR)/pcimux.obj &
             $(BUILDDIR)/dmamap.obj &
             $(BUILDDIR)/dmabnd.obj &
             $(BUILDDIR)/hwchksm.obj &
             $(BUILDDIR)/dos_idle.obj &
             $(BUILDDIR)/irq_bind.obj &
             $(BUILDDIR)/rtcfg.obj &
             $(BUILDDIR)/irqmit.obj &
             $(BUILDDIR)/rxbatch.obj &
             $(BUILDDIR)/txlazy.obj

# --- COLD SECTION - Initialization Assembly Objects ---
COLD_ASM_OBJS = $(BUILDDIR)/cpudet.obj &
                $(BUILDDIR)/pnp.obj &
                $(BUILDDIR)/promisc.obj &
                $(BUILDDIR)/smcpat.obj &
                $(BUILDDIR)/safestub.obj &
                $(BUILDDIR)/quiesce.obj

# --- COLD SECTION - Initialization C Objects ---
COLD_C_OBJS_BASE = $(BUILDDIR)/main.obj &
                   $(BUILDDIR)/init.obj &
                   $(BUILDDIR)/config.obj &
                   $(BUILDDIR)/pcmmgr.obj &
                   $(BUILDDIR)/pcmsnap.obj &
                   $(BUILDDIR)/flowctl.obj &
                   $(BUILDDIR)/pcmpebe.obj &
                   $(BUILDDIR)/pcmssbe.obj &
                   $(BUILDDIR)/memory.obj &
                   $(BUILDDIR)/xmsdet.obj &
                   $(BUILDDIR)/umbldr.obj &
                   $(BUILDDIR)/eeprom.obj &
                   $(BUILDDIR)/bufaloc.obj &
                   $(BUILDDIR)/bufauto.obj &
                   $(BUILDDIR)/statrt.obj &
                   $(BUILDDIR)/arp.obj &
                   $(BUILDDIR)/nic_init.obj &
                   $(BUILDDIR)/hardware.obj &
                   $(BUILDDIR)/hwstubs.obj &
                   $(BUILDDIR)/3c515.obj &
                   $(BUILDDIR)/3c509b.obj &
                   $(BUILDDIR)/entval.obj &
                   $(BUILDDIR)/pltprob.obj &
                   $(BUILDDIR)/dmacap.obj &
                   $(BUILDDIR)/tsrmgr.obj &
                   $(BUILDDIR)/dmatest.obj &
                   $(BUILDDIR)/dmasafe.obj &
                   $(BUILDDIR)/vds_core.obj &
                   $(BUILDDIR)/vdssafe.obj &
                   $(BUILDDIR)/vdsmgr.obj &
                   $(BUILDDIR)/extapi.obj &
                   $(BUILDDIR)/unwind.obj &
                   $(BUILDDIR)/chipdet.obj &
                   $(BUILDDIR)/bmtest.obj &
                   $(BUILDDIR)/loader_cpudet.obj &
                   $(BUILDDIR)/loader_patch_apply.obj &
                   $(BUILDDIR)/loader_init_stubs.obj &
                   $(BUILDDIR)/pci_bios.obj &
                   $(BUILDDIR)/3cpcidet.obj &
                   $(BUILDDIR)/3cvortex.obj &
                   $(BUILDDIR)/3cboom.obj &
                   $(BUILDDIR)/pciintg.obj &
                   $(BUILDDIR)/pcishme.obj &
                   $(BUILDDIR)/smcserl.obj &
                   $(BUILDDIR)/cachemgt.obj &
                   $(BUILDDIR)/dmapol.obj &
                   $(BUILDDIR)/vds.obj

# --- Debug-only objects ---
DEBUG_C_OBJS = $(BUILDDIR)/diag.obj &
               $(BUILDDIR)/logging.obj &
               $(BUILDDIR)/stats.obj

# --- Conditional inclusion ---
!ifdef PRODUCTION
COLD_C_OBJS = $(COLD_C_OBJS_BASE)
ALL_OBJS = $(LOADER_OBJ) $(HOT_ASM_OBJS) $(HOT_C_OBJS) $(COLD_ASM_OBJS) $(COLD_C_OBJS)
!else
COLD_C_OBJS = $(COLD_C_OBJS_BASE) $(DEBUG_C_OBJS)
ALL_OBJS = $(LOADER_OBJ) $(HOT_ASM_OBJS) $(HOT_C_OBJS) $(COLD_ASM_OBJS) $(COLD_C_OBJS)
!endif

# --- Suffix Rules ---
# Clear default extensions first (wmake already defines .exe, .obj, .c, .asm)
.EXTENSIONS:
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

$(BUILDDIR)/pcitest.exe: $(BUILDDIR)/pcitest.obj $(BUILDDIR)/pci_bios.obj &
                         $(BUILDDIR)/pci_shim.obj $(BUILDDIR)/pcishme.obj &
                         $(BUILDDIR)/pci_io.obj $(BUILDDIR)/logging.obj
    @echo Linking PCI Test Suite...
    $(LINK) $(LFLAGS) file {$<} name $@

$(BUILDDIR)/pciscan.exe: $(BUILDDIR)/pciscan.obj $(BUILDDIR)/pci_bios.obj &
                         $(BUILDDIR)/pcishme.obj $(BUILDDIR)/pci_io.obj &
                         $(BUILDDIR)/cpudet.obj $(BUILDDIR)/logging.obj
    @echo Linking PCI Scanner...
    $(LINK) $(LFLAGS) file {$<} name $@

$(BUILDDIR)/pcictl.exe: $(BUILDDIR)/pcictl.obj
    @echo Linking PCI Control utility...
    $(LINK) $(LFLAGS) file {$<} name $@

$(BUILDDIR)/pcidump.exe: $(BUILDDIR)/pcidump.obj $(BUILDDIR)/pci_bios.obj $(BUILDDIR)/pci_io.obj
    @echo Linking PCI Config Dump utility...
    $(LINK) $(LFLAGS) file {$<} name $@

# --- Build Directory ---

$(BUILDDIR): .SYMBOLIC
    @if not exist $(BUILDDIR) mkdir $(BUILDDIR)
    @if not exist $(BUILDDIR)/loader mkdir $(BUILDDIR)/loader

# --- Main Target ---

$(TARGET): $(ALL_OBJS)
    @echo Linking $@ with hot/cold section separation...
    $(LINK) $(LFLAGS) file {$(ALL_OBJS)} name $@
    @echo Build complete: $@

# --- C Source Compilation Rules ---
# Note: wmake suffix rules use $[* for stem

# Main C files
$(BUILDDIR)/main.obj: $(CDIR)/main.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)/init.obj: $(CDIR)/init.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)/api.obj: $(CDIR)/api.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)/config.obj: $(CDIR)/config.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)/hardware.obj: $(CDIR)/hardware.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)/hwstubs.obj: $(CDIR)/hwstubs.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)/memory.obj: $(CDIR)/memory.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)/xmsdet.obj: $(CDIR)/xmsdet.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)/bufaloc.obj: $(CDIR)/bufaloc.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)/bufauto.obj: $(CDIR)/bufauto.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)/routing.obj: $(CDIR)/routing.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)/statrt.obj: $(CDIR)/statrt.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)/arp.obj: $(CDIR)/arp.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)/nic_init.obj: $(CDIR)/nic_init.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)/3c515.obj: $(CDIR)/3c515.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)/3c509b.obj: $(CDIR)/3c509b.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)/diag.obj: $(CDIR)/diag.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)/logging.obj: $(CDIR)/logging.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)/stats.obj: $(CDIR)/stats.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)/pci_shim.obj: $(CDIR)/pci_shim.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)/pcimux.obj: $(CDIR)/pcimux.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)/pci_bios.obj: $(CDIR)/pci_bios.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)/pcishme.obj: $(CDIR)/pcishme.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)/pciintg.obj: $(CDIR)/pciintg.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)/3cpcidet.obj: $(CDIR)/3cpcidet.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)/3cvortex.obj: $(CDIR)/3cvortex.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)/3cboom.obj: $(CDIR)/3cboom.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)/dmamap.obj: $(CDIR)/dmamap.c
    @echo 'Compiling DMA:' $[@
    $(CC) $(CFLAGS) -dPRODUCTION -dNO_LOGGING -dNDEBUG $[@ -fo=$@

$(BUILDDIR)/dmabnd.obj: $(CDIR)/dmabnd.c
    @echo 'Compiling DMA:' $[@
    $(CC) $(CFLAGS) -dPRODUCTION -dNO_LOGGING -dNDEBUG $[@ -fo=$@

$(BUILDDIR)/hwchksm.obj: $(CDIR)/hwchksm.c
    @echo 'Compiling DMA:' $[@
    $(CC) $(CFLAGS) -dPRODUCTION -dNO_LOGGING -dNDEBUG $[@ -fo=$@

$(BUILDDIR)/dmasafe.obj: $(CDIR)/dmasafe.c
    @echo 'Compiling DMA:' $[@
    $(CC) $(CFLAGS) -dPRODUCTION -dNO_LOGGING -dNDEBUG $[@ -fo=$@

$(BUILDDIR)/dmatest.obj: $(CDIR)/dmatest.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)/dmapol.obj: $(CDIR)/dmapol.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)/dmacap.obj: $(CDIR)/dmacap.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)/vds.obj: $(CDIR)/vds.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)/vds_core.obj: $(CDIR)/vds_core.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)/vdssafe.obj: $(CDIR)/vdssafe.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)/vdsmgr.obj: $(CDIR)/vdsmgr.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)/dos_idle.obj: $(CDIR)/dos_idle.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)/irq_bind.obj: $(CDIR)/irq_bind.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)/rtcfg.obj: $(CDIR)/rtcfg.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)/irqmit.obj: $(CDIR)/irqmit.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)/rxbatch.obj: $(CDIR)/rxbatch.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)/txlazy.obj: $(CDIR)/txlazy.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)/pcmmgr.obj: $(CDIR)/pcmmgr.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)/pcmsnap.obj: $(CDIR)/pcmsnap.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)/pcmpebe.obj: $(CDIR)/pcmpebe.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)/pcmssbe.obj: $(CDIR)/pcmssbe.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)/flowctl.obj: $(CDIR)/flowctl.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)/umbldr.obj: $(CDIR)/umbldr.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)/eeprom.obj: $(CDIR)/eeprom.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)/entval.obj: $(CDIR)/entval.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)/pltprob.obj: $(CDIR)/pltprob.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)/tsrmgr.obj: $(CDIR)/tsrmgr.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)/extapi.obj: $(CDIR)/extapi.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)/unwind.obj: $(CDIR)/unwind.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)/chipdet.obj: $(CDIR)/chipdet.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)/bmtest.obj: $(CDIR)/bmtest.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)/smcserl.obj: $(CDIR)/smcserl.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)/cachemgt.obj: $(CDIR)/cachemgt.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

# Loader C files
$(BUILDDIR)/loader_cpudet.obj: $(SRCDIR)/loader/cpu_detect.c
    @echo 'Compiling loader:' $[@
    $(CC) $(CFLAGS) -dCOLD_SECTION $[@ -fo=$@

$(BUILDDIR)/loader_patch_apply.obj: $(SRCDIR)/loader/patch_apply.c
    @echo 'Compiling loader:' $[@
    $(CC) $(CFLAGS) -dCOLD_SECTION $[@ -fo=$@

$(BUILDDIR)/loader_init_stubs.obj: $(SRCDIR)/loader/init_stubs.c
    @echo 'Compiling loader:' $[@
    $(CC) $(CFLAGS) -dCOLD_SECTION $[@ -fo=$@

# PCI utility standalone compiles
$(BUILDDIR)/pcitest.obj: $(CDIR)/pcitest.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)/pciscan.obj: $(CDIR)/pciscan.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

$(BUILDDIR)/pcictl.obj: $(CDIR)/pcimux.c
    @echo 'Compiling standalone:' $[@
    $(CC) $(CFLAGS) -dSTANDALONE_UTILITY $[@ -fo=$@

$(BUILDDIR)/pcidump.obj: $(CDIR)/pcidump.c
    @echo Compiling: $[@
    $(CC) $(CFLAGS) $[@ -fo=$@

# --- Assembly Source Compilation Rules ---

$(BUILDDIR)/tsrldr.obj: $(ASMDIR)/tsrldr.asm
    @echo Assembling: $[@
    $(ASM) $(AFLAGS) $[@ -o $@

$(BUILDDIR)/main_asm.obj: $(ASMDIR)/main.asm
    @echo Assembling: $[@
    $(ASM) $(AFLAGS) $[@ -o $@

$(BUILDDIR)/pktapi.obj: $(ASMDIR)/pktapi.asm
    @echo Assembling: $[@
    $(ASM) $(AFLAGS) $[@ -o $@

$(BUILDDIR)/nicirq.obj: $(ASMDIR)/nicirq.asm
    @echo Assembling: $[@
    $(ASM) $(AFLAGS) $[@ -o $@

$(BUILDDIR)/hwsmc.obj: $(ASMDIR)/hwsmc.asm
    @echo Assembling: $[@
    $(ASM) $(AFLAGS) $[@ -o $@

$(BUILDDIR)/pcmisr.obj: $(ASMDIR)/pcmisr.asm
    @echo Assembling: $[@
    $(ASM) $(AFLAGS) $[@ -o $@

$(BUILDDIR)/flowrt.obj: $(ASMDIR)/flowrt.asm
    @echo Assembling: $[@
    $(ASM) $(AFLAGS) $[@ -o $@

$(BUILDDIR)/dirpio.obj: $(ASMDIR)/dirpio.asm
    @echo Assembling: $[@
    $(ASM) $(AFLAGS) $[@ -o $@

$(BUILDDIR)/pktops.obj: $(ASMDIR)/pktops.asm
    @echo Assembling: $[@
    $(ASM) $(AFLAGS) $[@ -o $@

$(BUILDDIR)/pktcopy.obj: $(ASMDIR)/pktcopy.asm
    @echo Assembling: $[@
    $(ASM) $(AFLAGS) $[@ -o $@

$(BUILDDIR)/tsrcom.obj: $(ASMDIR)/tsrcom.asm
    @echo Assembling: $[@
    $(ASM) $(AFLAGS) $[@ -o $@

$(BUILDDIR)/tsrwrap.obj: $(ASMDIR)/tsrwrap.asm
    @echo Assembling: $[@
    $(ASM) $(AFLAGS) $[@ -o $@

$(BUILDDIR)/pci_io.obj: $(ASMDIR)/pci_io.asm
    @echo Assembling: $[@
    $(ASM) $(AFLAGS) $[@ -o $@

$(BUILDDIR)/pciisr.obj: $(ASMDIR)/pciisr.asm
    @echo Assembling: $[@
    $(ASM) $(AFLAGS) $[@ -o $@

$(BUILDDIR)/cpudet.obj: $(ASMDIR)/cpudet.asm
    @echo Assembling: $[@
    $(ASM) $(AFLAGS) $[@ -o $@

$(BUILDDIR)/pnp.obj: $(ASMDIR)/pnp.asm
    @echo Assembling: $[@
    $(ASM) $(AFLAGS) $[@ -o $@

$(BUILDDIR)/promisc.obj: $(ASMDIR)/promisc.asm
    @echo Assembling: $[@
    $(ASM) $(AFLAGS) $[@ -o $@

$(BUILDDIR)/smcpat.obj: $(ASMDIR)/smcpat.asm
    @echo Assembling: $[@
    $(ASM) $(AFLAGS) $[@ -o $@

$(BUILDDIR)/safestub.obj: $(ASMDIR)/safestub.asm
    @echo Assembling: $[@
    $(ASM) $(AFLAGS) $[@ -o $@

$(BUILDDIR)/quiesce.obj: $(ASMDIR)/quiesce.asm
    @echo Assembling: $[@
    $(ASM) $(AFLAGS) $[@ -o $@

# --- Clean Target ---

clean: .SYMBOLIC
    @echo Cleaning all build artifacts...
    -rm -f $(BUILDDIR)/*.obj
    -rm -f $(BUILDDIR)/*.exe
    -rm -f $(BUILDDIR)/*.map
    -rm -f $(BUILDDIR)/*.err
    -rm -f $(BUILDDIR)/*.lst
    -rm -f $(BUILDDIR)/loader/*.obj
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
