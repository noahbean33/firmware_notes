/* =============================================================================
 * osKernel.h  —  Public API for the STM32F4 RTOS kernel
 *
 * Consumers include:
 *   main.c           — calls Init / AddThreads / Launch
 *   user task files  — call Yield / Sleep / semaphore primitives
 * ============================================================================= */

#ifndef __OS_KERNEL_H__
#define __OS_KERNEL_H__

#include <stdint.h>
#include "stm32f4xx.h"          /* CMSIS Cortex-M4 core + STM32F4 peripheral defs */
#include "STM32F4_RTOS_BSP.h"  /* Board support: LEDs, buttons, ADC, probes         */

/* ---------------------------------------------------------------------------
 * Core kernel lifecycle
 * --------------------------------------------------------------------------- */

/**
 * osKernelInit
 *
 * Must be called before osKernelLaunch.
 * - Computes MILLIS_PRESCALER from the 16 MHz bus clock so that quanta
 *   values in osKernelLaunch are expressed in milliseconds.
 * - Starts TIM4 as a 1 kHz periodic timer and registers periodic_event_execute
 *   as its ISR (priority 6).  That ISR ticks sleep countdowns and fires any
 *   registered periodic tasks.
 */
void osKernelInit(void);

/**
 * osKernelLaunch
 *
 * Configures SysTick to fire every `quanta` milliseconds, sets PendSV to
 * the lowest interrupt priority (7), enables SysTick, then calls the assembly
 * stub osSchedularLaunch to cold-start the first thread.  Never returns.
 *
 * @param quanta  Scheduler time-slice in milliseconds (e.g. 10 ms → 100 Hz).
 */
void osKernelLaunch(uint32_t quanta);

/**
 * osKernelAddThreads
 *
 * Registers 8 threads with the kernel before launch.
 * Each (taskN, priorityN) pair specifies:
 *   taskN      — pointer to a void(void) function that loops forever
 *   priorityN  — lower number = higher priority (0 highest, 255 lowest)
 *
 * Internally this:
 *   1. Chains the 8 TCBs into a circular linked list.
 *   2. Calls osKernelStackInit for each thread (builds a fake exception frame).
 *   3. Writes the task function pointer into the PC slot of each fake frame.
 *   4. Zeroes sleepTime and blocked for all threads.
 *   5. Sets currentPt = &tcbs[0].
 *
 * @return 1 on success (no failure path currently implemented).
 */
uint8_t osKernelAddThreads(
    void(*task0)(void), uint32_t priority0,
    void(*task1)(void), uint32_t priority1,
    void(*task2)(void), uint32_t priority2,
    void(*task3)(void), uint32_t priority3,
    void(*task4)(void), uint32_t priority4,
    void(*task5)(void), uint32_t priority5,
    void(*task6)(void), uint32_t priority6,
    void(*task7)(void), uint32_t priority7
);

/* ---------------------------------------------------------------------------
 * Thread control
 * --------------------------------------------------------------------------- */

/**
 * osThreadYield
 *
 * Voluntarily surrenders the remainder of the current time-slice.
 * Resets SysTick counter to 0 and writes PENDSVSET, which triggers
 * PendSV_Handler immediately (after any pending higher-priority ISRs).
 */
void osThreadYield(void);

/* ---------------------------------------------------------------------------
 * Periodic task registration (soft timers, executed from TIM4 ISR)
 * --------------------------------------------------------------------------- */

/**
 * osKernelAddPeriod_Thread
 *
 * Registers a lightweight periodic callback executed from the TIM4 ISR
 * context (not as a full RTOS thread — no separate stack).
 * period is in TIM4 ticks (1 tick = 1 ms at 1 kHz).
 *
 * Used internally to register periodic_event_execute.
 * Up to NUM_PERIODIC_TASK (5) callbacks may be registered.
 *
 * @return 1 on success, 0 if the table is full or period == 0.
 */
uint8_t osKernelAddPeriod_Thread(void(*task)(void), uint32_t period);

/* Placeholder declarations referenced in header but not used in main.c */
void periodicTask1(void);
void periodicTask2(void);

/* ---------------------------------------------------------------------------
 * Synchronization primitives  (counting semaphore)
 * --------------------------------------------------------------------------- */

/**
 * osSemaphoreInit  —  Set semaphore to initial value.
 * osSignalSet      —  V() / post: atomically increment the semaphore.
 * osSignalWait     —  P() / pend: spin-wait (busy) until semaphore > 0,
 *                     then decrement.  Interrupts are briefly re-enabled
 *                     between iterations so the system is not fully locked.
 */
void osSemaphoreInit(int32_t *semaphore, int32_t value);
void osSignalSet(int32_t *semaphore);
void osSignalWait(int32_t *semaphore);

/* ---------------------------------------------------------------------------
 * External interrupt / edge trigger
 * --------------------------------------------------------------------------- */

/**
 * osEdgeTriggerInit
 *
 * Configures PA0 for falling-edge EXTI and stores `semaphore` so that
 * EXTI0_IRQHandler can signal it on each edge.
 * The waiting thread calls osSignalWait(semaphore) to block until an edge arrives.
 */
void osEdgeTriggerInit(int32_t *semaphore);

#endif /* __OS_KERNEL_H__ */
