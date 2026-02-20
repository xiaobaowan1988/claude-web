/*
 * vm.c — Sv32 virtual memory manager (Phase 4)
 *
 * Provides:
 *   vm_init()         build kernel page table (identity-mapped megapages)
 *   vm_enter_s_mode() switch from M-mode to S-mode via mret
 *   vm_enable_sv32()  write satp + sfence.vma  (call from S-mode)
 */

#include "vm.h"

/* ------------------------------------------------------------------ */
/* Root page table                                                      */
/*                                                                      */
/* 1024 entries × 4 bytes = 4 KB (one page).                           */
/* __attribute__((aligned(4096))) ensures it sits on a page boundary   */
/* so satp's PPN field (root_pt >> 12) is exact.                       */
/* Placed in .bss — zeroed by start.S before kernel_main() runs.      */
/* ------------------------------------------------------------------ */
static unsigned int root_pt[1024] __attribute__((aligned(4096)));

static unsigned int saved_satp;

/* ------------------------------------------------------------------ */
/* Internal helpers                                                     */
/* ------------------------------------------------------------------ */

/*
 * make_megapage_pte — build a Sv32 level-1 leaf PTE for a 4 MB megapage.
 *
 * pa must be 4 MB-aligned (bits 21:0 == 0).
 *
 * PTE layout:
 *   bits 31:20  PPN[1] = pa >> 22
 *   bits 19:10  PPN[0] = 0          (required for megapage)
 *   bits  9: 0  flags | V | A | D
 *
 * Since pa is 4 MB-aligned, (pa >> 22) << 20 == (pa >> 2), so we can
 * compute the full PPN field cheaply as (pa >> 2).
 */
static unsigned int make_megapage_pte(unsigned int pa, unsigned int flags)
{
    return (pa >> 2) | flags | PTE_V | PTE_A | PTE_D;
}

/*
 * map_megapages — install 'count' consecutive 4 MB identity-mapped
 * megapage entries starting at physical address pa_start.
 */
static void map_megapages(unsigned int pa_start,
                           unsigned int count,
                           unsigned int flags)
{
    unsigned int i;
    for (i = 0; i < count; i++) {
        unsigned int pa   = pa_start + i * MEGAPAGE_SIZE;
        unsigned int vpn1 = pa >> MEGAPAGE_SHIFT;   /* == va >> 22 for id-map */
        root_pt[vpn1] = make_megapage_pte(pa, flags);
    }
}

/* ------------------------------------------------------------------ */
/* Minimal S-mode trap stub                                             */
/*                                                                      */
/* Written before entering S-mode so that any unexpected trap lands     */
/* here (WFI loop) rather than on a garbage stvec.                      */
/* __attribute__((naked)) suppresses prologue/epilogue; the function    */
/* body is pure assembly.                                               */
/* ------------------------------------------------------------------ */
__attribute__((naked)) void vm_s_trap_stub(void)
{
    asm volatile("1:\n"
                 "    wfi\n"
                 "    j 1b\n");
}

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

void vm_init(void)
{
    unsigned int i;

    /* Clear table (start.S zeroed .bss, but be explicit) */
    for (i = 0; i < 1024; i++)
        root_pt[i] = 0;

    /*
     * Identity-map 256 MB DRAM: 64 × 4 MB megapages
     *   PA/VA 0x80000000 – 0x8FFFFFFF  →  R/W/X
     * Kernel code, data, stack, PMM bitmap and page table all live here.
     */
    map_megapages(0x80000000u, 64u, PTE_R | PTE_W | PTE_X);

    /*
     * Identity-map peripheral regions (R/W, non-executable):
     *   UART  0x10000000 – 0x103FFFFF   1 × 4 MB
     *   CLINT 0x02000000 – 0x023FFFFF   1 × 4 MB
     *   PLIC  0x0C000000 – 0x0FFFFFFF  16 × 4 MB (64 MB)
     */
    map_megapages(0x10000000u,  1u, PTE_R | PTE_W);
    map_megapages(0x02000000u,  1u, PTE_R | PTE_W);
    map_megapages(0x0C000000u, 16u, PTE_R | PTE_W);
}

unsigned int vm_root_pt_phys(void)
{
    return (unsigned int)root_pt;
}

__attribute__((noreturn)) void vm_enter_s_mode(void (*entry)(void))
{
    unsigned int mstatus;

    /*
     * PMP (Physical Memory Protection) — grant S-mode and U-mode
     * unrestricted access to the entire address space.
     *
     * U-Boot may have programmed PMP entries that lock down S-mode
     * before handing control to us.  Without explicit PMP setup,
     * the first instruction fetch in S-mode would fault.
     *
     * RV32 pmpaddr registers hold PA[33:2].  Setting all bits to 1
     * in NAPOT mode (pmpcfg A=11) covers 2^34 bytes — the full
     * 34-bit physical address space.
     *
     * pmpcfg byte layout:
     *   bit 7   L  locked (we leave unlocked so it can be tightened later)
     *   bits 4:3 A  11 = NAPOT
     *   bit 2   X  execute
     *   bit 1   W  write
     *   bit 0   R  read
     *   → 0b00011111 = 0x1F
     */
    asm volatile("csrw pmpaddr0, %0" :: "r"(0xFFFFFFFFu));
    asm volatile("csrw pmpcfg0,  %0" :: "r"(0x1Fu));

    /* Point stvec at our stub so unexpected S-mode traps don't crash */
    asm volatile("csrw stvec, %0" :: "r"(vm_s_trap_stub));

    /*
     * Delegate all synchronous exceptions to S-mode.
     * Bit mask 0xFFFF covers exception codes 0–15 (page faults, etc.)
     */
    asm volatile("csrw medeleg, %0" :: "r"(0xFFFFu));

    /*
     * Build new mstatus:
     *   MPP  [12:11] = 01  (return to S-mode)
     *   MPIE [5]     =  1  (interrupts enabled after mret)
     */
    asm volatile("csrr %0, mstatus" : "=r"(mstatus));
    mstatus  = (mstatus & ~(3u << 11));   /* clear MPP */
    mstatus |= (1u << 11);                /* MPP = S-mode */
    mstatus |= (1u << 5);                 /* MPIE = 1 */

    /*
     * Write mstatus + mepc in one asm block then execute mret.
     * Keeping them together prevents the compiler inserting anything
     * between the privilege-change setup and the mret instruction.
     */
    asm volatile(
        "csrw mstatus, %0\n"
        "csrw mepc,    %1\n"
        "mret\n"
        :: "r"(mstatus), "r"(entry)
    );

    __builtin_unreachable();
}

void vm_enable_sv32(void)
{
    saved_satp = SATP_SV32 | ((unsigned int)root_pt >> PAGE_SHIFT);

    asm volatile("csrw satp, %0"         :: "r"(saved_satp));
    asm volatile("sfence.vma zero, zero" ::: "memory");
    /*
     * After sfence.vma all subsequent instruction fetches and data
     * accesses go through the page table.  Because we identity-mapped
     * the kernel, the PC and stack pointer remain valid addresses.
     */
}

unsigned int vm_satp_val(void)
{
    return saved_satp;
}
