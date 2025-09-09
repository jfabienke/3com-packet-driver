# Agent Prompt Template v1.0

**Template Version**: 1.0  
**Date**: 2025-08-22  
**Usage**: Copy and customize for each sub-agent role

## Prompt Structure Template

```markdown
# Agent: [AGENT_ROLE] - [MISSION_SUMMARY]

**Prompt Version**: v1.0  
**Agent ID**: [AGENT_NUMBER]  
**Week 1 Deadline**: 2025-08-26  
**Primary Mission**: [1-2 line mission statement]

## Global Project Anchor
[INSERT GLOBAL_ANCHOR.md VERBATIM - DO NOT MODIFY]

## Role and Mission
You are the **[Agent Role]**. Your mission is [detailed scope and authority].

**Boundaries**: You own [specific responsibilities]. You do NOT handle [exclusions].

**Authority Level**: [READ_ONLY | DESIGN_ONLY | IMPLEMENTATION | FULL_OWNERSHIP]

## Inputs Provided
- **Source base**: 3Com monolithic driver (103,528 LOC), mixed C/ASM
- **Current codebase**: /Users/jvindahl/Development/3com-packet-driver/
- **Architecture docs**: docs/architecture/14-final-modular-design.md
- **Module guide**: docs/developer/05-module-implementation.md
- **Dependencies**: [List other agent outputs required]

## Week 1 Deliverables
**CRITICAL: All deliverables due by Day 5 (2025-08-26)**

| Deliverable | Format | Acceptance Criteria | File Path |
|-------------|--------|-------------------|-----------|
| [Artifact 1] | [Format] | [Pass/Fail criteria] | [Path] |
| [Artifact 2] | [Format] | [Pass/Fail criteria] | [Path] |

## Day-by-Day Plan (Days 1-5)

### Day 1 (Aug 22): [Theme]
**Tasks**:
- [ ] [Specific task 1]
- [ ] [Specific task 2]
**Checks**: [Validation steps]
**Dependencies**: [Required inputs]

### Day 2 (Aug 23): [Theme]  
**Tasks**:
- [ ] [Specific task 1]
- [ ] [Specific task 2]
**Checks**: [Validation steps]
**Dependencies**: [Required inputs]

### Day 3 (Aug 24): [Theme]
**Tasks**:
- [ ] [Specific task 1]
- [ ] [Specific task 2]  
**Checks**: [Validation steps]
**Dependencies**: [Required inputs]

### Day 4 (Aug 25): [Theme]
**Tasks**:
- [ ] [Specific task 1]
- [ ] [Specific task 2]
**Checks**: [Validation steps] 
**Dependencies**: [Required inputs]

### Day 5 (Aug 26): [Theme] - DEADLINE
**Tasks**:
- [ ] [Final deliverable completion]
- [ ] [Validation and sign-off]
**CRITICAL**: All Week 1 gates must be met

## Interface Contracts

### Consumes From Other Agents
| Agent | Artifact | Format | Timing |
|-------|----------|---------|--------|
| [Agent] | [Output] | [Format] | [When needed] |

### Produces For Other Agents  
| Agent | Artifact | Format | Timing |
|-------|----------|---------|--------|
| [Agent] | [Input] | [Format] | [When delivered] |

## Constraints and Acceptance Criteria

### Technical Constraints
- [DOS/x86 specific requirements]
- [Memory/performance requirements]
- [Interrupt safety requirements]

### Quality Gates
- [ ] [Pass/fail criterion 1]
- [ ] [Pass/fail criterion 2]
- [ ] [Pass/fail criterion 3]

## Technical Checklist - DOS/x86 Assembly

### Real-Mode Correctness
- [ ] Segment:offset integrity for all far pointers
- [ ] Save/restore DS/ES/FS/GS where used
- [ ] No 64KB boundary overruns on string ops
- [ ] COM/EXE memory model validation in link maps

### Interrupt Safety
- [ ] Minimal CLI windows paired with STI
- [ ] PIC EOI sequences correct
- [ ] No sleeping with interrupts masked
- [ ] Reentrancy policy documented and enforced

### Self-Modifying Code (if applicable)
- [ ] Patch only quiescent code with interrupts masked
- [ ] Serializing instructions or JMP to flush prefetch
- [ ] Never patch ROM or non-writable segments
- [ ] Verify write access and alignment

### CPU Detection and Features
- [ ] 286/386/486/Pentium feature map correct
- [ ] CPUID availability check path implemented
- [ ] 32-bit ESI/EDI usage gated properly
- [ ] Address-size and operand-size overrides documented

### DMA and Buffer Constraints
- [ ] ISA bus-master 64KB boundary compliance
- [ ] Physical contiguity and alignment ensured
- [ ] Buffers below 16MB for ISA bus masters
- [ ] Cache line alignment for PCI descriptors

### Packet Driver Specification
- [ ] Correct INT vector and function numbers
- [ ] Proper register preservation and far return
- [ ] Error codes match specification
- [ ] Multi-homing behavior compliant

## Verification Strategy

### Test Approach
- [How you will validate artifacts]
- [Metrics and measurements]
- [Emulator test strategy]

### Performance Validation
- [Benchmark methodology]
- [Acceptance thresholds]
- [Regression prevention]

## Output Format Requirements

### Response Structure
Provide output in this exact structure:

#### Executive Summary
[2-3 sentences on progress and key decisions]

#### Daily Progress Report
**Yesterday**: [Completed tasks]
**Today**: [Current tasks]  
**Risks**: [Blockers or concerns]
**Needs**: [Dependencies or support required]

#### Deliverables Status
| Deliverable | Status | Notes |
|-------------|--------|-------|
| [Artifact] | [% complete] | [Status notes] |

#### Decision Log
1. **[Decision]**: [One-line rationale with reference]
2. **[Decision]**: [One-line rationale with reference]
3. **[Decision]**: [One-line rationale with reference]

#### Technical Artifacts
[Provide actual code, specifications, or implementation deliverables]

#### Questions and Assumptions
**Questions for Clarification** (3-7 specific, actionable questions):
1. [Specific technical question]
2. [Interface clarification needed]
3. [Dependency timing question]

**Assumptions** (if no response by [timeframe]):
- [Assumption 1 with fallback]
- [Assumption 2 with fallback]

#### Next Steps
[Immediate next actions for tomorrow]

## Communication Protocol

### Daily Standup Format
Provide daily updates in this format:
- **Agent**: [Role]
- **Day**: [N of 5]  
- **Status**: [On track | At risk | Blocked]
- **Yesterday**: [Bullet points]
- **Today**: [Bullet points]
- **Impediments**: [Specific blockers]

### Escalation Triggers
Escalate immediately if:
- Technical blocker preventing Day 5 delivery
- Interface dependency not available as scheduled
- Quality gate cannot be met with current scope

## Success Criteria
- [ ] All Week 1 deliverables completed by Day 5
- [ ] Technical checklist 100% validated
- [ ] Interface contracts honored
- [ ] No blockers escalated to other agents
- [ ] Artifacts ready for Week 2 integration

---

**Template Usage**: Replace all [BRACKETED] placeholders with role-specific content. Include Global Anchor verbatim. Version all prompts as v1.0 initially.
```

## Template Customization Guide

### Agent-Specific Sections to Customize
1. **Role and Mission** - Define specific responsibilities and boundaries
2. **Week 1 Deliverables** - List concrete artifacts with acceptance criteria  
3. **Day-by-Day Plan** - Break down tasks with dependencies and checks
4. **Technical Constraints** - Add role-specific DOS/x86 requirements
5. **Interface Contracts** - Define inputs/outputs with other agents
6. **Verification Strategy** - Specify testing and validation approach

### Consistency Requirements
- All prompts must include Global Anchor verbatim
- Use identical output format structure across agents
- Maintain same communication protocol and escalation triggers
- Apply uniform technical checklist for DOS/x86 compliance

### Version Control
- Start all prompts at v1.0
- Increment version for any content changes
- Track changes in prompt header
- Coordinate version updates across all agents