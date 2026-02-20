# RV32 Hobby OS

A RISC-V 32-bit Linux-like hobby OS built from scratch.
Runs on QEMU's `virt` machine (emulated on any host architecture).

## Roadmap

| Phase | Status | Description |
|-------|--------|-------------|
| 2 | ✅ Done | Bare-metal boot → UART "Hello World" |
| 3 | ✅ Done | Physical memory manager (bitmap allocator) |
| 4 | 🔲 Next | Sv32 virtual memory + kernel page tables |
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
    ├── pmm.h       Physical Memory Manager API
    ├── pmm.c       Bitmap allocator (1 bit/page, 8 KB for 256 MB DRAM)
    └── main.c      kernel_main() — first C code to run
```

## Physical Memory Manager (Phase 3)

`kernel/pmm.c` implements a simple bitmap allocator over the 256 MB DRAM region
(`0x80000000` – `0x90000000`).

| Constant | Value | Meaning |
|----------|-------|---------|
| `PAGE_SIZE` | 4096 | Bytes per page |
| `TOTAL_PAGES` | 65 536 | Pages in 256 MB |
| bitmap size | 8 KB | Stored in `.bss` |

### API

```c
void  pmm_init(unsigned int kernel_end_phys); // call with (uint)_stack_top
void *pmm_alloc_page(void);                   // returns phys addr, 0 on OOM
void  pmm_free_page(void *pa);
void  pmm_stats(unsigned int *used, unsigned int *total);
```

### Boot-time layout

```
0x80000000  U-Boot
0x80200000  Kernel text + data + BSS (includes 8 KB bitmap)
            + 64 KB stack  (_stack_top ≈ 0x80213000)
0x80213000  First allocatable page   ← pmm_alloc_page() starts here
   ...
0x8FFFF000  Last allocatable page
```

After `pmm_init(_stack_top)` on a freshly booted 256 MB machine:

```
total pages : 65536  (256 MB)
used  pages :   531  (~2 MB)   U-Boot + kernel + bitmap
free  pages : 65005  (~253 MB)
```

## Hardware Model (QEMU virt machine)

| Address        | Device               |
|---------------|----------------------|
| `0x00001000`  | MROM (reset vector)  |
| `0x02000000`  | CLINT (timer)        |
| `0x0c000000`  | PLIC (interrupts)    |
| `0x10000000`  | UART0 (NS16550A)     |
| `0x80000000`  | DRAM start           |
| `0x80000000`  | U-Boot firmware      |
| `0x80200000`  | **Our kernel**       |

## Toolchain Notes

Ubuntu ships `gcc-riscv64-linux-gnu` (rv64 target).
We compile for rv32 by passing:

```
-march=rv32imac_zicsr  -mabi=ilp32  -ffreestanding  -nostdlib
```

This produces valid rv32 machine code. The `-zicsr` extension is needed
so GCC accepts `csrr`/`csrw` instructions.
