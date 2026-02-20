/*
 * main.c — kernel entry point (C portion, M-mode)
 *
 * Called from start.S after the stack is set up and BSS is zeroed.
 * We are running in Machine mode (M-mode) — the highest RISC-V privilege.
 *
 * Arguments forwarded by QEMU's built-in loader (via a0/a1 in start.S):
 *   hart_id  — hardware thread ID of the boot hart (0 for single-hart QEMU)
 *   dtb      — physical address of the Flattened Device Tree blob (may be 0)
 */

#include "uart.h"

/* ------------------------------------------------------------------ */
/* Tiny decimal printer (avoids pulling in printf/libc)                */
/* ------------------------------------------------------------------ */
static void print_uint(unsigned int v)
{
    if (v == 0) { uart_putc('0'); return; }
    char buf[10];
    int i = 0;
    while (v) { buf[i++] = '0' + (v % 10); v /= 10; }
    while (i--) uart_putc(buf[i]);
}

/* ------------------------------------------------------------------ */
/* Read M-mode CSRs (all accessible in M-mode)                        */
/* ------------------------------------------------------------------ */
static unsigned int csr_mstatus(void)
{
    unsigned int v; asm volatile("csrr %0, mstatus" : "=r"(v)); return v;
}
static unsigned int csr_mie(void)
{
    unsigned int v; asm volatile("csrr %0, mie"     : "=r"(v)); return v;
}
static unsigned int csr_mtvec(void)
{
    unsigned int v; asm volatile("csrr %0, mtvec"   : "=r"(v)); return v;
}
static unsigned int csr_marchid(void)
{
    unsigned int v; asm volatile("csrr %0, marchid"  : "=r"(v)); return v;
}
static unsigned int csr_mimpid(void)
{
    unsigned int v; asm volatile("csrr %0, mimpid"   : "=r"(v)); return v;
}

/* ------------------------------------------------------------------ */
/* kernel_main                                                         */
/* ------------------------------------------------------------------ */
void kernel_main(unsigned int hart_id, unsigned int dtb)
{
    uart_init();

    uart_puts("\r\n");
    uart_puts("==============================================\r\n");
    uart_puts("  RV32 Hobby OS  --  Phase 2: Bare-Metal    \r\n");
    uart_puts("==============================================\r\n");
    uart_puts("\r\n");

    /* Boot parameters */
    uart_puts("[boot] Hart ID       : ");  print_uint(hart_id); uart_puts("\r\n");
    uart_puts("[boot] DTB phys addr : ");  uart_puthex(dtb);
    uart_puts("[boot] Privilege     : M-mode (Machine)\r\n");
    uart_puts("\r\n");

    /* Machine-mode CSRs */
    uart_puts("[csr]  mstatus       : ");  uart_puthex(csr_mstatus());
    uart_puts("[csr]  mie           : ");  uart_puthex(csr_mie());
    uart_puts("[csr]  mtvec         : ");  uart_puthex(csr_mtvec());
    uart_puts("[csr]  marchid       : ");  uart_puthex(csr_marchid());
    uart_puts("[csr]  mimpid        : ");  uart_puthex(csr_mimpid());
    uart_puts("\r\n");

    /* Memory map */
    uart_puts("[mem]  DRAM start    : 0x80000000\r\n");
    uart_puts("[mem]  Kernel base   : 0x80000000\r\n");
    uart_puts("[mem]  UART base     : 0x10000000\r\n");
    uart_puts("[mem]  CLINT base    : 0x02000000\r\n");
    uart_puts("[mem]  PLIC base     : 0x0c000000\r\n");
    uart_puts("\r\n");

    /* Done */
    uart_puts("[boot] Initialisation complete.  Halting hart 0.\r\n");
    uart_puts("\r\n");
    uart_puts("Next phases:\r\n");
    uart_puts("  Phase 3 : Physical memory manager (bitmap allocator)\r\n");
    uart_puts("  Phase 4 : Sv32 virtual memory + kernel page tables\r\n");
    uart_puts("  Phase 5 : Trap/exception handler + context switch\r\n");
    uart_puts("  Phase 6 : CLINT timer interrupt + round-robin scheduler\r\n");
    uart_puts("  Phase 7 : ecall + ELF32 loader + first user process\r\n");
    uart_puts("  Phase 8 : VFS + ramfs + shell\r\n");
    uart_puts("\r\n");

    /* Halt */
    while (1)
        asm volatile("wfi");
}
