/* =============================================================================
 * STM32F4_RTOS_BSP.c  —  Board Support Package for STM32F4-Discovery
 *
 * All peripheral access is via direct register writes (no HAL) to keep latency
 * predictable and dependencies minimal — appropriate for an RTOS BSP.
 * ============================================================================= */

#include "STM32F4_RTOS_BSP.h"
#include "stm32f4xx.h"   /* CMSIS device header: RCC, GPIO, TIM, ADC, NVIC defs */

/* =========================================================================
 * LED pin definitions  (STM32F4-Discovery onboard LEDs, all on GPIOD)
 * =========================================================================
 *   PD12 = green   PD13 = orange   PD14 = red   PD15 = blue
 *
 * MODER uses 2 bits per pin; "output" = 0b01.
 * The shift values below place 0b01 at the correct bit-pair position.
 */
#define BSP_LED_red_BIT    (1U << 28)  /* MODER bits [29:28] for PD14           */
#define BSP_LED_green_BIT  (1U << 24)  /* MODER bits [25:24] for PD12           */
#define BSP_LED_orange_BIT (1U << 26)  /* MODER bits [27:26] for PD13           */
#define BSP_LED_blue_BIT   (1U << 30)  /* MODER bits [31:30] for PD15           */

#define LED_PORT        GPIOD
#define BSP_LED_red     (1U << 14)     /* ODR bit for red   (PD14)              */
#define BSP_LED_green   (1U << 12)     /* ODR bit for green (PD12)              */
#define BSP_LED_blue    (1U << 15)     /* ODR bit for blue  (PD15)              */
#define BSP_LED_orange  (1U << 13)     /* ODR bit for orange(PD13)              */

/* RCC AHB1ENR clock-enable bits */
#define GPIOD_CLOCK  (1 << 3)          /* bit 3 = GPIODEN                       */
#define GPIOA_CLOCK  (1 << 0)          /* bit 0 = GPIOAEN                       */
#define GPIOC_CLOCK  (1 << 2)          /* bit 2 = GPIOCEN                       */

/* =========================================================================
 * Button (PA0, active-high, onboard blue user button on F4-Discovery)
 * ========================================================================= */
#define BSP_Button_PORT  GPIOA

/* =========================================================================
 * Logic-analyser probe pins (GPIOC output toggles)
 * PC0=CH0, PC1=CH1, PC2=CH2, PC4=CH3
 * MODER bits are at 2*pin offset; 0b01 = general-purpose output.
 * ========================================================================= */
#define BSP_Probe0_BIT  (1U << 0)   /* MODER bit for PC0 (bit-pair [1:0])      */
#define BSP_Probe1_BIT  (1U << 2)   /* MODER bit for PC1 (bit-pair [3:2])      */
#define BSP_Probe2_BIT  (1U << 4)   /* MODER bit for PC2 (bit-pair [5:4])      */
#define BSP_Probe3_BIT  (1U << 8)   /* MODER bit for PC4 (bit-pair [9:8])      */

#define BSP_Probe_PORT  GPIOC
#define CH0  (1U << 0)              /* ODR bit for PC0                          */
#define CH1  (1U << 1)              /* ODR bit for PC1                          */
#define CH2  (1U << 2)              /* ODR bit for PC2                          */
#define CH3  (1U << 4)              /* ODR bit for PC4                          */

/* =========================================================================
 * BSP_EdgeTrigger_Init
 *
 * Configures PA0 as a falling-edge external interrupt (EXTI0).
 * Used by the RTOS osEdgeTriggerInit to give threads a way to block on
 * a hardware event (e.g. button press or external signal).
 *
 * Steps:
 *   1. Enable GPIOA and SYSCFG clocks.
 *   2. Set PA0 MODER to input (clear bits [1:0]).
 *   3. Map EXTI0 to port A via SYSCFG->EXTICR[0].
 *   4. Unmask EXTI0 in IMR (interrupt mask register).
 *   5. Select falling edge in FTSR (falling trigger selection register).
 *   6. Enable EXTI0 in NVIC.
 * ========================================================================= */
void BSP_EdgeTrigger_Init(void) {
    __disable_irq();

    RCC->AHB1ENR  |= 4;            /* Enable GPIOA clock (bit 2)               */
    RCC->APB2ENR  |= 0x4000;       /* Enable SYSCFG clock (bit 14)             */

    GPIOA->MODER  &= ~0x03;        /* PA0 = input (clear MODER[1:0])           */

    SYSCFG->EXTICR[0] &= ~0x000F;  /* Clear EXTI0 source selection             */
    /* Default (0x0000) maps EXTI0 to port A — no explicit set needed           */

    EXTI->IMR  |= 0x0001;          /* Unmask EXTI line 0 (allow interrupt)     */
    EXTI->FTSR |= 0x0001;          /* Trigger on falling edge                  */

    NVIC_EnableIRQ(EXTI0_IRQn);

    __enable_irq();
}

/* =========================================================================
 * BSP_TIM2_Init
 *
 * Sets up TIM2 as a 1 kHz general-purpose timebase.
 *   PSC = 15999 → fTIM2 = 16 MHz / 16000 = 1 kHz
 *   ARR = 999   → overflow at 1 kHz / 1000 = 1 Hz  (1-second period)
 *
 * UIE (update interrupt enable) is set; the TIM2 IRQ is enabled in NVIC.
 * The application must define TIM2_IRQHandler to act on it.
 * ========================================================================= */
void BSP_TIM2_Init(void) {
    RCC->APB1ENR |= 1;             /* Enable TIM2 clock (bit 0)                */
    TIM2->PSC = 16000 - 1;         /* 16 MHz / 16000 = 1 kHz                   */
    TIM2->ARR = 1000 - 1;          /* 1 kHz / 1000 = 1 Hz overflow             */
    TIM2->CR1 = 1;                 /* Enable counter                            */

    TIM2->DIER |= 1;               /* UIE: enable update (overflow) interrupt  */
    NVIC_EnableIRQ(TIM2_IRQn);
}

/* =========================================================================
 * BSP_Probe_Init / BSP_Probe_CHx
 *
 * Four GPIO output pins wired to a logic analyser to observe RTOS timing.
 * Each BSP_Probe_CHx toggles its pin — calling it twice produces one pulse
 * that is visible on the analyser.
 * ========================================================================= */
void BSP_Probe_Init(void) {
    RCC->AHB1ENR |= GPIOC_CLOCK;  /* Enable GPIOC clock                       */
    /* Set PC0, PC1, PC2, PC4 as general-purpose outputs (MODER = 0b01)        */
    BSP_Probe_PORT->MODER |= BSP_Probe0_BIT | BSP_Probe1_BIT |
                              BSP_Probe2_BIT | BSP_Probe3_BIT;
}

void BSP_Probe_CH0(void) { BSP_Probe_PORT->ODR ^= CH0; } /* Toggle PC0        */
void BSP_Probe_CH1(void) { BSP_Probe_PORT->ODR ^= CH1; } /* Toggle PC1        */
void BSP_Probe_CH2(void) { BSP_Probe_PORT->ODR ^= CH2; } /* Toggle PC2        */
void BSP_Probe_CH3(void) { BSP_Probe_PORT->ODR ^= CH3; } /* Toggle PC4        */

/* =========================================================================
 * BSP_Button_Init / BSP_Button_Read
 *
 * PA0 is the blue user button on the STM32F4-Discovery (active-high with
 * internal pull-down — pressing it drives PA0 to 3.3 V).
 * MODER[1:0] = 00 = input mode (default after reset, but explicit here).
 * ========================================================================= */
void BSP_Button_Init(void) {
    RCC->AHB1ENR |= GPIOA_CLOCK;  /* Enable GPIOA clock                       */
    BSP_Button_PORT->MODER &= ~0x00000003; /* PA0 = input                      */
}

uint32_t BSP_Button_Read(void) {
    return BSP_Button_PORT->IDR & 0x01; /* Bit 0 of IDR = PA0 state            */
}

/* =========================================================================
 * BSP_Delay_Millisecond
 *
 * Blocking (polled) millisecond delay using TIM3.
 *   PSC = 159 → fTIM3 = 16 MHz / 160 = 100 kHz
 *   ARR =  99 → overflow at 100 kHz / 100 = 1 kHz (every 1 ms)
 *
 * Each iteration of the loop polls the UIF flag (bit 0 of TIM3->SR) and
 * clears it, so the total delay is `delay` milliseconds.
 *
 * NOTE: This is a polling delay — it burns CPU.  It is suitable for
 * initialisation code (before the scheduler starts) but should NOT be
 * called from RTOS tasks (use osThreadSleep instead).
 * ========================================================================= */
void BSP_Delay_Millisecond(uint32_t delay) {
    RCC->APB1ENR |= 0x02;  /* Enable TIM3 clock (bit 1)                       */
    TIM3->PSC = 160 - 1;   /* 16 MHz / 160 = 100 kHz                          */
    TIM3->ARR = 100 - 1;   /* 100 kHz / 100 = 1 kHz (1 ms per overflow)       */
    TIM3->CNT = 0;          /* Reset counter                                   */
    TIM3->CR1 = 1;          /* Enable counter                                  */

    for (int i = 0; i < delay; i++) {
        while (!(TIM3->SR & 1)) {}  /* Spin until UIF (update interrupt flag)  */
        TIM3->SR &= ~1;             /* Clear UIF (write 0 to clear)             */
    }
}

/* =========================================================================
 * BSP_ADC1_Init
 *
 * Configures ADC1 for single-channel, software-triggered 12-bit conversions
 * on PA1 (ADC1 channel 1).
 *   - PA1 MODER[3:2] = 11 (analog mode) to disable digital Schmitt trigger.
 *   - SQR3[4:0] = 1  → first conversion in sequence reads channel 1.
 *   - SQR1[23:20] = 0 → sequence length = 1 conversion.
 *   - CR2 bit 0 = ADON: enable ADC.
 * ========================================================================= */
void BSP_ADC1_Init(void) {
    /* GPIO: PA1 as analog input */
    RCC->AHB1ENR |= 1;         /* Enable GPIOA clock                          */
    GPIOA->MODER |= 0xC;       /* PA1 = analog (MODER[3:2] = 0b11)            */

    /* ADC1 */
    RCC->APB2ENR |= 0x00000100; /* Enable ADC1 clock (bit 8)                  */
    ADC1->CR2    = 0;            /* Reset control register                     */
    ADC1->SQR3   = 1;            /* Sequence register: first conv = channel 1  */
    ADC1->SQR1   = 0;            /* One conversion in sequence                 */
    ADC1->CR2   |= 1;            /* ADON: power up and enable ADC              */
}

/*
 * BSP_Sensor_Read
 *
 * Software-triggered single conversion on ADC1 channel 1.
 *   - CR2 bit 30 (SWSTART) launches conversion.
 *   - Poll SR bit 1 (EOC, end of conversion).
 *   - Return the 12-bit result from DR.
 */
uint32_t BSP_Sensor_Read(void) {
    ADC1->CR2 |= 0x40000000;    /* Set SWSTART: begin conversion               */
    while (!(ADC1->SR & 2)) {}  /* Wait for EOC (end of conversion flag)       */
    return ADC1->DR;             /* Read 12-bit result, clears EOC              */
}

/* =========================================================================
 * BSP_LED_Init
 *
 * Enables GPIOD clock and configures PD12-PD15 as push-pull outputs.
 * MODER uses 2 bits per pin; setting bit (2*pin) to 1 with bit (2*pin+1)
 * = 0 gives general-purpose output mode.
 * ========================================================================= */
void BSP_LED_Init(void) {
    __disable_irq();
    RCC->AHB1ENR |= GPIOD_CLOCK;  /* Enable GPIOD clock                       */
    GPIOD->MODER |= BSP_LED_red_BIT | BSP_LED_green_BIT |
                    BSP_LED_orange_BIT | BSP_LED_blue_BIT;
    __enable_irq();
}

/* =========================================================================
 * LED on/off/toggle
 *
 * ODR writes are not atomic on Cortex-M4 (read-modify-write via |= / &= / ^=).
 * For interrupt-safe use, prefer BSRR (bit-set/reset register), but the RTOS
 * context here protects against races via __disable_irq in callers.
 * ========================================================================= */
void BSP_LED_blueOn(void)     { GPIOD->ODR |=  BSP_LED_blue;   }
void BSP_LED_blueOff(void)    { GPIOD->ODR &= ~BSP_LED_blue;   }

void BSP_LED_orangeOn(void)   { GPIOD->ODR |=  BSP_LED_orange; }
void BSP_LED_orangeOff(void)  { GPIOD->ODR &= ~BSP_LED_orange; }

void BSP_LED_greenOn(void)    { GPIOD->ODR |=  BSP_LED_green;  }
void BSP_LED_greenOff(void)   { GPIOD->ODR &= ~BSP_LED_green;  }

void BSP_LED_redOn(void)      { GPIOD->ODR |=  BSP_LED_red;    }
void BSP_LED_redOff(void)     { GPIOD->ODR &= ~BSP_LED_red;    }

void BSP_LED_blueToggle(void)   { GPIOD->ODR ^= BSP_LED_blue;   }
void BSP_LED_greenToggle(void)  { GPIOD->ODR ^= BSP_LED_green;  }
void BSP_LED_redToggle(void)    { GPIOD->ODR ^= BSP_LED_red;    }
void BSP_LED_orangeToggle(void) { GPIOD->ODR ^= BSP_LED_orange; }
