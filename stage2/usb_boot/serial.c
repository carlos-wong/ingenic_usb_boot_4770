#include "jz4770.h"
#include <stdarg.h>

volatile u32 UART_BASE;// = 0xB0032000;	//FIXME: use uart2 for jz4770
#define CONFIG_BAUDRATE 57600
#define CFG_EXTAL		12000000	/* EXTAL freq must=12 MHz !! */

void serial_setbrg (void)
{
	volatile u8 *uart_lcr = (volatile u8 *)(UART_BASE + OFF_LCR);
	volatile u8 *uart_dlhr = (volatile u8 *)(UART_BASE + OFF_DLHR);
	volatile u8 *uart_dllr = (volatile u8 *)(UART_BASE + OFF_DLLR);
	u32 baud_div, tmp;

	baud_div = CFG_EXTAL / 16 / CONFIG_BAUDRATE;
	tmp = *uart_lcr;
	tmp |= UART_LCR_DLAB;
	*uart_lcr = tmp;

	*uart_dlhr = (baud_div >> 8) & 0xff;
	*uart_dllr = baud_div & 0xff;

	tmp &= ~UART_LCR_DLAB;
	*uart_lcr = tmp;
}

void serial_putc (const char c)
{
	volatile u8 *uart_lsr = (volatile u8 *)(UART_BASE + OFF_LSR);
	volatile u8 *uart_tdr = (volatile u8 *)(UART_BASE + OFF_TDR);

	if (c == '\n') serial_putc ('\r');

	/* Wait for fifo to shift out some bytes */
	while ( !((*uart_lsr & (UART_LSR_TDRQ | UART_LSR_TEMT)) == 0x60) );

	*uart_tdr = (u8)c;
}

void serial_puts (const char *s)
{
	while (*s) {
		serial_putc (*s++);
	}
}

void serial_init(void)
{
	volatile u8 *uart_fcr = (volatile u8 *)(UART_BASE + OFF_FCR);
	volatile u8 *uart_lcr = (volatile u8 *)(UART_BASE + OFF_LCR);
	volatile u8 *uart_ier = (volatile u8 *)(UART_BASE + OFF_IER);
	volatile u8 *uart_sircr = (volatile u8 *)(UART_BASE + OFF_SIRCR);

	/* Disable port interrupts while changing hardware */
	*uart_ier = 0;

	/* Disable UART unit function */
	*uart_fcr = ~UART_FCR_UUE;

	/* Set both receiver and transmitter in UART mode (not SIR) */
	*uart_sircr = ~(SIRCR_RSIRE | SIRCR_TSIRE);

	/* Set databits, stopbits and parity. (8-bit data, 1 stopbit, no parity) */
	*uart_lcr = UART_LCR_WLEN_8 | UART_LCR_STOP_1;

	/* Set baud rate */
	serial_setbrg();

	/* Enable UART unit, enable and clear FIFO */
	*uart_fcr = UART_FCR_UUE | UART_FCR_FE | UART_FCR_TFLS | UART_FCR_RFLS;
}

void serial_put_hex(unsigned int  d)
{
	unsigned char c[12];
	char i;
	for(i = 0; i < 8;i++)
	{
		c[i] = (d >> ((7 - i) * 4)) & 0xf;
		if(c[i] < 10)
			c[i] += 0x30;
		else
			c[i] += (0x41 - 10);
	}
	c[8] = '\n';
	c[9] = 0;
	serial_puts(c);

}

static void serial_put_hex_no_ln(unsigned int d) {
        int i = 0;
        unsigned char c;

        serial_putc('0');
        serial_putc('x');
        for (i = 7; i >= 0; i--) {
                c = (d >> (i * 4)) & 0xf;
                if (c < 10)
                        c += 0x30;
                else
                        c += (0x41 - 10);
                serial_putc(c);
        }
}

static void serial_put_dec(unsigned int d) {
        unsigned int div[10] = {
                1000000000,
                100000000,
                10000000,
                1000000,
                100000,
                10000,
                1000,
                100,
                10,
                1
        };
        unsigned int dig;
        int print = 0;
        int i = 0;

        for (i = 0; i <= 9; i++) {
                dig = d / div[i];
		if (dig) {
			serial_putc( (dig & 0xff) + '0');
			print = 1;
		} else if (print)
			serial_putc('0');

                d -= dig * div[i];
        }

	if (!print)
		serial_putc('0');
}

/* a simplified printf implementation, support %s, %x(default to 0x%08x), %d, %c */
void serial_printf(char *fmt, ...)
{
        va_list ap;
        char curr_c, next_c;
        unsigned int d;
        char c, *s;

        va_start(ap, fmt);
        while (*fmt) {
                curr_c = *fmt;
                next_c = *(fmt + 1);

                if (curr_c == '%') {
                        switch (next_c) {
                        case 's':
                                s = va_arg(ap, char *);
                                serial_puts(s);
                                fmt++;
                                break;
                        case 'x':
                                d = va_arg(ap, unsigned int);
				serial_put_hex_no_ln(d);
                                fmt++;
                                break;
			case 'd':
                                d = va_arg(ap, unsigned int);
				serial_put_dec(d);
                                fmt++;
                                break;
                        case 'c':
                                c = (char)va_arg(ap, int);
                                serial_putc(c);
                                fmt++;
                                break;
                        default:
                                serial_putc(curr_c);
                                break;
                        }
                } else {
                        serial_putc(curr_c);
                }

                fmt++;
        }
        va_end(ap);
}
