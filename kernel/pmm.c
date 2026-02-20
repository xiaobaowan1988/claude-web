/*
 * pmm.c — Physical Memory Manager (Phase 3)
 *
 * Bitmap allocator: one bit per 4 KB physical page.
 *   bit = 0  →  page is FREE
 *   bit = 1  →  page is RESERVED or ALLOCATED
 *
 * The bitmap lives in .bss (zero-initialised by start.S), so all pages
 * start as "free" in the zero state.  pmm_init() first marks every page
 * as reserved, then explicitly frees the allocatable range.  This ensures
 * that any DRAM we have not accounted for (U-Boot, hardware regions, etc.)
 * stays off-limits.
 */

#include "pmm.h"

/* ------------------------------------------------------------------ */
/* Bitmap storage                                                       */
/* ------------------------------------------------------------------ */

/*
 * 65536 pages / 32 bits per word = 2048 words × 4 bytes = 8 KB in .bss
 */
#define BITMAP_WORDS  (TOTAL_PAGES / 32u)

static unsigned int bitmap[BITMAP_WORDS];   /* 0=free, 1=used */
static unsigned int n_used;                 /* pages currently allocated */

/* ------------------------------------------------------------------ */
/* Bit helpers                                                          */
/* ------------------------------------------------------------------ */

static inline void page_set_used(unsigned int idx)
{
    bitmap[idx >> 5] |=  (1u << (idx & 31u));
}

static inline void page_set_free(unsigned int idx)
{
    bitmap[idx >> 5] &= ~(1u << (idx & 31u));
}

static inline int page_is_used(unsigned int idx)
{
    return (bitmap[idx >> 5] >> (idx & 31u)) & 1u;
}

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

void pmm_init(unsigned int kernel_end_phys)
{
    unsigned int first_free, i;

    /* 1. Reserve every page (bitmap starts as all-zeros from BSS,
     *    so we set all bits to 1). */
    for (i = 0; i < BITMAP_WORDS; i++)
        bitmap[i] = ~0u;
    n_used = TOTAL_PAGES;

    /* 2. Round kernel_end up to the next page boundary. */
    kernel_end_phys = (kernel_end_phys + PAGE_SIZE - 1u) & ~(PAGE_SIZE - 1u);

    /* 3. Free every page from [kernel_end .. PHYS_END). */
    first_free = (kernel_end_phys - PHYS_BASE) >> PAGE_SHIFT;
    for (i = first_free; i < TOTAL_PAGES; i++) {
        page_set_free(i);
        n_used--;
    }
}

void *pmm_alloc_page(void)
{
    unsigned int i, bit;

    /* Linear scan: find the first word with a free bit (bit = 0). */
    for (i = 0; i < BITMAP_WORDS; i++) {
        if (bitmap[i] == ~0u)
            continue;                       /* all used — skip */

        /* Isolate the lowest free bit: free bits are 0, so invert. */
        unsigned int free_bits = ~bitmap[i];
        /* CTZ: find position of lowest set bit via (v & -v) trick. */
        unsigned int lowest = free_bits & (unsigned int)(-(int)free_bits);
        bit = 0;
        while (!((lowest >> bit) & 1u))
            bit++;

        unsigned int idx = (i << 5u) | bit;
        page_set_used(idx);
        n_used++;
        return (void *)(PHYS_BASE + ((unsigned int)idx << PAGE_SHIFT));
    }
    return (void *)0;   /* out of memory */
}

void pmm_free_page(void *pa)
{
    unsigned int addr = (unsigned int)pa;
    unsigned int idx;

    if (addr < PHYS_BASE || addr >= PHYS_END)
        return;                             /* outside managed DRAM */
    if (addr & (PAGE_SIZE - 1u))
        return;                             /* not page-aligned */

    idx = (addr - PHYS_BASE) >> PAGE_SHIFT;
    if (!page_is_used(idx))
        return;                             /* double-free guard */

    page_set_free(idx);
    n_used--;
}

void pmm_stats(unsigned int *used, unsigned int *total)
{
    *used  = n_used;
    *total = TOTAL_PAGES;
}
