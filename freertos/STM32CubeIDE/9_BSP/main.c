#include "STM32F4_RTOS_BSP.h"

uint32_t buttonState;
uint32_t sensorValue;
int main (void)
{
	BSP_ADC1_Init();
	BSP_LED_Init();
	BSP_Button_Init();
	BSP_Probe_Init();
	int i;
	while(1)
	{
//	  buttonState = BSP_Button_Read();
//		if(buttonState)
//		{
//			BSP_LED_blueOn();
//			BSP_LED_redOn();
//			BSP_LED_greenOn();
//			BSP_LED_orangeOn();
//			BSP_Delay_MilliSecond(1000);
//		
//			BSP_LED_blueOff();
//			BSP_LED_redOff();
//			BSP_LED_greenOff();
//			BSP_LED_orangeOff();
//			BSP_Delay_MilliSecond(1000);
//		}
	BSP_Probe_CH0();
	BSP_Probe_CH1();
	BSP_Probe_CH2();
	BSP_Probe_CH3();
	BSP_Delay_MilliSecond(1000);
		
//		sensorValue = BSP_ADC1_Read();
		
	}
}