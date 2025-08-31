# DOS Packet Driver Boot Sequence Architecture

## Overview

This document defines the comprehensive 10-phase initialization sequence for the 3Com DOS packet driver, designed to safely handle TSR installation with proper hardware detection, DMA safety, cache coherency, and memory management on DOS systems from 286 to Pentium 4.

**Critical Design Principle**: The boot sequence must handle three major safety scenarios:
1. **ISA DMA Constraints**: 64KB boundaries, 16MB limits, bus mastering capability
2. **DOS Memory Management**: V86 mode, EMM386, VDS services, UMB allocation
3. **CPU Cache Coherency**: Cache management on 486+ systems with DMA operations

## Complete Boot Sequence (Cold → Hot Transition)

### Phase 0: Entry & Safety Validation

**Purpose**: Establish safe environment and prevent conflicts  
**Memory**: Cold section (discarded after init)  
**Duration**: <1ms

```c
// Entry point validation
int driver_entry(int argc, char *argv[]) {
    // Parse command line for software interrupt vector
    uint8_t target_vector = parse_command_line(argc, argv);
    
    // CRITICAL: Check for existing packet driver instance
    if (packet_driver_installed(target_vector)) {
        printf("Packet driver already installed on INT %02Xh\n", target_vector);
        return EXIT_ALREADY_INSTALLED;
    }
    
    // Set up error handling and cleanup path
    install_cleanup_handler();
    
    // CRITICAL: Disable hardware IRQs at PIC during probing
    // This prevents spurious interrupts during hardware detection
    disable_candidate_irqs(candidate_irq_list);
    
    return continue_to_platform_probe();
}
```

**Key Safety Checks**:
- Prevent multiple driver instances
- Establish failure cleanup path
- Disable hardware interrupts during probe

---

### Phase 1: Platform Probe & Policy Decisions

**Purpose**: Detect execution environment and establish DMA/cache policies  
**Memory**: Cold section  
**Duration**: 5-10ms

```c
int platform_probe(void) {
    // 1.1: CPU Detection and Feature Analysis
    cpu_info_t cpu = detect_cpu_capabilities();
    log_info("CPU: %s, Features: 0x%08X", cpu_type_name(cpu.type), cpu.features);
    
    // 1.2: CRITICAL - V86 Mode Detection
    bool in_v86_mode = detect_v86_mode();
    if (in_v86_mode) {
        log_warning("Running under DOS extender/EMM386 (V86 mode)");
    }
    
    // 1.3: DOS Environment Analysis
    dos_info_t dos = detect_dos_environment();
    log_info("DOS version: %d.%d, InDOS flag: %04X:%04X", 
             dos.major, dos.minor, dos.indos_seg, dos.indos_offset);
    
    // 1.4: CRITICAL - VDS Detection (MUST BE HERE!)
    vds_info_t vds = detect_vds_services();
    if (vds.present) {
        log_info("VDS services available: version %d.%d, flags: 0x%04X", 
                 vds.major, vds.minor, vds.flags);
    } else if (in_v86_mode) {
        log_warning("V86 mode without VDS - DMA operations restricted!");
    }
    
    // 1.5: Memory Manager Detection
    memory_managers_t mem_mgr = detect_memory_managers();
    log_info("XMS: %s, UMB: %s, EMS: %s", 
             mem_mgr.xms ? "Available" : "None",
             mem_mgr.umb ? "Available" : "None", 
             mem_mgr.ems ? "Available" : "None");
    
    // 1.6: CRITICAL POLICY DECISION
    // If V86 mode without VDS, bus-mastering DMA is FORBIDDEN
    if (in_v86_mode && !vds.present) {
        g_dma_policy = DMA_POLICY_PIO_ONLY;
        log_warning("DMA disabled due to V86 mode without VDS services");
    } else {
        g_dma_policy = DMA_POLICY_ALLOWED;
        log_info("DMA operations allowed");
    }
    
    return PLATFORM_PROBE_SUCCESS;
}
```

**Critical Safety Policies**:
- **V86 + No VDS = NO bus-master DMA** (prevents memory corruption)
- **486+ requires cache coherency strategy** 
- **16MB ISA limit enforcement**

---

### Phase 2: ISA/PnP Environment Preparation

**Purpose**: Prepare for ISA bus and Plug-and-Play device detection  
**Memory**: Cold section  
**Duration**: 2-5ms

```c
int prepare_isa_pnp_environment(void) {
    // 2.1: ISA PnP BIOS Detection
    pnp_bios_info_t pnp_bios = detect_pnp_bios();
    if (pnp_bios.present) {
        log_info("ISA PnP BIOS v%d.%d found at %04X:%04X", 
                 pnp_bios.version >> 8, pnp_bios.version & 0xFF,
                 pnp_bios.seg, pnp_bios.offset);
    }
    
    // 2.2: Prepare 3Com Activation Sequence
    // 3C509B requires specific ID port activation sequence
    if (prepare_3com_id_port() != 0) {
        log_error("Failed to prepare 3Com ID port activation");
        return ISA_PREP_ERROR;
    }
    
    // 2.3: Resource Conflict Detection
    // Build map of known I/O and IRQ usage
    build_resource_usage_map();
    
    return ISA_PREP_SUCCESS;
}
```

---

### Phase 3: NIC Discovery & Resource Selection

**Purpose**: Enumerate NICs and select resources (no DMA buffer allocation yet)  
**Memory**: Cold section  
**Duration**: 10-50ms depending on detection

```c
int discover_nics_and_resources(void) {
    nic_discovery_t discovery = {0};
    
    // 3.1: 3C509B Discovery (ISA with ID port activation)
    discovery.count_3c509b = discover_3c509b_nics(discovery.nics_3c509b, MAX_3C509B);
    log_info("3C509B NICs found: %d", discovery.count_3c509b);
    
    // 3.2: 3C515-TX Discovery (ISA with PnP)  
    discovery.count_3c515 = discover_3c515_nics(discovery.nics_3c515, MAX_3C515);
    log_info("3C515-TX NICs found: %d", discovery.count_3c515);
    
    // 3.3: Resource Selection and Validation
    for (int i = 0; i < discovery.count_3c509b; i++) {
        nic_info_t *nic = &discovery.nics_3c509b[i];
        
        // Validate I/O base address
        if (!is_io_address_safe(nic->io_base)) {
            log_warning("3C509B at 0x%X: I/O address conflict", nic->io_base);
            continue;
        }
        
        // CRITICAL: Validate IRQ not in use
        if (is_irq_in_use(nic->irq)) {
            log_warning("3C509B at 0x%X: IRQ %d already in use", nic->io_base, nic->irq);
            continue;
        }
        
        // Program NIC but DON'T enable interrupts yet
        program_nic_resources(nic);
        clear_pending_interrupts(nic);
        
        discovery.selected_nics[discovery.selected_count++] = *nic;
    }
    
    // Similar process for 3C515-TX...
    
    if (discovery.selected_count == 0) {
        log_error("No compatible NICs found or resources available");
        return NIC_DISCOVERY_NONE;
    }
    
    log_info("Selected %d NICs for initialization", discovery.selected_count);
    return NIC_DISCOVERY_SUCCESS;
}
```

---

### Phase 4: Feature Planning & Sizing

**Purpose**: Decide operational modes and compute memory requirements  
**Memory**: Cold section  
**Duration**: <1ms

```c
int plan_features_and_sizing(const nic_discovery_t *discovery) {
    driver_plan_t plan = {0};
    
    // 4.1: Mode Selection per NIC
    for (int i = 0; i < discovery->selected_count; i++) {
        nic_info_t *nic = &discovery->selected_nics[i];
        nic_plan_t *nic_plan = &plan.nic_plans[i];
        
        if (nic->type == NIC_TYPE_3C509B) {
            // 3C509B: PIO only
            nic_plan->mode = NIC_MODE_PIO;
            nic_plan->dma_required = false;
        } else if (nic->type == NIC_TYPE_3C515) {
            // 3C515: Prefer bus-master, fallback to PIO
            if (g_dma_policy == DMA_POLICY_ALLOWED && 
                cpu_supports_bus_mastering()) {
                nic_plan->mode = NIC_MODE_DMA;
                nic_plan->dma_required = true;
            } else {
                nic_plan->mode = NIC_MODE_PIO;
                nic_plan->dma_required = false;
                log_info("3C515 will use PIO mode (DMA unavailable)");
            }
        }
        
        // 4.2: Ring/Buffer Planning
        plan_ring_buffers(nic_plan);
        
        // 4.3: CRITICAL - ISA DMA Constraints Planning
        if (nic_plan->dma_required) {
            plan_dma_constraints(nic_plan);
        }
    }
    
    // 4.4: Compute Resident Memory Requirements
    plan.resident_size = calculate_resident_footprint(&plan);
    log_info("Planned resident size: %d bytes (%dKB)", 
             plan.resident_size, plan.resident_size / 1024);
    
    // Store plan globally
    g_driver_plan = plan;
    return FEATURE_PLANNING_SUCCESS;
}

void plan_dma_constraints(nic_plan_t *plan) {
    // ISA DMA cannot cross 64KB boundaries
    // ISA DMA limited to first 16MB of memory
    
    plan->buffer_alignment = 64;  // Align for cache line efficiency
    plan->max_segment_size = 65536 - plan->buffer_alignment;
    plan->requires_bounce_buffers = true;  // Always plan for bounce buffers
    plan->bounce_buffer_count = 8;  // Pre-allocate bounce buffers
    
    log_debug("DMA constraints: max_seg=%d, bounce_buffers=%d", 
              plan->max_segment_size, plan->bounce_buffer_count);
}
```

---

### Phase 5: Memory Allocation & DMA Preparation

**Purpose**: Allocate all memory and prepare DMA-safe buffers  
**Memory**: Allocates hot section target location  
**Duration**: 5-20ms depending on UMB/VDS operations

```c
int allocate_memory_and_dma_buffers(void) {
    // 5.1: Allocate Hot Section Memory (prefer UMB)
    void *hot_section = allocate_resident_memory(g_driver_plan.resident_size);
    if (!hot_section) {
        log_error("Failed to allocate %d bytes for resident code", 
                  g_driver_plan.resident_size);
        return MEMORY_ALLOCATION_FAILED;
    }
    log_info("Hot section allocated at %04X:%04X (%s)", 
             FP_SEG(hot_section), FP_OFF(hot_section),
             is_umb_memory(hot_section) ? "UMB" : "Conventional");
    
    // 5.2: Allocate DMA Buffers with Safety Constraints
    for (int i = 0; i < g_driver_plan.nic_count; i++) {
        nic_plan_t *plan = &g_driver_plan.nic_plans[i];
        
        if (plan->dma_required) {
            // CRITICAL: Allocate DMA-safe memory
            plan->dma_buffers = allocate_dma_safe_buffers(plan);
            if (!plan->dma_buffers) {
                log_error("Failed to allocate DMA buffers for NIC %d", i);
                return DMA_ALLOCATION_FAILED;
            }
            
            // 5.3: VDS Lock and Map (if VDS present)
            if (g_vds_info.present) {
                if (vds_lock_and_map_buffers(plan->dma_buffers) != 0) {
                    log_error("VDS lock/map failed for NIC %d", i);
                    return VDS_LOCK_FAILED;
                }
                log_debug("VDS locked %d DMA buffers for NIC %d", 
                         plan->dma_buffers->count, i);
            }
            
            // 5.4: Validate 64KB and 16MB Constraints
            if (!validate_dma_constraints(plan->dma_buffers)) {
                log_error("DMA buffer constraints violated for NIC %d", i);
                return DMA_CONSTRAINT_VIOLATION;
            }
        }
    }
    
    g_hot_section_base = hot_section;
    return MEMORY_ALLOCATION_SUCCESS;
}

dma_buffers_t *allocate_dma_safe_buffers(nic_plan_t *plan) {
    dma_buffers_t *buffers = malloc(sizeof(dma_buffers_t));
    
    // Allocate with over-allocation for alignment
    size_t alloc_size = plan->total_buffer_size + 65536;  // +64KB for alignment
    void *raw_memory = malloc(alloc_size);
    
    // Align to 64KB boundary to avoid ISA DMA crossing issues
    uintptr_t aligned = ((uintptr_t)raw_memory + 65535) & ~65535UL;
    buffers->base = (void *)aligned;
    buffers->size = plan->total_buffer_size;
    
    // Verify constraints
    uintptr_t physical_addr = get_physical_address(buffers->base);
    if (physical_addr >= 16 * 1024 * 1024) {
        log_error("DMA buffer above 16MB limit: 0x%08lX", physical_addr);
        free(buffers);
        return NULL;
    }
    
    return buffers;
}
```

---

### Phase 6: Hot Section Relocation

**Purpose**: Copy hot code/data to final resident location and fix addresses  
**Memory**: Hot section populated  
**Duration**: 1-3ms

```c
int relocate_hot_section(void) {
    // 6.1: Copy Hot Code and Data
    memcpy(g_hot_section_base, &__hot_section_start, 
           &__hot_section_end - &__hot_section_start);
    
    // 6.2: Fix Absolute Addresses in Relocated Code
    relocate_absolute_addresses(g_hot_section_base);
    
    // 6.3: Update Global Pointers to Hot Section
    g_packet_driver_entry = (packet_driver_func_t)
        ((char*)g_hot_section_base + OFFSET_PACKET_DRIVER_ENTRY);
    g_hardware_isr = (interrupt_handler_t)
        ((char*)g_hot_section_base + OFFSET_HARDWARE_ISR);
    
    // 6.4: Set MCB to Resident Size and Release Environment
    set_mcb_resident_size(g_driver_plan.resident_size);
    release_environment_block();
    
    log_info("Hot section relocated, %d paragraphs resident", 
             (g_driver_plan.resident_size + 15) / 16);
    
    return RELOCATION_SUCCESS;
}
```

---

### Phase 7: Self-Modifying Code (SMC) Patching

**Purpose**: Apply CPU-specific optimizations to hot section code  
**Memory**: Hot section (resident)  
**Duration**: <1ms

**⚠️ CRITICAL TIMING**: SMC patching MUST happen AFTER relocation, not before!

```c
int apply_smc_patches(void) {
    // 7.1: CPU-Specific Code Patching
    if (g_cpu_info.type >= CPU_TYPE_80386) {
        patch_386_optimizations(g_hot_section_base);
        log_debug("Applied 386+ optimizations");
    }
    
    if (cpu_has_feature(CPU_FEATURE_CMOV)) {
        patch_cmov_optimizations(g_hot_section_base);
        log_debug("Applied CMOV optimizations");
    }
    
    // 7.2: Cache Management Strategy Selection
    if (g_cpu_info.type >= CPU_TYPE_80486) {
        if (cpu_has_feature(CPU_FEATURE_WBINVD)) {
            patch_wbinvd_cache_management(g_hot_section_base);
            log_debug("Using WBINVD for cache management");
        } else {
            patch_software_cache_management(g_hot_section_base);
            log_debug("Using software cache management");
        }
    }
    
    // 7.3: DMA-Specific Patches
    if (g_dma_policy == DMA_POLICY_ALLOWED) {
        patch_dma_optimizations(g_hot_section_base);
        
        // VDS vs. direct physical address patches
        if (g_vds_info.present) {
            patch_vds_address_translation(g_hot_section_base);
            log_debug("Using VDS address translation");
        } else {
            patch_direct_physical_addresses(g_hot_section_base);
            log_debug("Using direct physical addresses");
        }
    }
    
    return SMC_PATCHING_SUCCESS;
}
```

---

### Phase 8: Interrupt Vector Setup

**Purpose**: Install software and hardware interrupt handlers  
**Memory**: Hot section  
**Duration**: <1ms

**⚠️ CRITICAL**: Hardware IRQs must remain masked until NIC is fully programmed!

```c
int setup_interrupt_vectors(void) {
    // 8.1: Install Software Interrupt (Packet Driver API)
    save_old_packet_driver_vector();
    install_packet_driver_vector(g_target_software_vector, g_packet_driver_entry);
    
    // Add reentrancy guard for DOS safety
    set_reentrancy_guard(g_packet_driver_entry);
    
    log_info("Packet driver installed on INT %02Xh", g_target_software_vector);
    
    // 8.2: Install Hardware ISR (but keep IRQ masked!)
    for (int i = 0; i < g_driver_plan.nic_count; i++) {
        nic_plan_t *plan = &g_driver_plan.nic_plans[i];
        
        save_old_hardware_vector(plan->irq);
        install_hardware_isr(plan->irq, g_hardware_isr);
        
        // CRITICAL: Keep IRQ masked at PIC until NIC ready
        mask_irq_at_pic(plan->irq);
        
        log_info("Hardware ISR installed for IRQ %d (masked)", plan->irq);
    }
    
    return INTERRUPT_SETUP_SUCCESS;
}
```

---

### Phase 9: NIC Initialization & DMA Coherency Tests

**Purpose**: Program NICs and validate DMA safety before going live  
**Memory**: Hot section  
**Duration**: 10-100ms depending on tests

**⚠️ CRITICAL**: This is where we validate that DMA and cache coherency actually work!

```c
int initialize_nics_and_test_dma(void) {
    // 9.1: Hard Reset and NIC Programming
    for (int i = 0; i < g_driver_plan.nic_count; i++) {
        nic_info_t *nic = &g_driver_plan.selected_nics[i];
        nic_plan_t *plan = &g_driver_plan.nic_plans[i];
        
        // Hard reset NIC and verify ID
        hard_reset_nic(nic);
        if (!verify_nic_identity(nic)) {
            log_error("NIC %d identity verification failed after reset", i);
            return NIC_INIT_IDENTITY_FAILED;
        }
        
        // Program MAC address, media, flow control
        program_nic_configuration(nic, plan);
        
        // Program ring descriptors with physical addresses
        program_ring_descriptors(nic, plan);
        
        // Clear ALL pending interrupts
        clear_all_pending_interrupts(nic);
        
        log_info("NIC %d programmed and ready for coherency tests", i);
    }
    
    // 9.2: CRITICAL DMA COHERENCY TESTS (only for DMA NICs)
    for (int i = 0; i < g_driver_plan.nic_count; i++) {
        nic_plan_t *plan = &g_driver_plan.nic_plans[i];
        
        if (plan->dma_required) {
            dma_test_results_t test_results = run_dma_coherency_tests(plan);
            
            if (!test_results.outbound_coherent) {
                log_error("NIC %d: Outbound DMA coherency test FAILED", i);
                plan->mode = NIC_MODE_PIO;  // Fallback to PIO
                plan->dma_required = false;
            }
            
            if (!test_results.inbound_coherent) {
                log_error("NIC %d: Inbound DMA coherency test FAILED", i);  
                plan->mode = NIC_MODE_PIO;  // Fallback to PIO
                plan->dma_required = false;
            }
            
            if (!test_results.boundary_safe) {
                log_error("NIC %d: 64KB boundary test FAILED", i);
                // Enable bounce buffers
                plan->requires_bounce_buffers = true;
            }
            
            if (test_results.coherency_failures > 0) {
                log_warning("NIC %d: %d coherency failures detected", 
                           i, test_results.coherency_failures);
                
                // If too many failures, fallback to PIO
                if (test_results.coherency_failures > MAX_ALLOWED_FAILURES) {
                    log_error("Too many coherency failures, disabling DMA");
                    plan->mode = NIC_MODE_PIO;
                    plan->dma_required = false;
                }
            }
            
            log_info("NIC %d: DMA coherency tests completed, mode: %s", 
                     i, plan->mode == NIC_MODE_DMA ? "DMA" : "PIO");
        }
    }
    
    return NIC_INIT_SUCCESS;
}

dma_test_results_t run_dma_coherency_tests(nic_plan_t *plan) {
    dma_test_results_t results = {0};
    
    // Test 1: Outbound DMA Test
    // CPU writes pattern, applies cache management, NIC DMAs it
    uint8_t test_pattern[] = {0xAA, 0x55, 0xFF, 0x00, 0xCC, 0x33};
    void *test_buffer = plan->dma_buffers->test_buffer;
    
    memcpy(test_buffer, test_pattern, sizeof(test_pattern));
    
    // Apply required cache maintenance for outbound DMA
    if (g_vds_info.present && (g_vds_info.flags & VDS_FLAG_CACHE_COHERENCY)) {
        vds_flush_cache_range(test_buffer, sizeof(test_pattern));
    } else if (cpu_has_feature(CPU_FEATURE_WBINVD)) {
        flush_cache_range(test_buffer, sizeof(test_pattern));
    }
    
    // Have NIC DMA the data and verify
    results.outbound_coherent = nic_dma_outbound_test(plan->nic, test_buffer, sizeof(test_pattern));
    
    // Test 2: Inbound DMA Test  
    // NIC DMAs pattern into buffer, CPU applies cache invalidate, verifies
    memset(test_buffer, 0, sizeof(test_pattern));
    
    nic_dma_inbound_test(plan->nic, test_buffer, test_pattern, sizeof(test_pattern));
    
    // Apply required cache invalidation for inbound DMA
    if (g_vds_info.present && (g_vds_info.flags & VDS_FLAG_CACHE_COHERENCY)) {
        vds_invalidate_cache_range(test_buffer, sizeof(test_pattern));
    } else if (cpu_has_feature(CPU_FEATURE_WBINVD)) {
        invalidate_cache_range(test_buffer, sizeof(test_pattern));
    }
    
    results.inbound_coherent = (memcmp(test_buffer, test_pattern, sizeof(test_pattern)) == 0);
    
    // Test 3: 64KB Boundary Test
    results.boundary_safe = test_64kb_boundary_crossings(plan);
    
    return results;
}
```

---

### Phase 10: Final Activation & TSR Installation

**Purpose**: Enable all systems and go resident  
**Memory**: Hot section (final state)  
**Duration**: <1ms

**⚠️ CRITICAL**: Only NOW do we unmask IRQs and enable NIC interrupts!

```c
int final_activation_and_tsr(void) {
    // 10.1: Final NIC Activation
    for (int i = 0; i < g_driver_plan.nic_count; i++) {
        nic_info_t *nic = &g_driver_plan.selected_nics[i];
        nic_plan_t *plan = &g_driver_plan.nic_plans[i];
        
        // Clear any remaining pending interrupts
        clear_all_pending_interrupts(nic);
        
        // Enable NIC interrupts (but PIC still masked)
        enable_nic_interrupts(nic);
        
        // CRITICAL: Only NOW unmask IRQ at PIC
        unmask_irq_at_pic(plan->irq);
        
        log_info("NIC %d ACTIVE: %s mode, IRQ %d unmasked", 
                 i, plan->mode == NIC_MODE_DMA ? "DMA" : "PIO", plan->irq);
    }
    
    // 10.2: Print Final Configuration Summary
    print_driver_summary();
    
    // 10.3: Install Unload Handler (if supported)
    if (SUPPORT_UNLOAD) {
        install_unload_handler();
    }
    
    // 10.4: TSR Installation
    // Keep interrupt vectors, release everything else
    keep_interrupt_vectors();
    release_cold_section();
    
    tsr_with_resident_size((g_driver_plan.resident_size + 15) / 16);
    
    // This point should never be reached
    return TSR_INSTALLED;
}

void print_driver_summary(void) {
    printf("3Com Packet Driver v1.0 - Installation Complete\n");
    printf("Resident size: %d bytes (%dKB)\n", 
           g_driver_plan.resident_size, g_driver_plan.resident_size / 1024);
    printf("Software interrupt: INT %02Xh\n", g_target_software_vector);
    printf("CPU: %s, DMA policy: %s\n", 
           cpu_type_name(g_cpu_info.type),
           g_dma_policy == DMA_POLICY_ALLOWED ? "Enabled" : "PIO only");
    
    for (int i = 0; i < g_driver_plan.nic_count; i++) {
        nic_info_t *nic = &g_driver_plan.selected_nics[i];
        nic_plan_t *plan = &g_driver_plan.nic_plans[i];
        
        printf("NIC %d: %s at I/O 0x%X, IRQ %d, mode %s\n",
               i + 1, nic_type_name(nic->type), nic->io_base, nic->irq,
               plan->mode == NIC_MODE_DMA ? "DMA" : "PIO");
    }
    
    if (g_vds_info.present) {
        printf("VDS services: v%d.%d (flags: 0x%04X)\n",
               g_vds_info.major, g_vds_info.minor, g_vds_info.flags);
    }
}
```

---

## Phase Timing and Memory Layout

### Execution Timeline
```
Phase 0: Entry & Safety           [    <1ms ] ─── Cold Section ───
Phase 1: Platform Probe          [ 5-10ms  ]
Phase 2: ISA/PnP Prep            [  2-5ms  ]
Phase 3: NIC Discovery           [ 10-50ms ]
Phase 4: Feature Planning        [    <1ms ]
Phase 5: Memory Allocation       [  5-20ms ]
                                                ┌─ Hot Section ─┐
Phase 6: Relocation              [  1-3ms  ] ──┤              │
Phase 7: SMC Patching            [    <1ms ]   │   RESIDENT   │
Phase 8: Interrupt Setup         [    <1ms ]   │              │
Phase 9: NIC Init & DMA Tests    [ 10-100ms]   │              │
Phase 10: Final Activation       [    <1ms ] ──┘              │
                                                               │
                                    TSR Active ────────────────┘
```

### Memory Transition
```
Boot Start:     [Program][Environment][Cold Section]
After Phase 5:  [Program][Hot Target][Cold Section][DMA Buffers]
After Phase 6:  [Hot Section (Resident)][Cold Section][DMA Buffers]
TSR Final:      [Hot Section (Resident)][DMA Buffers]
                └─────────── ~13KB resident ────────────┘
```

## Critical Safety Validations

### V86 Mode Safety
```c
if (in_v86_mode && !vds_present) {
    // NEVER allow bus-master DMA without VDS in V86 mode
    // Physical addresses are virtualized and will corrupt memory
    disable_all_dma_operations();
}
```

### ISA DMA Constraints  
```c
// Every DMA buffer must be validated:
if (physical_address >= 16 * 1024 * 1024) {
    return DMA_ABOVE_16MB_LIMIT;
}
if ((physical_address & 0xFFFF0000) != 
    ((physical_address + size - 1) & 0xFFFF0000)) {
    return DMA_CROSSES_64KB_BOUNDARY;
}
```

### IRQ Safety
```c
// NEVER enable NIC interrupts before:
// 1. ISR is installed
// 2. NIC is fully programmed  
// 3. Pending interrupts are cleared
clear_all_pending_interrupts(nic);    // First
install_hardware_isr(irq);            // Second  
enable_nic_interrupts(nic);           // Third
unmask_irq_at_pic(irq);               // Last!
```

## Error Recovery and Cleanup

### Failure Points and Recovery
- **Phase 0-4 Failure**: Exit cleanly, restore IRQ masks, no TSR
- **Phase 5-6 Failure**: Free allocated memory, restore state, exit
- **Phase 7-9 Failure**: More complex - may need to uninstall vectors
- **Phase 10 Failure**: Should not happen - system is committed to TSR

### Cleanup Sequence
```c
void cleanup_on_failure(int failed_phase) {
    switch (failed_phase) {
        case 0: /* restore IRQ masks */ break;
        case 1-4: /* free temporary allocations */ break;  
        case 5-6: free_hot_section(); free_dma_buffers(); break;
        case 7-9: uninstall_vectors(); free_all_allocations(); break;
        default: /* TSR committed - cannot clean up */ break;
    }
    
    restore_pic_masks();
    exit_with_error_code(failed_phase);
}
```

This comprehensive boot sequence ensures safe operation across all DOS environments from 286 to Pentium 4, with proper handling of V86 mode, DMA constraints, cache coherency, and interrupt timing. The 10-phase structure provides clear error recovery points and maintains system stability throughout the initialization process.

---

*Architecture Version: 2.0*  
*GPT-5 Validated: 2025-08-28*  
*Supports: DOS 2.0+, 286-P4, V86 mode, EMM386, DMA safety, Cache coherency*