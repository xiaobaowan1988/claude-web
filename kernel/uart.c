/*
 * uart.c — NS16550A UART driver for QEMU virt machine
 *
 * QEMU's "virt" board maps a 16550-compatible UART at 0x10000000.
 * All registers are 1 byte wide and aligned to 1-byte offsets.
 *
 * Register map (DLAB=0):
 *   +0  RBR/THR  Receive Buffer / Transmit Holding Register
 *   +1  IER      Interrupt Enable Register
 *   +2  IIR/FCR  Interrupt Ident / FIFO Control Register
 *   +3  LCR      Line Control Register
 *   +4  MCR      Modem Control Register
 *   +5  LSR      Line Status Register
 *   +6  MSR      Modem Status Register
 *   +7  SCR      Scratch Register
 *
 * Register map (DLAB=1 — set LCR bit 7):
 *   +0  DLL      Divisor Latch Low
 *   +1  DLH      Divisor Latch High
 */

#include "uart.h"

#define UART_BASE   0x10000000U

/* Register offsets */
#define UART_THR    0   /* Transmit Holding Register (write, DLAB=0) */
#define UART_RBR    0   /* Receive Buffer Register   (read,  DLAB=0) */
#define UART_DLL    0   /* Divisor Latch Low         (DLAB=1)        */
#define UART_IER    1   /* Interrupt Enable Register (DLAB=0)        */
#define UART_DLH    1   /* Divisor Latch High        (DLAB=1)        */
#define UART_FCR    2   /* FIFO Control Register                     */
#define UART_LCR    3   /* Line Control Register                     */
#define UART_MCR    4   /* Modem Control Register                    */
#define UART_LSR    5   /* Line Status Register                      */

/* LSR bits */
#define LSR_DR      (1 << 0)  /* Data Ready (RX byte available)      */
#define LSR_THRE    (1 << 5)  /* TX Holding Register Empty           */

/* LCR bits */
#define LCR_WLS8    0x03      /* 8-bit word length                   */
#define LCR_DLAB    (1 << 7)  /* Divisor Latch Access Bit            */

/* Helpers for memory-mapped byte I/O */
static inline void reg_write(unsigned int offset, unsigned char val)
{
    *(volatile unsigned char *)(UART_BASE + offset) = val;
}

static inline unsigned char reg_read(unsigned int offset)
{
    return *(volatile unsigned char *)(UART_BASE + offset);
}

void uart_init(void)
{
    /*
     * 1. Disable interrupts — we poll in this minimal driver.
     */
    reg_write(UART_IER, 0x00);

    /*
     * 2. Set baud-rate divisor.
     *    QEMU virt clock = 3686400 Hz (legacy 16550 default).
     *    Divisor for 38400 baud = 3686400 / (16 * 38400) = 6.
     *    (The exact value doesn't matter in QEMU emulation, but
     *     setting it correctly is good practice.)
     */
    reg_write(UART_LCR, LCR_DLAB);   /* enable divisor latch access */
    reg_write(UART_DLL, 0x06);        /* divisor low byte            */
    reg_write(UART_DLH, 0x00);        /* divisor high byte           */

    /*
     * 3. 8-N-1, clear DLAB.
     */
    reg_write(UART_LCR, LCR_WLS8);

    /*
     * 4. Enable and reset FIFOs (FCR).
     *    Bit 0 = enable, bits 1-2 = reset RX/TX FIFOs, bits 6-7 = 14-byte trigger.
     */
    reg_write(UART_FCR, 0xC7);

    /*
     * 5. Assert DTR and RTS (required by some implementations).
     */
    reg_write(UART_MCR, 0x0B);
}

void uart_putc(char c)
{
    /* Block until the TX holding register is empty */
    while (!(reg_read(UART_LSR) & LSR_THRE))
        ;
    reg_write(UART_THR, (unsigned char)c);
}

void uart_puts(const char *s)
{
    while (*s) {
        if (*s == '\n')
            uart_putc('\r');  /* convert LF → CRLF for terminal compatibility */
        uart_putc(*s++);
    }
}

void uart_puthex(unsigned int v)
{
    static const char hex[] = "0123456789abcdef";
    uart_puts("0x");
    for (int shift = 28; shift >= 0; shift -= 4)
        uart_putc(hex[(v >> shift) & 0xf]);
    uart_putc('\n');
}
