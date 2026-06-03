; =============================================================================
; osKernel.s  —  ARM Cortex-M4 assembly stubs for the RTOS kernel
;
; Two routines live here because they must touch the hardware stack pointer
; and CPU register file directly — things C cannot express:
;
;   1. osSchedularLaunch  — cold-start: restore the first thread's context
;                           and jump into it, effectively "entering" the RTOS.
;   2. PendSV_Handler     — the context-switch ISR, fired every SysTick tick
;                           (via INTCTRL write in SysTick_Handler).
;
; Memory model recap:
;   currentPt  — a C global of type tcbType*, always points to the TCB of the
;                thread that IS (or was last) running.
;   tcb.stackPt (first field, offset 0) — saved stack pointer for that thread.
;
; Cortex-M4 exception frame (hardware auto-saves on exception entry):
;   SP+0  : xPSR
;   SP+4  : PC  (return address / task entry point)
;   SP+8  : LR
;   SP+12 : R12
;   SP+16 : R3
;   SP+20 : R2
;   SP+24 : R1
;   SP+28 : R0
; The kernel manually saves/restores R4-R11 (callee-saved regs not in the
; hardware frame).
; =============================================================================

        AREA    |.text|, CODE, READONLY, ALIGN=2
        THUMB                          ; Generate Thumb-2 instructions
        PRESERVE8                      ; Maintain 8-byte stack alignment (ABI)

        EXTERN  currentPt              ; tcbType* defined in osKernel.c
        EXPORT  PendSV_Handler         ; Overrides the weak default in the CMSIS startup
        EXPORT  osSchedularLaunch      ; Called once from osKernelLaunch()
        IMPORT  osPriorityScheduler    ; C function — walks TCB list, updates currentPt


; =============================================================================
; PendSV_Handler
;
; Triggered by SysTick_Handler writing 0x10000000 to INTCTRL (PENDSVSET bit).
; PendSV is configured at the lowest priority (7) so it only runs after all
; higher-priority ISRs have completed — this prevents a context switch from
; tearing through an in-progress ISR.
;
; Sequence:
;   1. Disable interrupts (critical section around stack pointer manipulation).
;   2. Push the caller-saved register half (R4-R11) that the hardware did NOT
;      automatically save onto the CURRENT thread's stack.
;   3. Save the current SP into currentPt->stackPt (TCB offset 0).
;   4. Call osPriorityScheduler() — it inspects all TCBs and writes the
;      highest-priority ready thread's address into currentPt.
;   5. Load the NEW currentPt->stackPt into SP.
;   6. Pop R4-R11 from the new thread's stack.
;   7. Re-enable interrupts and return via BX LR.
;      The hardware then auto-restores xPSR, PC, LR, R12, R3-R0 from the
;      new thread's stack, completing the switch transparently.
; =============================================================================

PendSV_Handler

        CPSID   I                      ; Disable interrupts — begin atomic section

        ; --- Save current thread context ---
        PUSH    {R4-R11}               ; Push callee-saved regs onto current stack
                                       ; (hardware already saved R0-R3,R12,LR,PC,xPSR)

        LDR     R0, =currentPt         ; R0 = &currentPt  (address of the pointer)
        LDR     R1, [R0]               ; R1 = currentPt   (the TCB pointer itself)

        STR     SP, [R1]               ; currentPt->stackPt = SP
                                       ; TCB.stackPt is the FIRST member, so [R1+0]

        ; --- Select next thread ---
        PUSH    {R0, LR}               ; Preserve R0 (currentPt address) and LR
                                       ; (EXC_RETURN value — tells hardware which
                                       ;  stack/mode to return to)
        BL      osPriorityScheduler    ; Call C scheduler; it updates currentPt
        POP     {R0, LR}               ; Restore R0 and LR after the call

        ; --- Restore next thread context ---
        LDR     R1, [R0]               ; R1 = currentPt  (now the NEW thread's TCB)

        LDR     SP, [R1]               ; SP = new thread's saved stack pointer

        POP     {R4-R11}               ; Restore new thread's callee-saved regs

        CPSIE   I                      ; Re-enable interrupts

        BX      LR                     ; Return from exception:
                                       ; EXC_RETURN in LR tells the CPU to unstack
                                       ; R0-R3, R12, LR, PC, xPSR from the new SP,
                                       ; effectively resuming the new thread exactly
                                       ; where it was preempted.


; =============================================================================
; osSchedularLaunch
;
; Called exactly ONCE from osKernelLaunch() after SysTick is configured.
; At this point no thread is running; we manually unspool the first thread's
; fake initial stack frame (built by osKernelStackInit) and jump into it.
;
; The fake frame was laid out in TCB_STACK[i] as (from bottom to top):
;   [STACKSIZE-16] R4   \
;   [STACKSIZE-15] R5    |  manually saved regs (popped first by POP {R4-R11})
;   ...                  |
;   [STACKSIZE-9]  R11  /
;   [STACKSIZE-8]  R0   \
;   [STACKSIZE-7]  R1    |
;   [STACKSIZE-6]  R2    |  hardware exception frame (popped by BX LR)
;   [STACKSIZE-5]  R3    |
;   [STACKSIZE-4]  R12   |
;   [STACKSIZE-3]  LR    |
;   [STACKSIZE-2]  PC  ← task function pointer
;   [STACKSIZE-1]  xPSR (0x01000000 — Thumb bit set)
;
; Sequence mirrors what PendSV does on restore, but without a "save" phase:
;   1. Load currentPt (= &tcbs[0] after osKernelAddThreads).
;   2. Set SP to tcbs[0].stackPt.
;   3. Pop R4-R11 (fake init values — don't matter for first run).
;   4. Pop R0-R3, R12 (fake init values).
;   5. Skip the saved LR slot (ADD SP,#4) — not needed for cold start.
;   6. Pop LR (this will become the return address, overwritten by the task's PC).
;   7. Skip PSR slot (ADD SP,#4).
;   8. Enable interrupts.
;   9. BX LR — branches to the task's entry point (PC was sitting in LR after pop).
; =============================================================================

osSchedularLaunch

        LDR     R0, =currentPt         ; R0 = &currentPt
        LDR     R2, [R0]               ; R2 = currentPt = &tcbs[0]

        LDR     SP, [R2]               ; SP = tcbs[0].stackPt
                                       ; (points to TCB_STACK[0][STACKSIZE-16])

        POP     {R4-R11}               ; Pop fake R4-R11 init values (0x04040404 etc.)
        POP     {R0-R3}                ; Pop fake R0-R3
        POP     {R12}                  ; Pop fake R12
        ADD     SP, SP, #4             ; Skip the stacked LR slot (R14 placeholder)
        POP     {LR}                   ; LR = task entry point (the PC slot in frame)
        ADD     SP, SP, #4             ; Skip the xPSR slot

        CPSIE   I                      ; Enable interrupts — RTOS is now live

        BX      LR                     ; Jump to Task0(); never returns

        ALIGN
        END
