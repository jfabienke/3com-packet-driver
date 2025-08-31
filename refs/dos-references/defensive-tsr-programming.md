Robust DOS TSR & Packet‑Driver Engineering (Watcom C + NASM)

A production‑ready reference that folds in the key corrections and field‑tested patterns for building resilient DOS TSRs and packet drivers. Organized as:
	1.	Checklist of defensive patterns (with “why this matters”).
	2.	Drop‑in scaffolding for NASM + Watcom C.
	3.	Gotchas explained (the deep rules behind the rules).
	4.	Troubleshooting matrix (symptom → cause → remedy).

⸻

1) Corrected Patterns Checklist (Do These Every Time)

1. Minimal ISR work; defer the rest

What: Acknowledge the device and capture minimal state in a small ring buffer. Defer heavy work to a safe context (INT 28h idle for DOS‑OK paths, or INT 1Ch timer for pure memory/IO paths).
Why it matters: Hardware ISRs run with tight latency constraints; long handlers cause missed IRQs and reentrancy disasters.

2. DOS reentrancy gate (InDOS + CritErr)

What: Query InDOS/Critical Error pointers at install; never call DOS from hardware ISRs. Before any DOS call in non‑ISR code, bail if DOS is busy.
Why: DOS is not reentrant; calling it from the wrong context corrupts SDA and internal state.

3. Known‑good private stack on every entry (ISR/API)

What: Swap to your own stack immediately in handlers; restore on exit. Pair MOV SS with the very next instruction MOV SP.
Why: Caller stacks are small/hostile; other TSRs can clobber them. MOV SS masks interrupts until after the next instruction; use this window correctly.

4. Segment hygiene at entry

What: Load DS/ES to your own segment (mov ax,cs / mov ds,ax / mov es,ax) before touching memory.
Why: You cannot trust caller’s DS/ES; APIs/ISRs are far‑entered with arbitrary data segments.

5. Critical sections that restore IF precisely

What: Use pushf/cli to enter and popf to exit. Keep CLI windows tiny.
Why: Preserves the previous IF state instead of blindly sti/cli.

6. Interrupt vector hygiene (install, chain, verify, uninstall)

What: Save previous vector; set your own; chain cleanly if the interrupt isn’t yours. Uninstall only if you still own the vector (or can walk the chain back to you). Periodically verify vectors and re‑claim if hijacked.
Why: TSR stacking is common; restoring someone else’s vector bricks the system.

7. PIC/IRQ correctness

What: If you serviced a slave IRQ (8–15), send EOI to 0xA0 then 0x20. For master‑only, EOI to 0x20. Chain without EOI if the event wasn’t yours (unless sharing policy dictates otherwise).
Why: Wrong EOI sequencing yields stuck IRQ lines or spurious interrupts.

8. Timeouts + recovery around hardware waits

What: Bounded loops for device readiness; on timeout, reset device or mark fault.
Why: Prevents lockups from non‑responsive hardware.

9. Shared data integrity

What: Guard shared structures with short critical sections; add magic signatures + cheap checksums to resident structures; consider canaries.
Why: Detects corruption early and enables safe recovery paths.

10. Presence, versioning, and API contracts

What: Provide a robust probe (signature, version, feature bits) and validated API table. Validate all caller far pointers and lengths.
Why: Enables coexistence and safer integration with other residents.

11. Deferred‑work queue (DPC‑like)

What: Tiny lock‑protected ring buffer for ISR‑posted events; drained by worker (INT 1Ch/28h).
Why: Keeps ISRs fast and moves risky work out of interrupt context.

12. Memory discipline & TSR‑keep

What: Use AH=31h (Terminate & Stay Resident) with exact paragraphs. Free/detach your environment before staying resident.
Why: Avoids memory leaks and keeps resident footprint minimal.

13. Diagnostics build mode

What: Compile‑time switch for vector health counters, last‑error ring, optional serial logging (never heavy in ISRs), and a panic record area.
Why: Field debugging without special tools.

14. Safe uninstall & reload

What: Only restore vectors/IRQs if you still own them; otherwise refuse and report.
Why: Prevents breaking later TSRs in the chain.

15. Packet‑driver etiquette (INT 60h)

What: Follow pkt‑driver ABI: presence returns signature/version, validate buffers, don’t assume reentrancy, cooperate when sharing INT 60h.
Why: The packet‑driver ecosystem expects these contracts to coexist.

⸻

2) Drop‑in Scaffolding (NASM + Watcom C)

Notes
	•	Target: 16‑bit real mode, DOS.
	•	Open Watcom C function names get a leading underscore by default; export matching labels from NASM (e.g., global _our_irq_handler).
	•	NASM object format for Watcom: -f obj (OMF).

2.1 tsr.inc — core macros & utilities (NASM, corrected ISR prolog/epilog)

; tsr.inc — include in resident ASM units
BITS 16

extern _drv_ss, _drv_sp_top      ; set by installer (paragraphs/words)
extern _saved_ss, _saved_sp

%macro ENTER_CRIT 0
    pushf
    cli
%endmacro

%macro EXIT_CRIT 0
    popf
%endmacro

%macro PUSH_ALL 0
    push ax
    push bx
    push cx
    push dx
    push si
    push di
    push bp
%endmacro

%macro POP_ALL 0
    pop bp
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
%endmacro

%macro LOAD_CS_DS_ES 0
    push ax
    mov ax, cs
    mov ds, ax
    mov es, ax
    pop ax
%endmacro

; Switch to driver stack — pair MOV SS with next MOV SP
%macro SWITCH_TO_DRIVER_STACK 0
    cli                         ; keep window tight
    mov [cs:_saved_ss], ss
    mov [cs:_saved_sp], sp
    mov ax, [cs:_drv_ss]
    mov ss, ax                  ; IF masked until after next instr
    mov sp, [cs:_drv_sp_top]    ; paired write
%endmacro

%macro RESTORE_CALLER_STACK 0
    mov ss, [cs:_saved_ss]
    mov sp, [cs:_saved_sp]
%endmacro

; ISR skeleton: DO NOT pushf here; IRET needs original stack layout
%macro ISR_PROLOG 0
    PUSH_ALL
    push ds
    push es
    LOAD_CS_DS_ES
    SWITCH_TO_DRIVER_STACK
%endmacro

%macro ISR_EPILOG 0
    RESTORE_CALLER_STACK
    pop es
    pop ds
    POP_ALL
    iret
%endmacro

; AX <- XOR checksum over ES:DI, CX bytes
global _calc_xor_checksum
_calc_xor_checksum:
    push bx
    push cx
    push di
    push ds
    push es
    xor ax, ax
    mov bx, di
.chk:
    xor al, [es:bx]
    inc bx
    loop .chk
    cbw
    pop es
    pop ds
    pop di
    pop cx
    pop bx
    ret

2.2 Safe vector install/uninstall (vectors.asm)

BITS 16
%define INT_PKT 0x60

segment _DATA public class=DATA use16
old_pkt_off dw 0
old_pkt_seg dw 0

segment _TEXT public class=CODE use16
extern _our_pkt_handler

get_old_pkt:
    mov ax, 0x3500 + INT_PKT   ; ES:BX = old vector
    int 0x21
    mov [old_pkt_off], bx
    mov [old_pkt_seg], es
    ret

set_new_pkt:
    push ds
    mov dx, _our_pkt_handler
    mov ax, 0x2500 + INT_PKT
    mov ds, cs
    int 0x21
    pop ds
    ret

; Returns CF=0 if restored, CF=1 if not owner
uninstall_pkt_if_owner:
    mov ax, 0x3500 + INT_PKT
    int 0x21                  ; ES:BX = current vector
    cmp bx, _our_pkt_handler
    jne .not_owner
    cmp es, cs
    jne .not_owner
    mov dx, [old_pkt_off]
    mov ds, [old_pkt_seg]
    mov ax, 0x2500 + INT_PKT
    int 0x21
    clc
    ret
.not_owner:
    stc
    ret

2.3 IRQ service skeleton (irq.asm) — correct PIC EOI and chaining

BITS 16
%include "tsr.inc"

extern _old_irq_far_ptr    ; dd old handler far ptr
extern _nic_is_ours        ; CF=0 ours, CF=1 not ours
extern _nic_service
extern _nic_ack

segment _TEXT public class=CODE use16

global _our_irq_handler
_our_irq_handler:
    ISR_PROLOG

    call _nic_is_ours
    jc  .chain

    call _nic_service
    call _nic_ack

    ; EOI — adjust if this is a slave IRQ
    mov al, 0x20
    out 0x20, al

    ISR_EPILOG

.chain:
    ; No EOI here if not ours
    jmp far [cs:_old_irq_far_ptr]

2.4 Packet‑driver entry (packet_api.asm) — INT 60h etiquette

BITS 16
%include "tsr.inc"

segment _DATA public class=DATA use16
pkt_sig db 'PKT DRVR',0
pkt_ver dw 0x0134

segment _TEXT public class=CODE use16

global _our_pkt_handler
_our_pkt_handler:
    ; Far entry from INT 60h — treat like ISR (no PUSHF here)
    ISR_PROLOG

    cmp ah, 0               ; presence call?
    jne .dispatch
    mov es, cs
    lea di, [pkt_sig]
    mov bx, [pkt_ver]
    clc
    jmp .ep

.dispatch:
    ; Validate inputs, avoid DOS here; queue work if needed
    ; set CF on error, clear on success
    clc

.ep:
    ISR_EPILOG

2.5 Watcom C glue (tsr.h)

/* tsr.h */
#ifndef TSR_H
#define TSR_H
#include <i86.h>
#include <dos.h>

typedef void (__interrupt __far *isr_t)(void);

/* InDOS/Critical Error pointers (AH=34h) */
extern volatile unsigned char far *g_inDos;
extern volatile unsigned char far *g_critErr;

static inline void get_inDos_pointers(void) {
    union REGS r; struct SREGS s;
    r.h.ah = 0x34; intdosx(&r, &r, &s);
    g_inDos  = (unsigned char far *)MK_FP(s.es, _BX);
    g_critErr= (unsigned char far *)MK_FP(s.es, _DI);
}

static inline int dos_busy_now(void) {
    return (*g_inDos != 0) || (*g_critErr != 0);
}

static inline isr_t getvect8(unsigned int intno) { return _dos_getvect(intno); }
static inline void  setvect8(unsigned int intno, isr_t h) { _dos_setvect(intno, h); }

/* Far pointer validation helpers */
static inline int fp_in_range(void far *p, unsigned len, unsigned min_seg, unsigned max_seg) {
    unsigned seg = FP_SEG(p);
    return (seg >= min_seg) && (seg <= max_seg) && (len < 0xF000);
}

#endif

2.6 Deferred‑work ring (dpc_queue.c)

#include "tsr.h"

#define QSIZE 32
typedef struct { unsigned char code; unsigned short a, b; } evt_t;

static volatile evt_t q[QSIZE];
static volatile unsigned char q_head, q_tail;

#pragma aux cli  = "cli"  modify []
#pragma aux sti  = "sti"  modify []

static void q_push_isr(unsigned char code, unsigned short a, unsigned short b) {
    unsigned char next = (unsigned char)((q_head + 1) % QSIZE);
    if (next == q_tail) return; /* drop on overflow */
    q[q_head].code = code; q[q_head].a = a; q[q_head].b = b;
    q_head = next;
}

static int q_pop(evt_t *out) {
    int ok = 0; cli();
    if (q_tail != q_head) { *out = q[q_tail]; q_tail = (unsigned char)((q_tail + 1) % QSIZE); ok = 1; }
    sti();
    return ok;
}

/* INT 1Ch worker: drain memory‑only work (no DOS calls here). */
void __interrupt __far worker_1Ch(void) {
    evt_t e;
    while (q_pop(&e)) {
        /* handle e */
    }
}

/* INT 28h worker: safe place to make DOS calls if dos_busy_now()==0 */
void __interrupt __far worker_28h(void) {
    if (dos_busy_now()) return;
    /* drain a DOS‑needing queue if you keep a separate one */
}

2.7 Installer & TSR‑keep (install.c)

#include "tsr.h"

volatile unsigned char far *g_inDos, *g_critErr;

static isr_t old_1Ch;
extern void __interrupt __far _our_irq_handler(void);
extern unsigned short _drv_ss, _drv_sp_top, _saved_ss, _saved_sp;

static void setup_private_stack(void) {
    /* Allocate a small stack segment for handlers (e.g., via DOS alloc, keep para)
       For brevity, assume installer prepared _drv_ss and _drv_sp_top. */
}

void install_all(void) {
    get_inDos_pointers();
    setup_private_stack();

    old_1Ch = getvect8(0x1C);
    setvect8(0x1C, worker_1Ch);

    /* Hook INT 28h if you maintain a DOS‑capable queue */
    /* setvect8(0x28, worker_28h); */

    /* Hook hardware IRQ/INT 60h via ASM routines */
}

void uninstall_if_possible(void) {
    setvect8(0x1C, old_1Ch);
    /* Call ASM uninstall for INT 60h and IRQ if you still own them */
}

/* Terminate & Stay Resident with precise paragraphs */
void stay_resident(unsigned paras) {
    union REGS r; r.h.ah = 0x31; r.x.dx = paras; r.h.al = 0; intdos(&r, &r);
}

2.8 Diagnostics mode (C)

#ifdef DEBUG_BUILD
#define DBG_INC(x)   (++g_diag.x)
#define DBG_PANIC(c) do{ g_diag.panic=(c); }while(0)
#else
#define DBG_INC(x)   (void)0
#define DBG_PANIC(c) (void)0
#endif

struct diag {
    unsigned vector_rehook_count;
    unsigned queue_overflow_count;
    unsigned short panic;
    unsigned short last_errors[8];
} g_diag;

2.9 Build sketch (Open Watcom + NASM)

rem Assemble
nasm -f obj tsr.asm -o tsr.obj
nasm -f obj vectors.asm -o vectors.obj
nasm -f obj irq.asm -o irq.obj
nasm -f obj packet_api.asm -o packet_api.obj

rem Compile (model: small/compact as needed)
wcl -mc -zq -s -bt=dos -fe=driver.com install.c dpc_queue.c tsr.obj vectors.obj irq.obj packet_api.obj

Adjust memory model (-ms/-mc) to match your segmentation strategy. Ensure public symbol names match (leading underscores for C‑visible labels).

⸻

3) Why These Rules Are True (Deep Gotchas)
	•	IRET vs RETF: An INT pushes FLAGS, CS, IP (top to bottom IP→CS→FLAGS). Handlers must IRET to pop them. Pushing extra FLAGS at entry corrupts the return frame unless you also popf before iret—hence our ISR prolog avoids pushf entirely.
	•	MOV SS pairing: After MOV SS,xxxx, interrupts are inhibited until after the next instruction. Pair it immediately with MOV SP,yyyy. Keep the window tight with an initial cli and avoid anything between those two instructions.
	•	PIC EOI ordering: For slave IRQs (8–15), the slave PIC (0xA0) must be EOId first, then the master (0x20). Wrong order = stuck IRQs.
	•	INT 28h vs INT 1Ch lanes: INT 1Ch runs at each tick—fine for memory/IO work only. INT 28h (DOS idle) is the only ISR‑ish place you should consider DOS calls, and even then only when dos_busy_now()==0.
	•	Segment hygiene: Far entries bring arbitrary DS/ES. Uniformly load them from CS (or your dedicated data segment) before dereferencing pointers.
	•	Vector ownership: TSR chains can be long. Never restore vectors unless you still own them; otherwise refuse uninstall. Periodic verification + counters helps diagnose “stolen vector” scenarios.
	•	Pointer validation: User/caller buffers must be range‑checked (segment, length) and protected with short critical sections to avoid TOCTOU with other ISRs.

⸻

4) Troubleshooting Matrix

Symptom	Likely Cause	Fix Pattern
Spurious or continuous interrupts	Missed/incorrect EOI (esp. slave PIC)	Send EOI 0xA0 then 0x20 for IRQs 8–15; only EOI if you actually handled it
Random crashes on return from ISR	Extra pushf without matching popf; wrong prolog	Use ISR prolog that does not pushf; end with iret
Hang during device wait	Infinite spin waiting for status	Add bounded timeout; on expiry, reset device or mark error
DOS corruption, weird file errors	Called DOS from ISR or while InDOS/CritErr set	Move work to INT 28h path; check dos_busy_now()
Stack overflows, data scribbles	Using caller’s tiny/hostile stack	Switch to private stack at entry; keep ISR minimal
Uninstall breaks other TSRs	Restoring vectors you don’t own	Verify vector points to you (or chain includes you) before restoring
Occasional lost packets/IRQs	Long CLI sections blocking nested IRQs	Shorten critical sections; move work to deferred queue
Intermittent GPF/lockups on heavy traffic	Reentrancy or DS/ES not normalized	Load DS/ES from CS on entry; audit shared data guards
Presence/probe fails with some stacks	ABI mismatch or unvalidated pointers	Return signature/version/feature bits exactly as specified; validate far pointers


⸻

Appendix: Snippets You’ll Reuse

Critical section macros (non‑ISR code):

%macro ENTER_CRIT 0
    pushf
    cli
%endmacro
%macro EXIT_CRIT 0
    popf
%endmacro

Slave EOI helper (ASM):

; For IRQs 8–15
mov al, 0x20
out 0xA0, al
out 0x20, al

Far pointer guard (C):

int safe_copy_from_user(void far *dst, void far *src, unsigned len) {
    if (!fp_in_range(src, len, 0x1000, 0x9FFF)) return 0; /* tune range */
    _fmemcpy(dst, src, len);
    return 1;
}

TSR‑keep (DOS int 21h, AH=31h):

void stay_resident(unsigned paras) {
    union REGS r; r.h.ah = 0x31; r.x.dx = paras; r.h.al = 0; intdos(&r, &r);
}


⸻

Final Checklist (paste into your README)
	•	ISR does the absolute minimum; defers via 1Ch/28h
	•	No DOS calls in hardware ISRs
	•	Private stack switch at every entry; proper MOV SS pairing
	•	DS/ES normalized on entry
	•	Critical sections use pushf/cli … popf
	•	Interrupt vectors saved, verified, chained, and safely uninstalled
	•	Slave/master EOI order correct
	•	Timeouts & recovery on all hardware waits
	•	Pointer/length validation for all external API calls
	•	Diagnostics counters & panic record present (debug build)
	•	TSR‑keep via AH=31h with exact paragraphs; environment detached

⸻

Use this as your canonical template. If you want a minimal, buildable skeleton (COM) with makefile and a loopback “fake NIC” IRQ to test the DPC path, we can add that as a companion sample.
