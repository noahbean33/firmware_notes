/* =============================================================================
 * osKernel.c  —  STM32F4 RTOS kernel implementation
 *
 * Architecture: preemptive, priority-based round-robin on a Cortex-M4.
 *
 * Key design choices:
 *   - SysTick ISR   → sets PENDSVSET to schedule PendSV at lowest priority.
 *   - PendSV ISR    → the ONLY place a context switch actually happens
 *                     (in osKernel.s).  Running at lowest priority ensures
 *                     it never tears through a higher-priority ISR.
 *   - TIM4 ISR      → 1 kHz tick for sleep countdown + periodic callbacks.
 *   - osPriorityScheduler  → O(N) linear scan; lowest numeric priority wins.
 *   - Stacks        → statically allocated 2D array, 100 words per thread.
 * ============================================================================= */

#include "osKernel.h"

/* ----------------------------- Configuration ----------------------------- */
#define NUM_OF_THREADS      8     /* Maximum concurrent threads                */
#define STACKSIZE           100   /* Stack depth per thread in 32-bit words     */
#define NUM_PERIODIC_TASK   5     /* Maximum soft-timer callbacks               */

#define BUS_FREQ            16000000   /* STM32F401/F411 default HSI = 16 MHz   */

/* ----------------------------- Type aliases ------------------------------ */
typedef void(*taskT)(void);      /* Convenience typedef for task function ptrs  */

#define NULL (void*)0            /* Avoid pulling in <stdlib.h> just for NULL   */

/* -------- Hardware register aliases (avoids CMSIS HAL for speed) --------- */

/* System Handler Priority Register 3: bits[31:24] control PendSV priority.  */
#define SYSPRI3   (*((volatile uint32_t *)0xE000ED20))

/* Interrupt Control and State Register: writing bit 28 sets PENDSVSET,
   writing bit 26 sets SYSTICKSET — used to pend exceptions from software.   */
#define INTCTRL   (*((volatile uint32_t *)0xE000ED04))

/* ========================= Periodic Task Table =========================== */

/* Soft-timer descriptor — executed from TIM4 ISR, not as a full thread.     */
typedef struct {
    taskT    task;      /* Callback to invoke                                 */
    uint32_t period;    /* Reload value in TIM4 ticks (ms at 1 kHz)          */
    uint32_t timeLeft;  /* Countdown; fires when this reaches 0               */
} periodicTaskT;

static periodicTaskT periodicTask[NUM_PERIODIC_TASK];
static int32_t  NumPeriodicThreads = 0;  /* Count of registered soft timers  */

/* (TimeMsec and MaxPeriod are declared but not actively used in this build)  */
static uint32_t TimeMsec;
static uint32_t MaxPeriod;

/* =============================== TCB Layout ============================== */
/*
 * Thread Control Block.  The FIRST field MUST be stackPt because the
 * assembly code in osKernel.s does:
 *     STR SP, [R1]      ; currentPt->stackPt = SP  (R1 = currentPt, offset 0)
 *     LDR SP, [R1]      ; SP = currentPt->stackPt
 * Any reordering of the struct breaks context switching.
 */
struct tcb {
    int32_t    *stackPt;   /* Saved stack pointer — MUST be first (offset 0)  */
    struct tcb *nextPt;    /* Next TCB in the circular linked list             */
    uint32_t    sleepTime; /* Ticks remaining before thread wakes (0 = awake) */
    uint32_t    blocked;   /* Non-zero if thread is blocked on a semaphore     */
    uint32_t    priority;  /* Scheduler priority (lower = higher priority)     */
};

typedef struct tcb tcbType;

/* Static TCB pool and associated stacks */
tcbType tcbs[NUM_OF_THREADS];
tcbType *currentPt;                           /* Pointer to currently-running TCB */
int32_t TCB_STACK[NUM_OF_THREADS][STACKSIZE]; /* Static stack storage             */

uint32_t MILLIS_PRESCALER;   /* SysTick reload divisor per millisecond          */

/* Forward declaration (defined later, called from osKernelInit) */
void osPeriodicTask_Init(void(*task)(void), uint32_t freq, uint8_t priority);

/* Assembly stub defined in osKernel.s */
void osSchedularLaunch(void);

/* ====================== Stack Initialization ============================= */
/*
 * osKernelStackInit
 *
 * Builds a fake Cortex-M4 exception frame on thread i's stack so that
 * PendSV_Handler / osSchedularLaunch can "return into" it as if it had
 * been preempted.
 *
 * Cortex-M4 auto-saves on exception entry (hardware frame, top of stack):
 *   [STACKSIZE-1]  xPSR  ← must have bit 24 set (Thumb mode)
 *   [STACKSIZE-2]  PC    ← task entry point, written by osKernelAddThreads
 *   [STACKSIZE-3]  LR    ← ELR_EXC (0x14141414 placeholder; overwritten)
 *   [STACKSIZE-4]  R12
 *   [STACKSIZE-5]  R3
 *   [STACKSIZE-6]  R2
 *   [STACKSIZE-7]  R1
 *   [STACKSIZE-8]  R0
 *
 * Manually saved by PendSV (below hardware frame):
 *   [STACKSIZE-9]  R11
 *   ...
 *   [STACKSIZE-16] R4    ← stackPt set here (initial SP value)
 *
 * The magic constants (0x04040404 etc.) are diagnostic — they make it
 * easy to spot uninitialized registers under a debugger.
 */
void osKernelStackInit(int i) {
    tcbs[i].stackPt = &TCB_STACK[i][STACKSIZE - 16]; /* Initial SP              */

    /* xPSR: Thumb bit (bit 24) must be 1 or the CPU will fault on return       */
    TCB_STACK[i][STACKSIZE - 1]  = 0x01000000;

    /* Hardware exception frame (auto-saved/restored by Cortex-M4 on exception) */
    /* PC is written separately in osKernelAddThreads after this call            */
    TCB_STACK[i][STACKSIZE - 3]  = 0x14141414; /* R14 (LR)                      */
    TCB_STACK[i][STACKSIZE - 4]  = 0x12121212; /* R12                           */
    TCB_STACK[i][STACKSIZE - 5]  = 0x03030303; /* R3                            */
    TCB_STACK[i][STACKSIZE - 6]  = 0x02020202; /* R2                            */
    TCB_STACK[i][STACKSIZE - 7]  = 0x01010101; /* R1                            */
    TCB_STACK[i][STACKSIZE - 8]  = 0x00000000; /* R0                            */

    /* Manually saved callee-saved registers (saved/restored by PendSV_Handler) */
    TCB_STACK[i][STACKSIZE - 9]  = 0x11111111; /* R11                           */
    TCB_STACK[i][STACKSIZE - 10] = 0x10101010; /* R10                           */
    TCB_STACK[i][STACKSIZE - 11] = 0x09090909; /* R9                            */
    TCB_STACK[i][STACKSIZE - 12] = 0x08080808; /* R8                            */
    TCB_STACK[i][STACKSIZE - 13] = 0x07070707; /* R7                            */
    TCB_STACK[i][STACKSIZE - 14] = 0x06060606; /* R6                            */
    TCB_STACK[i][STACKSIZE - 15] = 0x05050505; /* R5                            */
    TCB_STACK[i][STACKSIZE - 16] = 0x04040404; /* R4  ← SP starts here          */
}

/* ==================== Periodic Task Registration ========================= */
/*
 * osKernelAddPeriod_Thread
 *
 * Registers a soft-timer callback.  The callback runs in TIM4 ISR context
 * (priority 6), NOT as an RTOS thread — it has no independent stack and
 * must complete quickly to avoid starving other ISRs.
 *
 * timeLeft is initialised to period-1 so the first fire happens after a
 * full period (not immediately at t=0).
 */
uint8_t osKernelAddPeriod_Thread(void(*task)(void), uint32_t period) {
    if (NumPeriodicThreads == NUM_PERIODIC_TASK || period == 0)
        return 0; /* Table full or zero period requested                        */

    periodicTask[NumPeriodicThreads].task     = task;
    periodicTask[NumPeriodicThreads].period   = period;
    periodicTask[NumPeriodicThreads].timeLeft = period - 1; /* skip t=0 fire   */

    NumPeriodicThreads++;
    return 1;
}

/* ====================== Periodic Event Dispatcher ======================== */
/*
 * periodic_event_execute
 *
 * Called from TIM4_IRQHandler every 1 ms.
 * Two jobs:
 *   1. Walk the soft-timer table and fire any callbacks whose countdown
 *      has reached zero, then reload them.
 *   2. Decrement the sleepTime of every thread that is currently sleeping.
 *      When sleepTime hits 0 the thread becomes eligible for scheduling
 *      on the next PendSV invocation.
 */
void periodic_event_execute(void) {
    int i;

    /* --- Soft-timer dispatch -------------------------------------------- */
    for (i = 0; i < NumPeriodicThreads; i++) {
        if (periodicTask[i].timeLeft == 0) {
            periodicTask[i].task();                         /* Fire callback  */
            periodicTask[i].timeLeft = periodicTask[i].period - 1; /* Reload */
        } else {
            periodicTask[i].timeLeft--;
        }
    }

    /* --- Sleep countdown ------------------------------------------------- */
    for (i = 0; i < NUM_OF_THREADS; i++) {
        if (tcbs[i].sleepTime > 0) {
            tcbs[i].sleepTime--; /* Wakes when this reaches 0                  */
        }
    }
}

/* ======================== Thread Registration ============================ */
/*
 * osKernelAddThreads
 *
 * Hard-coded for exactly 8 threads.  The circular list order (0→1→...→7→0)
 * determines traversal order inside osPriorityScheduler but NOT which thread
 * actually runs — the scheduler picks the lowest-priority-number ready thread.
 *
 * Note: __disable_irq / __enable_irq guard the TCB initialisation so an
 * early SysTick (if somehow enabled) cannot see a half-built TCB list.
 */
uint8_t osKernelAddThreads(
    void(*task0)(void), uint32_t priority0,
    void(*task1)(void), uint32_t priority1,
    void(*task2)(void), uint32_t priority2,
    void(*task3)(void), uint32_t priority3,
    void(*task4)(void), uint32_t priority4,
    void(*task5)(void), uint32_t priority5,
    void(*task6)(void), uint32_t priority6,
    void(*task7)(void), uint32_t priority7)
{
    __disable_irq();

    /* Build circular linked list: 0 → 1 → 2 → ... → 7 → 0                  */
    tcbs[0].nextPt = &tcbs[1];
    tcbs[1].nextPt = &tcbs[2];
    tcbs[2].nextPt = &tcbs[3];
    tcbs[3].nextPt = &tcbs[4];
    tcbs[4].nextPt = &tcbs[5];
    tcbs[5].nextPt = &tcbs[6];
    tcbs[6].nextPt = &tcbs[7];
    tcbs[7].nextPt = &tcbs[0]; /* Wrap-around closes the ring                  */

    /* Initialise each thread's stack and write its entry-point into the PC
       slot ([STACKSIZE-2]) of the fake exception frame.                       */
    osKernelStackInit(0); TCB_STACK[0][STACKSIZE - 2] = (int32_t)(task0);
    osKernelStackInit(1); TCB_STACK[1][STACKSIZE - 2] = (int32_t)(task1);
    osKernelStackInit(2); TCB_STACK[2][STACKSIZE - 2] = (int32_t)(task2);
    osKernelStackInit(3); TCB_STACK[3][STACKSIZE - 2] = (int32_t)(task3);
    osKernelStackInit(4); TCB_STACK[4][STACKSIZE - 2] = (int32_t)(task4);
    osKernelStackInit(5); TCB_STACK[5][STACKSIZE - 2] = (int32_t)(task5);
    osKernelStackInit(6); TCB_STACK[6][STACKSIZE - 2] = (int32_t)(task6);
    osKernelStackInit(7); TCB_STACK[7][STACKSIZE - 2] = (int32_t)(task7);

    /* First thread to run — osSchedularLaunch will restore its context       */
    currentPt = &tcbs[0];

    /* Clear synchronisation state for all threads                            */
    for (int i = 0; i < NUM_OF_THREADS; i++) {
        tcbs[i].blocked   = 0;
        tcbs[i].sleepTime = 0;
    }

    /* Assign priorities                                                       */
    tcbs[0].priority = priority0;
    tcbs[1].priority = priority1;
    tcbs[2].priority = priority2;
    tcbs[3].priority = priority3;
    tcbs[4].priority = priority4;
    tcbs[5].priority = priority5;
    tcbs[6].priority = priority6;
    tcbs[7].priority = priority7;

    __enable_irq();
    return 1;
}

/* ========================== Kernel Init ================================== */
/*
 * osKernelInit
 *
 * Called before osKernelLaunch.  Sets up the millisecond prescaler and starts
 * TIM4 at 1 kHz so sleep/periodic machinery is ready before the first thread
 * runs.
 */
void osKernelInit(void) {
    MILLIS_PRESCALER = (BUS_FREQ / 1000); /* = 16000 reload counts per ms      */

    /* Register periodic_event_execute as a 1 kHz TIM4 callback, priority 6.
       This handles sleep countdown and soft timers.                           */
    osPeriodicTask_Init(periodic_event_execute, 1000, 6);
}

/* ========================== Kernel Launch ================================ */
/*
 * osKernelLaunch
 *
 * Final step before the RTOS starts.
 *   1. Clear and load SysTick for quanta-ms ticks.
 *   2. Set PendSV to the LOWEST priority (0xE0 in bits[31:24] of SYSPRI3)
 *      so context switches never preempt real ISRs.
 *   3. Enable SysTick with the core clock and the interrupt enable bit.
 *   4. Call osSchedularLaunch (assembly) — never returns.
 */
void osKernelLaunch(uint32_t quanta) {
    SysTick->CTRL = 0;                          /* Disable SysTick while configuring */
    SysTick->VAL  = 0;                          /* Clear current value               */
    SysTick->LOAD = (quanta * MILLIS_PRESCALER) - 1; /* Reload for quanta ms         */

    /* Bits[31:24] of SYSPRI3 = PendSV priority; 0xE0 = priority 7 (lowest)   */
    SYSPRI3 = (SYSPRI3 & 0x00FFFFFF) | 0xE0000000;

    /* CTRL bits: [2] CLKSOURCE=processor clock, [1] TICKINT=enable, [0] ENABLE */
    SysTick->CTRL = 0x00000007;

    osSchedularLaunch(); /* Jump into the first thread — never returns          */
}

/* ========================= Thread Yield ================================== */
/*
 * osThreadYield
 *
 * Allows a thread to voluntarily preempt itself before its quanta expires.
 * Resetting SysTick->VAL prevents a double-fire: without this, a very quick
 * yield followed by a natural tick expiry could cause two rapid switches.
 *
 * Writing 0x04000000 to INTCTRL sets bit 26 (SYSTICKSET) → pends SysTick →
 * which then pends PendSV → scheduler runs.
 *
 * NOTE: This actually writes SYSTICKSET (bit 26), not PENDSVSET (bit 28).
 * SysTick_Handler immediately re-pends PendSV, so the net effect is the
 * same as a direct PENDSVSET but goes through one extra ISR hop.
 */
void osThreadYield(void) {
    SysTick->VAL = 0;            /* Reset SysTick counter to avoid double-tick */
    INTCTRL = 0x04000000;        /* Set SYSTICKSET bit → pends SysTick ISR     */
}

/* ========================= SysTick Handler =============================== */
/*
 * SysTick_Handler
 *
 * Fires every `quanta` ms.  Sole job: write PENDSVSET to defer the actual
 * context switch to PendSV (lowest priority ISR), which runs after any
 * currently-executing higher-priority ISR completes.
 *
 * 0x10000000 = bit 28 of INTCTRL = PENDSVSET.
 */
void SysTick_Handler(void) {
    INTCTRL = 0x10000000; /* Pend PendSV — context switch will happen on exit  */
}

/* ========================= Priority Scheduler ============================ */
/*
 * osPriorityScheduler
 *
 * Called from PendSV_Handler (in assembly).  Walks the entire circular TCB
 * list once, finding the ready thread with the lowest priority number.
 * Updates currentPt to point to the winner.
 *
 * A thread is "ready" when:
 *   - blocked   == 0  (not waiting on a semaphore)
 *   - sleepTime == 0  (not sleeping)
 *
 * If all threads are blocked/sleeping this loops and picks the current
 * thread again (degenerate case — the idle thread should prevent this).
 *
 * Complexity: O(N) per scheduling decision.
 */
void osPriorityScheduler(void) {
    tcbType *__currentPt      = currentPt;
    tcbType *nextThreadToRun  = __currentPt;
    uint8_t  highestPriorityFound = 255; /* Start worst-case; any real priority beats this */

    do {
        __currentPt = __currentPt->nextPt; /* Advance to next TCB in ring          */

        if ((__currentPt->priority < highestPriorityFound) &&
            (__currentPt->blocked  == 0) &&
            (__currentPt->sleepTime == 0))
        {
            nextThreadToRun       = __currentPt;
            highestPriorityFound  = __currentPt->priority;
        }
    } while (__currentPt != currentPt); /* Stop when we've lapped the list        */

    currentPt = nextThreadToRun; /* PendSV_Handler restores this thread's context */
}

/* ====================== TIM4 Periodic Task Init ========================== */
/*
 * osPeriodicTask_Init
 *
 * Configures TIM4 as a general-purpose timer with:
 *   PSC = 15  → fTIM4 = 16 MHz / 16 = 1 MHz
 *   ARR = (1 000 000 / freq) - 1
 *   e.g. freq=1000 → ARR=999 → 1 MHz / 1000 = 1 kHz interrupts
 *
 * The single function pointer PeriodicTask is stored globally; this design
 * only supports one TIM4 callback (periodic_event_execute, which in turn
 * dispatches the soft-timer table).
 */
void(*PeriodicTask)(void); /* Pointer to the single TIM4 callback              */

void osPeriodicTask_Init(void(*task)(void), uint32_t freq, uint8_t priority) {
    __disable_irq();

    PeriodicTask = task;

    RCC->APB1ENR |= 0x04;          /* Enable TIM4 clock (bit 2 of APB1ENR)     */
    TIM4->PSC = 16 - 1;            /* Prescaler: 16 MHz / 16 = 1 MHz           */
    TIM4->ARR = (1000000 / freq) - 1; /* Auto-reload for desired frequency      */
    TIM4->CR1 = 1;                 /* Enable counter                            */

    TIM4->DIER |= 1;               /* UIE: enable update interrupt              */
    NVIC_SetPriority(TIM4_IRQn, priority); /* priority 6: above PendSV(7), below ISRs */
    NVIC_EnableIRQ(TIM4_IRQn);

    __enable_irq();
}

/*
 * TIM4_IRQHandler
 *
 * Clear the update interrupt flag (writing 0 to SR) then invoke the
 * registered callback (periodic_event_execute).
 * Failure to clear SR would cause immediate re-entry.
 */
void TIM4_IRQHandler(void) {
    TIM4->SR = 0;         /* Clear all TIM4 status/interrupt flags              */
    (*PeriodicTask)();    /* Dispatch → periodic_event_execute()                */
}

/* ========================= Semaphore Primitives ========================== */
/*
 * Counting semaphore implementation.
 * The semaphore value is a plain int32_t; the address is passed by pointer
 * so the same functions work for any in-scope variable.
 */

void osSemaphoreInit(int32_t *semaphore, int32_t value) {
    *semaphore = value;
}

/*
 * osSignalSet  (V / post)
 *
 * Atomically increments the semaphore.  Disabling interrupts here rather
 * than using LDREX/STREX is adequate on single-core Cortex-M4 but would
 * need rethinking on multicore.
 */
void osSignalSet(int32_t *semaphore) {
    __disable_irq();
    *semaphore += 1;
    __enable_irq();
}

/*
 * osSignalWait  (P / pend) — BUSY-WAIT variant
 *
 * Spins with interrupts briefly re-enabled between checks so that the
 * TIM4 sleep-tick and the producer thread (which calls osSignalSet) can
 * actually run.  This is a spin-wait, not a true block — the thread
 * continues to consume scheduler quanta while waiting.
 *
 * Appropriate only for short waits.  For long waits, use the cooperative
 * variant below, or block via the `blocked` TCB field.
 */
void osSignalWait(int32_t *semaphore) {
    __disable_irq();
    while (*semaphore <= 0) {
        __enable_irq();   /* Allow other ISRs to run and potentially signal    */
        __disable_irq();  /* Re-disable before re-checking                     */
    }
    *semaphore -= 1;
    __enable_irq();
}

/*
 * osSignalCooperativeWait  (P / pend) — YIELD variant
 *
 * Like osSignalWait but yields the CPU between checks instead of spinning.
 * The thread gives up its remaining quanta each loop iteration, allowing
 * lower-priority threads (including the signalling thread) to run.
 * Better CPU utilisation for longer waits.
 */
void osSignalCooperativeWait(int32_t *semaphore) {
    __disable_irq();
    while (*semaphore <= 0) {
        __enable_irq();
        osThreadYield();  /* Voluntarily context-switch; resume after a quanta */
        __disable_irq();
    }
    *semaphore -= 1;
    __enable_irq();
}

/* ========================= Thread Sleep ================================== */
/*
 * osThreadSleep
 *
 * Sets the calling thread's sleepTime counter and immediately yields.
 * The TIM4 ISR (via periodic_event_execute) decrements sleepTime every ms.
 * Once sleepTime == 0 the thread becomes schedulable again.
 *
 * The __disable_irq guard prevents a TIM4 IRQ from decrementing sleepTime
 * before osThreadYield has taken effect (race: set then instantly decremented
 * to 0 before the switch, waking immediately).
 */
void osThreadSleep(uint32_t sleep_time) {
    __disable_irq();
    currentPt->sleepTime = sleep_time; /* Set countdown (decremented by TIM4)  */
    __enable_irq();
    osThreadYield();                   /* Immediately surrender the CPU         */
}

/* ========================= Mailbox (single-slot IPC) ===================== */
/*
 * A one-element mailbox: a single uint32_t with a has-data flag.
 * Used for single-producer / single-consumer inter-thread messages.
 * If the box is full when a send is attempted, the data is silently dropped.
 *
 * MB_Sem tracks whether data is available; the receiver blocks on it via
 * osSignalCooperativeWait (yielding, not spinning).
 */
static uint8_t  MB_hasdata; /* 1 if a value is waiting in MB_data             */
static uint32_t MB_data;    /* The stored message                              */
static int32_t  MB_Sem;     /* Semaphore: 0 = empty, 1 = has data             */

void osMailBoxInit(void) {
    MB_hasdata = 0;
    MB_data    = 0;
    osSemaphoreInit(&MB_Sem, 0); /* Start empty                                */
}

/*
 * osMailBoxSend
 *
 * If the mailbox already contains data, the new value is dropped (no queue).
 * Otherwise, stores the value and signals the semaphore to unblock a waiting
 * receiver.
 */
void osMailBoxSend(uint32_t data) {
    __disable_irq();
    if (MB_hasdata) {
        __enable_irq();
        return; /* Drop: mailbox full (only holds one item)                    */
    }
    MB_data    = data;
    MB_hasdata = 1;
    __enable_irq();
    osSignalSet(&MB_Sem); /* Wake a waiting receiver                            */
}

/*
 * osMailBoxReceive
 *
 * Blocks (cooperatively) until a value is available, then returns it and
 * clears the mailbox.
 */
uint32_t osMailBoxReceive(void) {
    osSignalCooperativeWait(&MB_Sem); /* Block until sender signals            */
    uint32_t data;
    __disable_irq();
    data       = MB_data;
    MB_hasdata = 0;       /* Mark mailbox as empty so next send can proceed    */
    __enable_irq();
    return data;
}

/* ========================= FIFO Queue (IPC) ============================== */
/*
 * A fixed-size circular FIFO using index-based wrap-around.
 * Capacity: FIFO_SIZE (15) entries.
 * current_fifo_size acts as both a semaphore (blocks Get when empty)
 * and a fill counter.
 *
 * NOTE: osFifoGet uses osSignalWait (busy-wait) — see osSignalWait for
 * caveats.  For a higher-throughput design, replace with the cooperative
 * variant.
 */
#define FIFO_SIZE 15

uint32_t PutI;                   /* Write index                                */
uint32_t GetI;                   /* Read index                                 */
uint32_t OS_Fifo[FIFO_SIZE];     /* Circular buffer                            */
int32_t  current_fifo_size;      /* Fill count — also used as semaphore        */
uint32_t lost_data;              /* Count of dropped puts (overflow)           */

void osFifoInit(void) {
    PutI = 0;
    GetI = 0;
    osSemaphoreInit(&current_fifo_size, 0); /* Empty                          */
    lost_data = 0;
}

/*
 * osFifoPut
 *
 * Returns -1 and increments lost_data if full; otherwise writes and
 * signals the semaphore so a blocked consumer wakes.
 * Should be called from a non-critical-section context or with interrupts
 * disabled by the caller if the producer is an ISR.
 */
int8_t osFifoPut(uint32_t data) {
    if (current_fifo_size == FIFO_SIZE) {
        lost_data++;
        return -1; /* FIFO overflow — data lost                                */
    }
    OS_Fifo[PutI] = data;
    PutI = (PutI + 1) % FIFO_SIZE; /* Wrap-around write index                 */
    osSignalSet(&current_fifo_size);
    return 1;
}

/*
 * osFifoGet
 *
 * Blocks (busy-spins via osSignalWait) until an element is available,
 * then atomically reads and returns it.
 */
uint32_t osFifoGet(void) {
    uint32_t data;
    osSignalWait(&current_fifo_size);   /* Block until FIFO is non-empty       */
    __disable_irq();
    data = OS_Fifo[GetI];
    GetI = (GetI + 1) % FIFO_SIZE;     /* Wrap-around read index              */
    __enable_irq();
    return data;
}

/* ====================== Edge Trigger (EXTI) ============================== */
/*
 * Enables PA0 as a falling-edge interrupt source and ties it to a
 * caller-supplied semaphore.  The EXTI0 ISR (not shown here — expected in
 * BSP or user code) should call osSignalSet(edgeSemaphore) to unblock
 * a thread waiting with osSignalWait(edgeSemaphore).
 */
int32_t *edgeSemaphore; /* Pointer to the caller's semaphore variable          */

void osEdgeTriggerInit(int32_t *semaphore) {
    edgeSemaphore = semaphore;
    BSP_EdgeTrigger_Init(); /* Configures PA0 EXTI falling-edge in BSP          */
}
