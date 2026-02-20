# RV32 Hobby OS

A RISC-V 32-bit Linux-like hobby OS built from scratch.
Runs on QEMU's `virt` machine (emulated on any host architecture).

## Roadmap

| Phase | Status | Description |
|-------|--------|-------------|
| 2 | ✅ Done | Bare-metal boot → UART "Hello World" |
| 3 | 🔲 Next | Physical memory manager (bitmap allocator) |
| 4 | 🔲 | Sv32 virtual memory + kernel page tables |
| 5 | 🔲 | Trap handler + process struct + context switch |
| 6 | 🔲 | CLINT timer interrupt + round-robin scheduler |
| 7 | 🔲 | System calls (`ecall`) + ELF32 loader |
| 8 | 🔲 | VFS + ramfs + shell |

## Quick Start

```bash
# Prerequisites (Ubuntu/Debian)
sudo apt install gcc-riscv64-linux-gnu qemu-system-misc

# Build
make

# Run (Ctrl-A then X to quit QEMU)
make run

# Debug with GDB
make run-gdb          # terminal 1: QEMU waits for GDB
gdb-multiarch         # terminal 2
  (gdb) set architecture riscv:rv32
  (gdb) target remote :1234
  (gdb) file kernel.elf
  (gdb) break kernel_main
  (gdb) continue
  (gdb) layout asm
```

## Project Layout

```
.
├── Makefile
├── README.md
└── kernel/
    ├── linker.ld   Linker script (load address, section layout)
    ├── start.S     Assembly entry: stack, BSS clear, trap vector
    ├── uart.h      UART API
    ├── uart.c      NS16550A UART driver (QEMU virt 0x10000000)
    └── main.c      kernel_main() — first C code to run
```

## Hardware Model (QEMU virt machine)

| Address        | Device               |
|---------------|----------------------|
| `0x00001000`  | MROM (reset vector)  |
| `0x02000000`  | CLINT (timer)        |
| `0x0c000000`  | PLIC (interrupts)    |
| `0x10000000`  | UART0 (NS16550A)     |
| `0x80000000`  | DRAM start           |
| `0x80000000`  | OpenSBI firmware     |
| `0x80200000`  | **Our kernel**       |

## Toolchain Notes

Ubuntu ships `gcc-riscv64-linux-gnu` (rv64 target).
We compile for rv32 by passing:

```
-march=rv32imac_zicsr  -mabi=ilp32  -ffreestanding  -nostdlib
```

This produces valid rv32 machine code. The `-zicsr` extension is needed
so GCC accepts `csrr`/`csrw` instructions.
