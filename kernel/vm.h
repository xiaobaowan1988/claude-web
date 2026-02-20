/*
 * vm.h — Sv32 virtual memory (Phase 4)
 *
 * RISC-V Sv32 uses a two-level page table with 32-bit virtual addresses:
 *
 *   VA[31:22]  VPN[1]  — root page table index  (10 bits → 1024 entries)
 *   VA[21:12]  VPN[0]  — leaf page table index  (10 bits → 1024 entries)
 *   VA[11: 0]  offset                            (12 bits → 4 KB page)
 *
 * A "megapage" (Sv32 superpage) is a level-1 leaf entry that maps 4 MB
 * (2^22 bytes) directly — no second-level table needed.  PPN[0] must be 0.
 *
 * PTE format (32-bit):
 *   Bits 31:20  PPN[1]   (12 bits)
 *   Bits 19:10  PPN[0]   (10 bits)
 *   Bits  9: 8  RSW      (reserved for software)
 *   Bit      7  D        dirty
 *   Bit      6  A        accessed
 *   Bit      5  G        global
 *   Bit      4  U        user-accessible
 *   Bit      3  X        execute
 *   Bit      2  W        write
 *   Bit      1  R        read
 *   Bit      0  V        valid
 *
 * satp register (Sv32 mode):
 *   Bit     31  MODE = 1  (enable Sv32)
 *   Bits 21: 0  PPN of root page table
 */

#ifndef VM_H
#define VM_H

/* ---- PTE permission / status bits ---------------------------------- */
#define PTE_V   (1u << 0)   /* valid                                    */
#define PTE_R   (1u << 1)   /* readable                                 */
#define PTE_W   (1u << 2)   /* writable                                 */
#define PTE_X   (1u << 3)   /* executable                               */
#define PTE_U   (1u << 4)   /* user-accessible                          */
#define PTE_G   (1u << 5)   /* global mapping                           */
#define PTE_A   (1u << 6)   /* accessed (must be set by SW on QEMU)     */
#define PTE_D   (1u << 7)   /* dirty    (must be set by SW on QEMU)     */

/* ---- satp ---------------------------------------------------------- */
#define SATP_SV32       (1u << 31)  /* enable Sv32 translation           */
#define PAGE_SHIFT      12u
#define MEGAPAGE_SIZE   (1u << 22)  /* 4 MB                              */
#define MEGAPAGE_SHIFT  22u

/* ---- API ----------------------------------------------------------- */

/*
 * vm_init() — populate the kernel root page table (call from M-mode).
 *
 * Identity-maps (VA == PA) the following regions with 4 MB megapages:
 *   0x80000000 – 0x8FFFFFFF  64 × 4 MB  DRAM (R/W/X)
 *   0x10000000 – 0x103FFFFF   1 × 4 MB  UART (R/W)
 *   0x02000000 – 0x023FFFFF   1 × 4 MB  CLINT (R/W)
 *   0x0C000000 – 0x0FFFFFFF  16 × 4 MB  PLIC (R/W)
 */
void vm_init(void);

/* vm_root_pt_phys() — physical address of the root page table. */
unsigned int vm_root_pt_phys(void);

/*
 * vm_enter_s_mode(entry) — switch from M-mode to S-mode via mret.
 *
 * Sets mstatus.MPP = S-mode, writes mepc = entry, then executes mret.
 * Does NOT return.  entry() will be called in S-mode.
 */
__attribute__((noreturn))
void vm_enter_s_mode(void (*entry)(void));

/*
 * vm_enable_sv32() — enable Sv32 paging (call from S-mode).
 *
 * Writes satp = SATP_SV32 | (root_pt >> PAGE_SHIFT)
 * then executes sfence.vma to flush TLB/pipeline.
 * After this returns, all memory accesses go through the page table.
 */
void vm_enable_sv32(void);

/* vm_satp_val() — return the value written to satp by vm_enable_sv32(). */
unsigned int vm_satp_val(void);

#endif /* VM_H */
