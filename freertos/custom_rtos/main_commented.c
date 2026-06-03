/* =============================================================================
 * main.c  —  Application entry point and task definitions
 *
 * This is a minimal stress-test / demonstration of the RTOS kernel.
 * Eight tasks each do nothing but increment a counter in a tight loop.
 * Observing the counter values under a debugger reveals the relative CPU
 * time each task receives, which is proportional to how often the priority
 * scheduler selects it.
 * ============================================================================= */

#include "osKernel.h"
#include "STM32F4_RTOS_BSP.h"

/* Time-slice length for the preemptive scheduler.
   10 ms → SysTick fires at 100 Hz → up to 100 context-switches per second.  */
#define QUANTA  10

/* -------------------------------------------------------------------------
 * Per-task counters (volatile would be more correct here since they are
 * written by tasks and read by an external debugger, but the original omits
 * it — left as-is to match the source).
 * ------------------------------------------------------------------------- */
uint32_t count0, count1, count2, count3, count4, count5, count6, count7;

/* -------------------------------------------------------------------------
 * Task bodies
 *
 * Each task is an infinite loop.  The kernel never expects a task to return;
 * if one did, execution would fall off the end of the function and into
 * undefined memory.
 *
 * Priority mapping (lower number = higher priority):
 *   Task0 — priority 5
 *   Task1 — priority 1  ← highest in this set (tied with Task2, Task5, Task6)
 *   Task2 — priority 1
 *   Task3 — priority 2
 *   Task4 — priority 5
 *   Task5 — priority 1
 *   Task6 — priority 1
 *   Task7 — priority 3
 *
 * Because the scheduler is priority-based (not true round-robin within a
 * priority level — it restarts the scan from currentPt each time), tasks at
 * priority 1 will dominate CPU time over tasks at priority 2, 3, or 5.
 * -------------------------------------------------------------------------*/

void Task0(void) {
    while (1) {
        count0++; /* Counts scheduler quanta granted to Task0                  */
    }
}

void Task1(void) {
    while (1) {
        count1++;
    }
}

void Task2(void) {
    while (1) {
        count2++;
    }
}

void Task3(void) {
    while (1) {
        count3++;
    }
}

void Task4(void) {
    while (1) {
        count4++;
    }
}

void Task5(void) {
    while (1) {
        count5++;
    }
}

void Task6(void) {
    while (1) {
        count6++;
    }
}

void Task7(void) {
    while (1) {
        count7++;
    }
}

/* -------------------------------------------------------------------------
 * main
 *
 * Three-step RTOS startup sequence:
 *   1. BSP_LED_Init()          — configure GPIOD for the four onboard LEDs
 *                                (not used by tasks here, but available).
 *   2. osKernelInit()          — compute MILLIS_PRESCALER, start TIM4 at
 *                                1 kHz for sleep and periodic machinery.
 *   3. osKernelAddThreads()    — build the TCB linked list and fake stacks.
 *   4. osKernelLaunch(QUANTA)  — configure SysTick, set PendSV priority,
 *                                launch first thread.  Never returns.
 * ------------------------------------------------------------------------- */
int main(void) {

    BSP_LED_Init(); /* Enable GPIOD clock, configure PD12-PD15 as outputs     */

    osKernelInit(); /* Prescaler + TIM4 periodic tick setup                   */

    /* Register all 8 tasks with their priorities.
       The scheduler will always prefer the lowest-numbered priority.
       Tasks at the same priority level are picked by traversal order,
       effectively giving them a coarse round-robin within that tier.         */
    osKernelAddThreads(
        &Task0, 5,
        &Task1, 1,
        &Task2, 1,
        &Task3, 2,
        &Task4, 5,
        &Task5, 1,
        &Task6, 1,
        &Task7, 3
    );

    osKernelLaunch(QUANTA); /* Start the scheduler — does not return          */

    /* Unreachable.  If execution ever reaches here the MCU has a serious
       problem (stack corruption, bad linker script, etc.).                   */
}
