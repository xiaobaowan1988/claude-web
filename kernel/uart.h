#pragma once

/* Initialise the UART (set baud-rate divisor, enable FIFOs). */
void uart_init(void);

/* Write a single character, blocking until the TX FIFO has room. */
void uart_putc(char c);

/* Write a NUL-terminated string. */
void uart_puts(const char *s);

/* Write a 32-bit value as 8 hex digits followed by '\n'. */
void uart_puthex(unsigned int v);
