# Documentation Style and Template

Last Updated: 2025-09-04
Status: canonical

Purpose: Ensure consistent, readable, and maintainable documentation.

Header Template
- Title: One concise line
- Last Updated: YYYY-MM-DD
- Version: optional (e.g., 1.1)
- Status: canonical | supplemental | archived
- Purpose: single-sentence scope statement

Example
```
# DRIVER_BOOT_SEQUENCE

Last Updated: 2025-09-04
Version: 1.1
Status: canonical
Purpose: Authoritative cold→hot boot sequence for TSR install and activation.
```

Conventions
- Audience-first: “what, why, how”, with scope and out-of-scope
- Keep sections short; prefer bullets over long prose
- Present tense, active voice
- Cross-link related docs instead of duplicating content
- Call out constraints and acceptance criteria near the top (e.g., CPU/TSR, resident budget)

Structure
- Overview: context and scope
- Core content: grouped logically (e.g., Architecture, API, Boot Sequence)
- Constraints: requirements, budgets, compatibility
- References: external specs and internal links
- Version history: brief, at the end

Status Definitions
- canonical: Source of truth. Changes here drive implementation.
- supplemental: Supporting details, examples, or background.
- archived: Historical reference. Do not update except to add a pointer to the canonical doc.

Do’s
- Use explicit file references for cross-links (e.g., UNIFIED_DRIVER_ARCHITECTURE.md)
- Use consistent headings and ordering across related docs
- Prefer diagrams only when they add clarity; keep them simple

Don’ts
- Don’t duplicate whole sections across multiple docs
- Don’t leave stale “TBD” sections; add TODO with owner or remove
- Don’t mix canonical and historical content in the same document

Review Checklist
- Header complete (title, updated date, status, purpose)
- Links to canonical/supplemental/archived documents present
- Language is concise; sections are scan-friendly
- Constraints and acceptance criteria stated where relevant
- Version history appended (if material changes)

