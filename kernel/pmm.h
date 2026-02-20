/*
 * pmm.h — Physical Memory Manager (Phase 3)
 *
 * A simple bitmap allocator over the DRAM region.
 * One bit per 4 KB page: 0 = free, 1 = reserved/allocated.
 *
 * Memory map assumed (matches QEMU -m 256M):
 *   PHYS_BASE  0x80000000
 *   PHYS_END   0x90000000   (256 MB)
 *   PAGE_SIZE  4096
 *   TOTAL_PAGES 65536       bitmap = 8 KB in .bss
 */

#ifndef PMM_H
#define PMM_H

#define PAGE_SIZE    4096u
#define PAGE_SHIFT   12u

#define PHYS_BASE    0x80000000u
#define PHYS_END     0x90000000u          /* 256 MB DRAM */
#define TOTAL_PAGES  ((PHYS_END - PHYS_BASE) >> PAGE_SHIFT)   /* 65536 */

/*
 * pmm_init — mark all pages reserved, then free everything above
 *            kernel_end (rounded up to a page boundary).
 *
 * Pass _stack_top from the linker script:
 *   extern char _stack_top[];
 *   pmm_init((unsigned int)_stack_top);
 */
void pmm_init(unsigned int kernel_end_phys);

/*
 * pmm_alloc_page — allocate one physical page.
 * Returns the physical address (always PAGE_SIZE-aligned), or 0 on OOM.
 */
void *pmm_alloc_page(void);

/*
 * pmm_free_page — return a previously allocated page to the pool.
 * Silently ignores addresses outside DRAM or already-free pages.
 */
void pmm_free_page(void *pa);

/* pmm_stats — fill *used and *total with page counts. */
void pmm_stats(unsigned int *used, unsigned int *total);

#endif /* PMM_H */
