/*
 * main.c — kernel entry point (C portion, M-mode)
 *
 * Boot flow:
 *   QEMU → U-Boot (0x80000000, M-mode) → kernel (0x80200000, M-mode)
 *
 * U-Boot loads kernel.bin from virtio disk to 0x80200000 and jumps
 * via `go 0x80200000`.  U-Boot passes a0=0, a1=0 (no SBI, no DTB)
 * so we use the static memory map from QEMU's virt machine spec.
 */

#include "uart.h"
#include "pmm.h"

/* Linker-script symbols — end of kernel + stack (first free byte) */
extern char _stack_top[];

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
/* Read M-mode CSRs                                                    */
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
/* PMM demo helpers                                                    */
/* ------------------------------------------------------------------ */
static void print_pmm_stats(void)
{
    unsigned int used, total, free_pages;
    pmm_stats(&used, &total);
    free_pages = total - used;

    uart_puts("[pmm]  total pages   : "); print_uint(total);
    uart_puts("  ("); print_uint(total >> 8); uart_puts(" MB)\r\n");

    uart_puts("[pmm]  used  pages   : "); print_uint(used);
    uart_puts("  ("); print_uint(used >> 8); uart_puts(" MB)\r\n");

    uart_puts("[pmm]  free  pages   : "); print_uint(free_pages);
    uart_puts("  ("); print_uint(free_pages >> 8); uart_puts(" MB)\r\n");
}

/* ------------------------------------------------------------------ */
/* kernel_main                                                         */
/* ------------------------------------------------------------------ */
void kernel_main(unsigned int hart_id, unsigned int dtb)
{
    uart_init();

    uart_puts("\r\n");
    uart_puts("==============================================\r\n");
    uart_puts("  RV32 Hobby OS  --  Phase 3: PMM            \r\n");
    uart_puts("==============================================\r\n");
    uart_puts("\r\n");

    /* Boot info */
    uart_puts("[boot] Hart ID       : "); print_uint(hart_id); uart_puts("\r\n");
    uart_puts("[boot] DTB addr      : "); uart_puthex(dtb);
    uart_puts("[boot] Privilege     : M-mode (Machine)\r\n");
    uart_puts("[boot] Kernel end    : "); uart_puthex((unsigned int)_stack_top);
    uart_puts("\r\n");

    /* CSRs */
    uart_puts("[csr]  mstatus       : "); uart_puthex(csr_mstatus());
    uart_puts("[csr]  mie           : "); uart_puthex(csr_mie());
    uart_puts("[csr]  mtvec         : "); uart_puthex(csr_mtvec());
    uart_puts("[csr]  marchid       : "); uart_puthex(csr_marchid());
    uart_puts("[csr]  mimpid        : "); uart_puthex(csr_mimpid());
    uart_puts("\r\n");

    /* ----------------------------------------------------------------
     * Phase 3 — Physical Memory Manager
     * ---------------------------------------------------------------- */
    uart_puts("--- Phase 3: Physical Memory Manager ---\r\n\r\n");

    /* Initialise: reserve [PHYS_BASE .. _stack_top), free everything above */
    pmm_init((unsigned int)_stack_top);
    uart_puts("[pmm]  init complete.  Allocatable DRAM:\r\n");
    print_pmm_stats();
    uart_puts("\r\n");

    /* Allocate 4 pages and show their addresses */
    uart_puts("[pmm]  allocating 4 pages ...\r\n");
    void *p[4];
    for (int i = 0; i < 4; i++) {
        p[i] = pmm_alloc_page();
        uart_puts("         p["); print_uint(i); uart_puts("] = ");
        uart_puthex((unsigned int)p[i]);
    }
    uart_puts("\r\n");
    print_pmm_stats();
    uart_puts("\r\n");

    /* Free page 1, then allocate again — must get the same address back */
    uart_puts("[pmm]  freeing p[1] = "); uart_puthex((unsigned int)p[1]);
    pmm_free_page(p[1]);
    uart_puts("[pmm]  allocating 1 page  → ");
    void *recycled = pmm_alloc_page();
    uart_puthex((unsigned int)recycled);
    if (recycled == p[1])
        uart_puts("[pmm]  recycled correctly (same page returned)\r\n");
    else
        uart_puts("[pmm]  ERROR: unexpected address\r\n");
    uart_puts("\r\n");
    print_pmm_stats();
    uart_puts("\r\n");

    /* Free all pages back and confirm stats return to baseline */
    uart_puts("[pmm]  freeing all 4 pages ...\r\n");
    pmm_free_page(p[0]);
    pmm_free_page(recycled);    /* p[1] slot */
    pmm_free_page(p[2]);
    pmm_free_page(p[3]);
    print_pmm_stats();
    uart_puts("\r\n");

    /* ---------------------------------------------------------------- */
    uart_puts("[boot] Phase 3 complete.  Halting.\r\n");
    uart_puts("\r\n");
    uart_puts("Next phases:\r\n");
    uart_puts("  Phase 4 : Sv32 virtual memory + kernel page tables\r\n");
    uart_puts("  Phase 5 : Trap/exception handler + context switch\r\n");
    uart_puts("  Phase 6 : CLINT timer interrupt + round-robin scheduler\r\n");
    uart_puts("  Phase 7 : ecall + ELF32 loader + first user process\r\n");
    uart_puts("  Phase 8 : VFS + ramfs + shell\r\n");
    uart_puts("\r\n");

    while (1)
        asm volatile("wfi");
}
