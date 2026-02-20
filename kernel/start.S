/*
 * start.S — kernel entry point (RV32, M-mode)
 *
 * With `-bios none -kernel kernel.elf`, QEMU's built-in loader places a
 * small jump stub in MROM at 0x1000 and hands control to 0x80000000 in
 * Machine mode (the highest privilege level).
 *
 * At entry:
 *   a0 = hart ID  (always 0 for single-core QEMU virt)
 *   a1 = pointer to flattened device tree (FDT) — or 0 if none
 *   Machine mode CSRs are active (mstatus, mie, mtvec, …)
 */

/*
 * Boot vector — must land at exactly 0x80000000.
 * Provides a guaranteed 32-bit jump to _start regardless of where the
 * linker places _start within the .text region.
 */
.section .text.boot
.global _boot_vector
_boot_vector:
    j       _start              /* 4-byte JAL-encoded unconditional jump */

/* ---- actual entry point ---- */
.section .text.init
.global _start

_start:
    /*
     * ---------------------------------------------------------------
     * 1. Only hart 0 continues; all others park in a WFI loop.
     *    (Future SMP bringup will wake secondary harts here.)
     * ---------------------------------------------------------------
     */
    csrr    t0, mhartid
    bnez    t0, park

    /*
     * ---------------------------------------------------------------
     * 2. Disable all interrupts while we set up.
     * ---------------------------------------------------------------
     */
    csrw    mstatus, zero
    csrw    mie,     zero

    /*
     * ---------------------------------------------------------------
     * 3. Install an early machine-mode trap vector (direct mode).
     *    If anything goes wrong before the real trap handler is set up
     *    we get a message on UART instead of a silent hang.
     * ---------------------------------------------------------------
     */
    la      t0, early_trap_vector
    csrw    mtvec, t0

    /*
     * ---------------------------------------------------------------
     * 4. Set the stack pointer.
     *    _stack_top is defined in linker.ld; stacks grow downward.
     * ---------------------------------------------------------------
     */
    la      sp, _stack_top

    /*
     * ---------------------------------------------------------------
     * 5. Zero the BSS section.
     *    C globals without explicit initialisers must start as zero.
     * ---------------------------------------------------------------
     */
    la      t0, _bss_start
    la      t1, _bss_end
1:
    bgeu    t0, t1, 2f
    sw      zero, 0(t0)
    addi    t0, t0, 4
    j       1b

2:
    /*
     * ---------------------------------------------------------------
     * 6. Jump to the C kernel entry.
     *    a0 (hart ID) and a1 (DTB pointer) are already in the right
     *    argument registers — just call kernel_main().
     * ---------------------------------------------------------------
     */
    call    kernel_main

    /* If kernel_main returns, fall through to park */
park:
    wfi
    j       park


/*
 * early_trap_vector — a bare-metal M-mode trap handler that prints a
 * diagnostic message and halts.  Called before kernel_main sets up the
 * real handler.
 *
 * We stay in pure assembly here to avoid relying on a working stack.
 */
.align 2
early_trap_vector:
    li      t0, 0x10000000      /* UART0 THR/LSR base address */

    /* Macro: spin until UART TX ready, then send one character */
.macro uart_putc reg, char
    li      \reg, \char
99: lb      t2, 5(t0)           /* LSR */
    andi    t2, t2, 0x20        /* bit 5 = THRE (TX holding empty) */
    beqz    t2, 99b
    sb      \reg, 0(t0)         /* write to THR */
.endm

    uart_putc t1, '\r'
    uart_putc t1, '\n'
    uart_putc t1, '!'
    uart_putc t1, 'E'
    uart_putc t1, 'A'
    uart_putc t1, 'R'
    uart_putc t1, 'L'
    uart_putc t1, 'Y'
    uart_putc t1, '_'
    uart_putc t1, 'T'
    uart_putc t1, 'R'
    uart_putc t1, 'A'
    uart_putc t1, 'P'
    uart_putc t1, '!'
    uart_putc t1, '\r'
    uart_putc t1, '\n'

    /* Print mcause so we can see what triggered the trap */
    csrr    t3, mcause
    /* Print low nibble for a quick hint */
    andi    t3, t3, 0xf
    li      t4, 10
    blt     t3, t4, 1f
    addi    t3, t3, 'A' - 10
    j       2f
1:  addi    t3, t3, '0'
2:
    lb      t2, 5(t0)
    andi    t2, t2, 0x20
    beqz    t2, 2b
    sb      t3, 0(t0)
    uart_putc t1, '\r'
    uart_putc t1, '\n'

halt_loop:
    wfi
    j       halt_loop
