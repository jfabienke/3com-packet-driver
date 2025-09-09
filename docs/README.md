# Documentation Index and Guidelines

Last Updated: 2025-09-04
Status: canonical

Purpose: Provide a simple, world-class structure for driver docs with clear navigation, ownership, and status.

Legend
- Status: canonical = source of truth; supplemental = supporting; archived = historical

Folder Index
- overview/: End-user docs (User Manual, Configuration, Troubleshooting)
- architecture/: Design and boot flow (subfolders: memory/, hardware/, cpu_detection/)
- api/: API reference and vendor extensions
- development/: Engineering plans, migration, releases, tuning
- performance/: Optimization techniques and SMC safety
- reference/: Schemas and technical summaries
- archive/: Historical documents and reports
- graphs/: Diagrams and dependency graphs
- build/: Generated-docs tooling (e.g., doxygen) and scripts
- testing/: Symlink to tests/docs

Quick Navigation
- Overview
  - canonical: overview/USER_MANUAL.md — End-user guide and quickstart
  - canonical: overview/CONFIGURATION.md — Options, parameters, examples
  - canonical: overview/TROUBLESHOOTING.md — Common issues and fixes
- Architecture
  - canonical: architecture/UNIFIED_DRIVER_ARCHITECTURE.md — Unified design and capability model
  - canonical: architecture/DRIVER_BOOT_SEQUENCE.md — Cold→hot init, TSR, and constraints
  - supplemental: architecture/memory/DMA_VS_PIO_ON_ISA.md — Trade-offs and policy
  - supplemental: architecture/memory/DMA_CONSTRAINTS.md — Alignment, 64KB crossings, VDS
  - supplemental: architecture/hardware/PCI_SUPPORT_PLAN.md — PCI families, scope
  - supplemental: architecture/hardware/PCI_DATAPATH_ARCHITECTURE.md — PCI datapath specifics
- API
  - canonical: api/API_REFERENCE.md — Packet Driver + vendor extensions
  - supplemental: api/VENDOR_EXTENSION_API.md — INT 60h extension set
  - supplemental: api/EXTENSION_INTEGRATION_GUIDE.md — Using extensions safely
- Development
  - supplemental: development/IMPLEMENTATION_PLAN.md — Project plan overview
  - supplemental: development/MIGRATION_GUIDE.md — Moving to unified core
  - supplemental: development/RELEASE_NOTES.md — Version changes
  - canonical: development/PERFORMANCE_TUNING.md — Performance guide (PCI tuning merged)
  - testing/: See tests/docs (symlink) for runners and guides
- Performance
  - supplemental: performance/SMC_SAFETY_PERFORMANCE.md — SMC/JIT and safety
  - supplemental: performance/OPTIMIZATION_ROADMAP.md — Priorities and phases
  - supplemental: performance/OPTIMIZATION_TECHNIQUES.md — Techniques catalog
- Reference
  - supplemental: BMTEST_SCHEMA_V1.2.md — JSON schema
  - supplemental: NIC_IRQ_IMPLEMENTATION_SUMMARY.md — IRQ approach
  - supplemental: PCMCIA_IMPLEMENTATION_SUMMARY.md — Scope & notes
- Archive (historical)
  - archive/: BOOT_SEQUENCE_* analyses, GPT5_* and STAGE_* reports, weekly/phase summaries, legacy implementation trackers

Status Tags and Headers
- Every doc should start with: Title, Last Updated, Version (optional), Status
- Canonical docs must link to relevant supplemental docs and back

Style and Format
- Keep docs concise: short sections, clear scope/out-of-scope
- Prefer present tense; avoid duplication across files
- Cross-link related topics (architecture ↔ API ↔ configuration)
- See STYLE.md for template and conventions

Migration Plan (lightweight)
1) Consolidate overlaps
   - USER_GUIDE.md → merge into overview/USER_MANUAL.md (then archive donor)
   - api_documentation.md → merge into api/API_REFERENCE.md (then archive donor)
   - Choose development/PERFORMANCE_TUNING.md vs DRIVER_TUNING.md; merge into one
   - UNIFIED_DRIVER_IMPLEMENTATION.md → fold unique bits into architecture/UNIFIED_DRIVER_ARCHITECTURE.md or development/, then archive
2) Rationalize boot docs
   - Keep architecture/DRIVER_BOOT_SEQUENCE.md as canonical
   - Move BOOT_SEQUENCE_ARCHITECTURE.md, BOOT_SEQUENCE_IMPLEMENTATION.md, BOOT_SEQUENCE_GAPS.md to archive/ with a pointer
3) PCI documentation
   - Keep PCI_SUPPORT_PLAN.md and PCI_DATAPATH_ARCHITECTURE.md as supplemental under architecture/hardware/
4) Memory documentation
   - Ensure DMA_VS_PIO_ON_ISA.md and DMA_CONSTRAINTS.md are linked from boot sequence and architecture
5) Update headers
   - Add Status: canonical/supplemental/archived to top of each document

Ownership
- Canonical owners: Architecture lead for architecture/UNIFIED_DRIVER_ARCHITECTURE.md and architecture/DRIVER_BOOT_SEQUENCE.md; API lead for api/API_REFERENCE.md; Release lead for development/RELEASE_NOTES.md; Docs lead for overview/USER_MANUAL.md and overview/CONFIGURATION.md

Notes
- Do not delete archived documents; keep for traceability with a pointer to the canonical replacement
- Prefer linking to tests/docs for detailed runner instructions instead of duplicating content
