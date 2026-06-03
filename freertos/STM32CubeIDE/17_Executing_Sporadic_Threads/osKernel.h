#ifndef __OS_KERNEL_H__
#define __OS_KERNEL_H__

#include <stdint.h>
#include "stm32f4xx.h"                  // Device header
#include "STM32F4_RTOS_BSP.h"

void osKernelLaunch(uint32_t quanta);
void osKernelInit(void);
uint8_t osKernelAddThreads(void(*task0)(void), void(*task1)(void),void(*task2)(void));
void osThreadYield(void);

uint8_t osKernelAddPeriod_Thread(void(*task)(void),uint32_t period);

void periodicTask1(void);
void periodicTask2(void);

void osSemaphoreInit(int32_t *semaphore, int32_t value);
void osSignalSet(int32_t *semaphore);
void osSignalWait(int32_t *semaphore);

void osEdgeTriggerInit(int32_t *semaphore);

#endif