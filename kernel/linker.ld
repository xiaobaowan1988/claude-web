/*
 * Linker script for RV32 hobby OS kernel.
 *
 * QEMU virt machine memory layout:
 *   0x00000000 - 0x0fffffff  Boot ROM / MROM
 *   0x02000000               CLINT (timer)
 *   0x0c000000               PLIC (interrupt controller)
 *   0x10000000               UART0 (NS16550A)
 *   0x80000000               DRAM start
 *
 * OpenSBI (the M-mode firmware) loads at 0x80000000 and jumps to
 * the next-stage payload at 0x80200000 in S-mode.  That is where
 * our kernel lives.
 */

OUTPUT_ARCH(riscv)
ENTRY(_start)

/*
 * No rv32 OpenSBI available in this QEMU build.
 * We boot directly at 0x80000000 in M-mode with -bios none.
 */
KERNEL_BASE = 0x80000000;

SECTIONS
{
    . = KERNEL_BASE;

    /*
     * ---- boot vector ----
     * QEMU (with -bios none) always starts execution at 0x80000000.
     * Place a single 32-bit jump here to reach _start wherever it lands.
     */
    .text.boot : {
        *(.text.boot)
    }

    /* ---- text ---- */
    .text : {
        /* Entry point must be immediately after the boot vector */
        *(.text.init)
        *(.text .text.*)
    }

    /* ---- read-only data ---- */
    .rodata : ALIGN(4) {
        *(.rodata .rodata.*)
    }

    /* ---- initialised data ---- */
    .data : ALIGN(4) {
        *(.data .data.*)
    }

    /* ---- zero-initialised data ---- */
    .bss : ALIGN(4) {
        _bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        _bss_end = .;
    }

    . = ALIGN(16);

    /* 64 KB kernel stack placed right after BSS */
    _stack_bottom = .;
    . += 0x10000;
    _stack_top = .;

    /* Discard everything the C runtime would normally need */
    /DISCARD/ : {
        *(.eh_frame)
        *(.comment)
        *(.note.gnu.build-id)   /* prevent build-id landing at 0x80000000 */
    }
}
