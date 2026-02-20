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
 * Boot flow with U-Boot:
 *   0x80000000  U-Boot (M-mode firmware + bootloader)
 *   0x80200000  Our kernel (loaded from virtio disk by U-Boot, then `go`)
 */

OUTPUT_ARCH(riscv)
ENTRY(_start)

/* U-Boot lives at 0x80000000; kernel is loaded here by U-Boot. */
KERNEL_BASE = 0x80200000;

SECTIONS
{
    . = KERNEL_BASE;

    /*
     * ---- boot vector ----
     * U-Boot's `go 0x80200000` jumps directly to this address.
     * Place a 32-bit unconditional jump so _start can follow at any offset.
     */
    .text.boot : {
        *(.text.boot)
    }

    /* ---- text ---- */
    .text : {
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

    /DISCARD/ : {
        *(.eh_frame)
        *(.comment)
        *(.note.gnu.build-id)
    }
}
