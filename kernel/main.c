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
#include "vm.h"

/* Linker-script symbol — byte after kernel stack (first free DRAM byte) */
extern char _stack_top[];

/* ------------------------------------------------------------------ */
/* Tiny decimal/hex printers                                           */
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
/* PMM helpers                                                          */
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
/* Phase 4 — S-mode entry                                              */
/*                                                                      */
/* Called via mret from vm_enter_s_mode().  At this point we are in   */
/* S-mode but still using physical addresses.  We enable Sv32, after  */
/* which all memory accesses go through the identity-mapped page table. */
/* ------------------------------------------------------------------ */
static __attribute__((noreturn)) void s_mode_entry(void)
{
    unsigned int sstatus, satp;

    /* Confirm S-mode by reading sstatus */
    asm volatile("csrr %0, sstatus" : "=r"(sstatus));
    uart_puts("[vm]   entered S-mode  (sstatus = "); uart_puthex(sstatus);
    uart_puts("[vm]   SPP bit = ");
    uart_putc(((sstatus >> 8) & 1) ? '1' : '0');
    uart_puts("  (0 = came from U-mode/M, means first S-mode entry)\r\n");
    uart_puts("\r\n");

    /* Enable Sv32: writes satp and executes sfence.vma */
    uart_puts("[vm]   enabling Sv32 ...\r\n");
    vm_enable_sv32();

    /* From this point all addresses go through the page table */
    satp = vm_satp_val();
    uart_puts("[vm]   satp             : "); uart_puthex(satp);
    uart_puts("[vm]   MODE bit [31]    : ");
    uart_putc(((satp >> 31) & 1) ? '1' : '0');
    uart_puts("  (1 = Sv32 active)\r\n");
    uart_puts("[vm]   root PT PPN      : "); uart_puthex(satp & 0x3FFFFFu);
    uart_puts("\r\n");
    uart_puts("[vm]   Sv32 ACTIVE — kernel running under virtual memory\r\n");
    uart_puts("\r\n");

    /* ---------------------------------------------------------------- */
    uart_puts("[boot] Phase 4 complete.  Halting.\r\n");
    uart_puts("\r\n");
    uart_puts("Next phases:\r\n");
    uart_puts("  Phase 5 : Trap handler + process struct + context switch\r\n");
    uart_puts("  Phase 6 : CLINT timer interrupt + round-robin scheduler\r\n");
    uart_puts("  Phase 7 : System calls (ecall) + ELF32 loader\r\n");
    uart_puts("  Phase 8 : VFS + ramfs + shell\r\n");
    uart_puts("\r\n");

    while (1)
        asm volatile("wfi");
}

/* ------------------------------------------------------------------ */
/* kernel_main — runs in M-mode                                        */
/* ------------------------------------------------------------------ */
void kernel_main(unsigned int hart_id, unsigned int dtb)
{
    uart_init();

    uart_puts("\r\n");
    uart_puts("==============================================\r\n");
    uart_puts("  RV32 Hobby OS  --  Phase 4: Sv32 VM        \r\n");
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
    pmm_init((unsigned int)_stack_top);
    uart_puts("[pmm]  init complete.\r\n");
    print_pmm_stats();
    uart_puts("\r\n");

    /* ----------------------------------------------------------------
     * Phase 4 — Sv32 Virtual Memory
     * ---------------------------------------------------------------- */
    uart_puts("--- Phase 4: Sv32 Virtual Memory ---\r\n\r\n");

    uart_puts("[vm]   building page table ...\r\n");
    vm_init();

    uart_puts("[vm]   root_pt phys    : "); uart_puthex(vm_root_pt_phys());
    uart_puts("[vm]   megapage map:\r\n");
    uart_puts("[vm]     DRAM   0x80000000 - 0x8FFFFFFF  (64 x 4 MB, R/W/X)\r\n");
    uart_puts("[vm]     UART   0x10000000 - 0x103FFFFF  ( 1 x 4 MB, R/W)\r\n");
    uart_puts("[vm]     CLINT  0x02000000 - 0x023FFFFF  ( 1 x 4 MB, R/W)\r\n");
    uart_puts("[vm]     PLIC   0x0C000000 - 0x0FFFFFFF  (16 x 4 MB, R/W)\r\n");
    uart_puts("\r\n");

    uart_puts("[vm]   switching M-mode -> S-mode ...\r\n");
    vm_enter_s_mode(s_mode_entry);   /* does not return */
}
