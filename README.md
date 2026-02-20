# RV32 Hobby OS

A RISC-V 32-bit Linux-like hobby OS built from scratch.
Runs on QEMU's `virt` machine (emulated on any host architecture).

## Roadmap

| Phase | Status | Description |
|-------|--------|-------------|
| 2 | ✅ Done | Bare-metal boot → UART "Hello World" |
| 3 | ✅ Done | Physical memory manager (bitmap allocator) |
| 4 | ✅ Done | Sv32 virtual memory + kernel page tables |
| 5 | 🔲 Next | Trap handler + process struct + context switch |
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
    ├── start.S     Assembly entry: stack, BSS clear, M-mode trap vector
    ├── uart.h      UART API
    ├── uart.c      NS16550A UART driver (QEMU virt 0x10000000)
    ├── pmm.h       Physical Memory Manager API
    ├── pmm.c       Bitmap allocator (1 bit/page, 8 KB for 256 MB DRAM)
    ├── vm.h        Sv32 virtual memory API + PTE/satp constants
    ├── vm.c        Page table init, M→S mode switch, Sv32 enable
    └── main.c      kernel_main() + s_mode_entry() — C entry points
```

## Physical Memory Manager (Phase 3)

`kernel/pmm.c` implements a bitmap allocator over the 256 MB DRAM region
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

## Sv32 Virtual Memory (Phase 4)

`kernel/vm.c` sets up the kernel page table and switches from M-mode to
S-mode before enabling virtual memory.

### Sv32 virtual address layout

```
31        22 21        12 11          0
[ VPN[1]  ] [ VPN[0]   ] [ page offset ]
  10 bits     10 bits       12 bits
```

A **megapage** (level-1 leaf entry, `PPN[0] = 0`) maps 4 MB per PTE —
no second-level page table needed.  We use only megapages for the kernel.

### Identity mapping

All regions are identity-mapped (VA == PA) so the kernel runs at the
same addresses before and after enabling Sv32.

| Region | VA / PA | Size | Flags |
|--------|---------|------|-------|
| DRAM | `0x80000000 – 0x8FFFFFFF` | 64 × 4 MB | R/W/X |
| UART | `0x10000000 – 0x103FFFFF` | 1 × 4 MB | R/W |
| CLINT | `0x02000000 – 0x023FFFFF` | 1 × 4 MB | R/W |
| PLIC | `0x0C000000 – 0x0FFFFFFF` | 16 × 4 MB | R/W |

### Boot sequence

```
kernel_main()  [M-mode]
  pmm_init()
  vm_init()           ← populate root_pt with megapage PTEs
  vm_enter_s_mode()   ← configure PMP, medeleg, mstatus.MPP=S, mret
    s_mode_entry()  [S-mode, physical addressing]
      vm_enable_sv32()  ← csrw satp; sfence.vma
      [S-mode, virtual addressing — UART still works via identity map]
```

### PMP note

U-Boot programs PMP entries that deny S-mode access to DRAM by default.
`vm_enter_s_mode()` writes `pmpaddr0 = 0xFFFFFFFF` + `pmpcfg0 = 0x1F`
(NAPOT, R/W/X, full address space) before the `mret`, granting S-mode
unrestricted access.

### API

```c
void         vm_init(void);
void         vm_enter_s_mode(void (*entry)(void));  // noreturn
void         vm_enable_sv32(void);                  // call from S-mode
unsigned int vm_satp_val(void);
unsigned int vm_root_pt_phys(void);
```

### Verified output

```
[vm]   root_pt phys    : 0x80203000
[vm]   entered S-mode  (sstatus = 0x00000020)
[vm]   satp             : 0x80080203    ← MODE=1 (Sv32), PPN=0x80203
[vm]   Sv32 ACTIVE — kernel running under virtual memory
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
