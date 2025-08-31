# DOS TSR Programming Reference

## Memory Layout Considerations
- **Resident Code**: Must stay in memory after initialization
- **Initialization Code**: Can be discarded after setup
- **Data Structures**: Organize based on access patterns

## Critical TSR Requirements
1. **Memory Models**: Use small memory model (code+data < 64K)
2. **Stack Management**: SS != DS, no stack probes (-zu flag)
3. **Interrupt Handling**: Save/restore all registers
4. **Reentrancy**: Protect critical sections from interrupts

## DOS Function Calls from TSR
- **Safe Functions**: INT 21h functions 01h-0Ch, 30h, 33h-48h
- **Unsafe Functions**: File I/O during hardware interrupts
- **Indos Flag**: Check before calling DOS functions
- **Critical Error Handler**: Install custom handler

## Memory Allocation Strategy
1. **Conventional Memory**: < 640KB, most compatible
2. **Upper Memory Blocks**: 640KB-1MB, requires UMB driver
3. **Extended Memory**: > 1MB, requires XMS driver
4. **High Memory Area**: First 64KB of extended memory

## Interrupt Vector Management
```asm
; Save original interrupt vector
mov ax, 3500h + IRQ_NUMBER    ; Get interrupt vector
int 21h                       ; ES:BX = original handler
mov word [old_handler], bx
mov word [old_handler+2], es

; Install new interrupt handler
mov ax, 2500h + IRQ_NUMBER    ; Set interrupt vector
mov dx, offset new_handler
int 21h

; Chain to original handler
pushf                         ; Simulate interrupt call
call far [old_handler]
```

## TSR Installation Check
```asm
; Check if already installed using signature
mov ax, 1234h                 ; Installation signature
int 60h                       ; Packet driver interrupt
cmp ax, 5678h                 ; Expected response
je already_installed
```

## Linker Directives for TSR
```
system com                    ; Create COM file
option stack=1024            ; Set stack size
option map                   ; Generate memory map
order                        ; Specify segment order
  clname 'CODE' segment '_TEXT'
  clname 'DATA' segment '_DATA'
  clname 'INIT' segment '_INIT'
```

## Build Flags for Watcom C
```
-ms    ; Small memory model
-zdf   ; DS != SS (required for TSR)
-zu    ; No stack overflow checks
-s     ; Remove stack checks
-zp1   ; Pack structures on byte boundaries
```

## Practical TSR Size Limits
- **Resident Portion**: < 32KB recommended for compatibility
- **Total Memory Usage**: < 64KB for small memory model
- **Stack Space**: 1-4KB sufficient for most packet drivers