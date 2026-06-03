/* =============================================================================
 * STM32F4_RTOS_BSP.h  —  Board Support Package API for the STM32F4-Discovery
 *
 * Provides thin hardware abstractions for:
 *   - Four onboard LEDs (PD12 green, PD13 orange, PD14 red, PD15 blue)
 *   - User button (PA0)
 *   - ADC1 on PA1 (12-bit, single-channel, software-triggered)
 *   - TIM2 and TIM3 (delay utility, scope probes)
 *   - GPIO scope probes on PC0-PC4
 *   - PA0 EXTI0 falling-edge interrupt for the RTOS edge-trigger primitive
 * ============================================================================= */

#ifndef __STM32F4_RTOS_BSP_H
#define __STM32F4_RTOS_BSP_H
#include <stdint.h>

/* --- LED control (PD12=green, PD13=orange, PD14=red, PD15=blue) ---------- */
void BSP_LED_Init(void);           /* Enable GPIOD clock, set PD12-15 as output */

void BSP_LED_blueOn(void);
void BSP_LED_blueOff(void);
void BSP_LED_orangeOn(void);
void BSP_LED_orangeOff(void);
void BSP_LED_redOn(void);
void BSP_LED_redOff(void);
void BSP_LED_greenOn(void);
void BSP_LED_greenOff(void);
void BSP_LED_orangeToggle(void);
void BSP_LED_blueToggle(void);
void BSP_LED_greenToggle(void);
void BSP_LED_redToggle(void);

/* --- ADC (PA1, channel 1, 12-bit single-shot) ---------------------------- */
void     BSP_ADC1_Init(void);      /* Configure PA1 as analog, start ADC1       */
uint32_t BSP_Sensor_Read(void);    /* Trigger conversion, return 12-bit result  */

/* --- Blocking millisecond delay using TIM3 ------------------------------- */
void BSP_Delay_Millisecond(uint32_t delay);

/* --- User button (PA0, active-high) -------------------------------------- */
void     BSP_Button_Init(void);
uint32_t BSP_Button_Read(void);    /* Returns non-zero if pressed               */

/* --- Logic-analyser probe outputs (PC0, PC1, PC2, PC4) ------------------- */
void BSP_Probe_Init(void);
void BSP_Probe_CH0(void);          /* Toggle PC0                                */
void BSP_Probe_CH1(void);          /* Toggle PC1                                */
void BSP_Probe_CH2(void);          /* Toggle PC2                                */
void BSP_Probe_CH3(void);          /* Toggle PC4                                */

/* --- TIM2 time-base (1 kHz) for general timing --------------------------- */
void BSP_TIM2_Init(void);

/* --- EXTI0 edge trigger (PA0 falling edge) for RTOS synchronisation ------ */
void BSP_EdgeTrigger_Init(void);

#endif /* __STM32F4_RTOS_BSP_H */
