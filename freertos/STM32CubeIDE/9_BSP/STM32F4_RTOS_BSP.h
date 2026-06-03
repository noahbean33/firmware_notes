#ifndef __STM32F4_RTOS_BSP_H__
#define __STM32F4_RTOS_BSP_H__ 

#include <stdint.h>
#include "stm32f4xx.h"                  // Device header



void BSP_LED_Init(void);

void BSP_LED_blueOn(void);
void BSP_LED_blueOff(void);
void BSP_LED_blueToggle(void);
void BSP_LED_redOn(void);
void BSP_LED_redOff(void);
void BSP_LED_redToggle(void);
void BSP_LED_orangeOn(void);
void BSP_LED_orangeOff(void);
void BSP_LED_orangeToggle(void);
void BSP_LED_greenOn(void);
void BSP_LED_greenOff(void);
void BSP_LED_greenToggle(void);

void BSP_ADC1_Init(void);
uint32_t BSP_ADC1_Read(void);

void BSP_Delay_MilliSecond(uint32_t delay);

void BSP_Button_Init(void);
uint32_t BSP_Button_Read(void);

void BSP_Probe_Init(void);
void BSP_Probe_CH0(void);
void BSP_Probe_CH1(void);
void BSP_Probe_CH2(void);
void BSP_Probe_CH3(void);

#endif