#include <stdint.h>
#include <uart_min.h>


#define SR_TXE					(1U<<7)


static void uart_write(int ch);

int __io_putchar(int ch)
{
	uart_write(ch);
	return ch;
}

static void uart_write(int ch)
{
	/*Make sure transmit data register is empty*/
	while(!(USART2->SR & SR_TXE)){}

	/*Write to transmit data register*/
	USART2->DR =(ch & 0xFF);
}
