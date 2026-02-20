# =============================================================================
# RV32 Hobby OS — Makefile
#
# Toolchain: riscv64-linux-gnu-gcc with -march=rv32imac / -mabi=ilp32
#            (Ubuntu ships only an rv64 cross-compiler; it can still generate
#             rv32 code when given the right -march/-mabi flags and
#             -ffreestanding / -nostdlib.)
#
# QEMU:      qemu-system-riscv32  -machine virt  -bios default
#            "default" bios = OpenSBI, which provides M-mode firmware and
#            jumps to our kernel at 0x80200000 in S-mode.
# =============================================================================

# ----------------------------------------------------------------------------
# Toolchain
# ----------------------------------------------------------------------------
CROSS   := riscv64-linux-gnu-
CC      := $(CROSS)gcc
AS      := $(CROSS)gcc       # use GCC as the assembler front-end
LD      := $(CROSS)gcc       # use GCC as the linker front-end
OBJCOPY := $(CROSS)objcopy
OBJDUMP := $(CROSS)objdump
SIZE    := $(CROSS)size

# ----------------------------------------------------------------------------
# Target architecture flags
#   rv32imac_zicsr  — RV32 base + integer multiply/divide + atomic +
#                     compressed instructions + Zicsr (CSR instructions)
#   ilp32           — 32-bit integer / 32-bit pointers ABI
# ----------------------------------------------------------------------------
ARCH_FLAGS := -march=rv32imac_zicsr -mabi=ilp32

# ----------------------------------------------------------------------------
# Compiler flags
# ----------------------------------------------------------------------------
CFLAGS  := $(ARCH_FLAGS)        \
            -mcmodel=medany     \
            -ffreestanding      \
            -fno-stack-protector\
            -fno-pic            \
            -nostdlib           \
            -nostdinc           \
            -O2                 \
            -Wall               \
            -Wextra             \
            -Ikernel

ASFLAGS := $(ARCH_FLAGS)        \
            -mcmodel=medany     \
            -ffreestanding      \
            -nostdlib           \
            -nostdinc

# ----------------------------------------------------------------------------
# Linker flags
# ----------------------------------------------------------------------------
LDFLAGS := $(ARCH_FLAGS)        \
            -T kernel/linker.ld \
            -nostdlib           \
            -static

# ----------------------------------------------------------------------------
# Source files & objects
# ----------------------------------------------------------------------------
KERNEL_SRCS_S := kernel/start.S
KERNEL_SRCS_C := kernel/uart.c \
                 kernel/main.c

KERNEL_OBJS   := $(KERNEL_SRCS_S:.S=.o) \
                 $(KERNEL_SRCS_C:.c=.o)

# ----------------------------------------------------------------------------
# Output artefacts
# ----------------------------------------------------------------------------
KERNEL_ELF  := kernel.elf
KERNEL_BIN  := kernel.bin
KERNEL_DUMP := kernel.dump

# ----------------------------------------------------------------------------
# QEMU settings
# ----------------------------------------------------------------------------
QEMU        := qemu-system-riscv32
QEMU_MACHINE:= virt
QEMU_BIOS   := none             # no rv32 OpenSBI; we boot directly in M-mode
QEMU_FLAGS  := -machine $(QEMU_MACHINE) \
               -bios $(QEMU_BIOS)       \
               -kernel $(KERNEL_ELF)    \
               -m 128M                  \
               -nographic              \
               -serial mon:stdio

# GDB stub listens on localhost:1234
QEMU_DEBUG_FLAGS := $(QEMU_FLAGS) -S -gdb tcp::1234

# ----------------------------------------------------------------------------
# Default target
# ----------------------------------------------------------------------------
.PHONY: all
all: $(KERNEL_ELF)

# ----------------------------------------------------------------------------
# Link the kernel ELF
# ----------------------------------------------------------------------------
$(KERNEL_ELF): $(KERNEL_OBJS)
	$(LD) $(LDFLAGS) -o $@ $^
	$(SIZE) $@
	@echo ""
	@echo "  Built: $@"
	@echo "  Run:   make run"
	@echo "  Debug: make debug  (then: gdb-multiarch kernel.elf)"

# ----------------------------------------------------------------------------
# Compile assembly files
# ----------------------------------------------------------------------------
%.o: %.S
	$(AS) $(ASFLAGS) -c -o $@ $<

# ----------------------------------------------------------------------------
# Compile C files
# ----------------------------------------------------------------------------
%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# ----------------------------------------------------------------------------
# Raw binary (useful for inspection)
# ----------------------------------------------------------------------------
$(KERNEL_BIN): $(KERNEL_ELF)
	$(OBJCOPY) -O binary $< $@

# ----------------------------------------------------------------------------
# Disassembly listing
# ----------------------------------------------------------------------------
$(KERNEL_DUMP): $(KERNEL_ELF)
	$(OBJDUMP) -D -M no-aliases -M numeric $< > $@

.PHONY: dump
dump: $(KERNEL_DUMP)
	@echo "Disassembly written to $(KERNEL_DUMP)"

# ----------------------------------------------------------------------------
# Run in QEMU (Ctrl-A X to quit)
# ----------------------------------------------------------------------------
.PHONY: run
run: $(KERNEL_ELF)
	@echo "Starting QEMU... (press Ctrl-A then X to quit)"
	$(QEMU) $(QEMU_FLAGS)

# ----------------------------------------------------------------------------
# Run in QEMU with GDB stub (non-interactive)
# ----------------------------------------------------------------------------
.PHONY: run-gdb
run-gdb: $(KERNEL_ELF)
	@echo "QEMU waiting for GDB on :1234 ..."
	$(QEMU) $(QEMU_DEBUG_FLAGS)

# ----------------------------------------------------------------------------
# Debug: launch QEMU in background and attach gdb-multiarch
# ----------------------------------------------------------------------------
.PHONY: debug
debug: $(KERNEL_ELF)
	@echo "Launching QEMU with GDB stub on :1234 (background)"
	$(QEMU) $(QEMU_DEBUG_FLAGS) &
	@sleep 0.5
	gdb-multiarch                          \
	    -ex "set architecture riscv:rv32"  \
	    -ex "target remote :1234"          \
	    -ex "file $(KERNEL_ELF)"           \
	    -ex "break kernel_main"            \
	    -ex "continue"

# ----------------------------------------------------------------------------
# Show symbols and section sizes
# ----------------------------------------------------------------------------
.PHONY: info
info: $(KERNEL_ELF)
	@echo "=== Section sizes ==="
	$(SIZE) --format=SysV $(KERNEL_ELF)
	@echo ""
	@echo "=== Symbol table ==="
	$(CROSS)nm --numeric-sort $(KERNEL_ELF)

# ----------------------------------------------------------------------------
# Clean
# ----------------------------------------------------------------------------
.PHONY: clean
clean:
	rm -f $(KERNEL_OBJS) $(KERNEL_ELF) $(KERNEL_BIN) $(KERNEL_DUMP)
	@echo "Cleaned."
