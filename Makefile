# =============================================================================
# RV32 Hobby OS — Makefile
#
# Boot flow:
#   QEMU → U-Boot (boot/u-boot.bin, M-mode, 0x80000000)
#        → boot.scr (FAT on virtio disk)
#        → fatload kernel.bin → 0x80200000
#        → go 0x80200000  (our kernel, M-mode)
#
# Toolchain: riscv64-linux-gnu-gcc with -march=rv32imac_zicsr / -mabi=ilp32
#            (Ubuntu's rv64 cross-compiler can emit rv32 with the right flags.)
# =============================================================================

# ----------------------------------------------------------------------------
# Toolchain
# ----------------------------------------------------------------------------
CROSS   := riscv64-linux-gnu-
CC      := $(CROSS)gcc
AS      := $(CROSS)gcc
LD      := $(CROSS)gcc
OBJCOPY := $(CROSS)objcopy
OBJDUMP := $(CROSS)objdump
SIZE    := $(CROSS)size
MKIMAGE := mkimage

# ----------------------------------------------------------------------------
# Target architecture flags
# ----------------------------------------------------------------------------
ARCH_FLAGS := -march=rv32imac_zicsr -mabi=ilp32

CFLAGS  := $(ARCH_FLAGS)         \
            -mcmodel=medany      \
            -ffreestanding       \
            -fno-stack-protector \
            -fno-pic             \
            -nostdlib            \
            -nostdinc            \
            -O2                  \
            -Wall                \
            -Wextra              \
            -Ikernel

ASFLAGS := $(ARCH_FLAGS)         \
            -mcmodel=medany      \
            -ffreestanding       \
            -nostdlib            \
            -nostdinc

LDFLAGS := $(ARCH_FLAGS)         \
            -T kernel/linker.ld  \
            -nostdlib            \
            -static

# ----------------------------------------------------------------------------
# Source files & objects
# ----------------------------------------------------------------------------
KERNEL_SRCS_S := kernel/start.S
KERNEL_SRCS_C := kernel/uart.c \
                 kernel/main.c

KERNEL_OBJS := $(KERNEL_SRCS_S:.S=.o) $(KERNEL_SRCS_C:.c=.o)

# ----------------------------------------------------------------------------
# Output artefacts
# ----------------------------------------------------------------------------
KERNEL_ELF  := kernel.elf
KERNEL_BIN  := kernel.bin
KERNEL_DUMP := kernel.dump
BOOT_SCR    := boot/boot.scr
BOOT_SOURCE := boot/boot.source
DISK_IMG    := disk.img
UBOOT_BIN   := boot/u-boot.bin

# Partition geometry (matches disk.img layout)
PART_START  := 2048
PART_SECTS  := 28672

# ----------------------------------------------------------------------------
# QEMU settings — U-Boot boot flow
#   -m 256M   : U-Boot's default scriptaddr (0x8c100000) needs >192MB DRAM
#   -bios     : U-Boot binary (M-mode firmware + bootloader)
#   -drive    : virtio block device containing FAT partition with kernel.bin
# ----------------------------------------------------------------------------
QEMU         := qemu-system-riscv32
QEMU_FLAGS   := -machine virt                            \
                -bios $(UBOOT_BIN)                       \
                -m 256M                                  \
                -nographic                               \
                -serial mon:stdio                        \
                -drive file=$(DISK_IMG),format=raw,id=hd0\
                -device virtio-blk-device,drive=hd0

QEMU_DEBUG_FLAGS := $(QEMU_FLAGS) -S -gdb tcp::1234

# ----------------------------------------------------------------------------
# Default target: build everything needed to run
# ----------------------------------------------------------------------------
.PHONY: all
all: $(KERNEL_ELF) $(KERNEL_BIN) $(BOOT_SCR) $(DISK_IMG)
	@echo ""
	@echo "  Boot chain ready."
	@echo "  Run:   make run      (Ctrl-A then X to quit QEMU)"
	@echo "  Debug: make run-gdb  (then attach gdb-multiarch)"

# ----------------------------------------------------------------------------
# Kernel ELF
# ----------------------------------------------------------------------------
$(KERNEL_ELF): $(KERNEL_OBJS)
	$(LD) $(LDFLAGS) -o $@ $^
	$(SIZE) $@

%.o: %.S
	$(AS) $(ASFLAGS) -c -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# ----------------------------------------------------------------------------
# Raw kernel binary (stripped from ELF, placed at 0x80200000 by U-Boot)
# ----------------------------------------------------------------------------
$(KERNEL_BIN): $(KERNEL_ELF)
	$(OBJCOPY) -O binary $< $@

# ----------------------------------------------------------------------------
# U-Boot boot script
#   boot.source  — human-readable script (fatload + go)
#   boot.scr     — compiled U-Boot image (mkimage -T script)
# U-Boot's distro_bootcmd scans FAT partitions for /boot.scr first.
# ----------------------------------------------------------------------------
$(BOOT_SOURCE):
	@echo "# RV32 Hobby OS boot script"                         > $@
	@echo "echo '=== RV32 Hobby OS boot script ==='"           >> $@
	@echo "echo 'Loading kernel.bin to 0x80200000 ...'"        >> $@
	@echo "fatload virtio 0:1 0x80200000 /kernel.bin"          >> $@
	@echo "echo 'Jumping to kernel at 0x80200000 ...'"         >> $@
	@echo "go 0x80200000"                                       >> $@

$(BOOT_SCR): $(BOOT_SOURCE)
	$(MKIMAGE) -T script -A riscv -O linux -C none \
	           -n "RV32 Hobby OS" -d $< $@

# ----------------------------------------------------------------------------
# Disk image (FAT16 on MBR partition, 16 MB total)
#   Layout:
#     Sector 0-2047   : MBR + reserved  (1 MB)
#     Sector 2048+    : FAT16 partition (14 MB)
#   Files on FAT:
#     /kernel.bin     : raw RV32 kernel binary (U-Boot fatloads this)
#     /boot.scr       : compiled U-Boot boot script
# ----------------------------------------------------------------------------
$(DISK_IMG): $(KERNEL_BIN) $(BOOT_SCR)
	@echo "Building disk image $@ ..."
	# 1. Blank 16 MB image
	dd if=/dev/zero of=$@ bs=1M count=16 2>/dev/null
	# 2. MBR partition table: one bootable FAT32 (type=b) partition
	printf 'label: dos\ndisk.img1 : start=$(PART_START), size=$(PART_SECTS), type=b, bootable\n' \
	    | sfdisk $@ >/dev/null 2>&1
	# 3. Extract partition area, format as FAT16, populate, re-insert
	dd if=$@ of=part1.img bs=512 skip=$(PART_START) count=$(PART_SECTS) 2>/dev/null
	mkfs.vfat -F 16 -n "HOBBYOS" part1.img >/dev/null 2>&1
	mmd    -i part1.img ::/boot    2>/dev/null || true
	mcopy  -i part1.img $(KERNEL_BIN) ::/kernel.bin
	mcopy  -i part1.img $(BOOT_SCR)   ::/boot.scr
	dd if=part1.img of=$@ bs=512 seek=$(PART_START) conv=notrunc 2>/dev/null
	rm -f part1.img
	@echo "  Created $@  (FAT16, $(KERNEL_BIN) + $(BOOT_SCR))"

# ----------------------------------------------------------------------------
# Run (Ctrl-A then X to quit QEMU)
# ----------------------------------------------------------------------------
.PHONY: run
run: all
	@echo "Starting QEMU with U-Boot ... (Ctrl-A then X to quit)"
	$(QEMU) $(QEMU_FLAGS)

# ----------------------------------------------------------------------------
# GDB stub mode — QEMU waits for debugger connection on :1234
# ----------------------------------------------------------------------------
.PHONY: run-gdb
run-gdb: all
	@echo "QEMU waiting for GDB on :1234 ..."
	$(QEMU) $(QEMU_DEBUG_FLAGS)

# ----------------------------------------------------------------------------
# Launch QEMU + attach gdb-multiarch automatically
# ----------------------------------------------------------------------------
.PHONY: debug
debug: all
	@echo "Launching QEMU with GDB stub (background) ..."
	$(QEMU) $(QEMU_DEBUG_FLAGS) &
	@sleep 0.5
	gdb-multiarch                         \
	    -ex "set architecture riscv:rv32" \
	    -ex "target remote :1234"         \
	    -ex "file $(KERNEL_ELF)"          \
	    -ex "break kernel_main"           \
	    -ex "continue"

# ----------------------------------------------------------------------------
# Disassembly
# ----------------------------------------------------------------------------
.PHONY: dump
dump: $(KERNEL_ELF)
	$(OBJDUMP) -D -M no-aliases -M numeric $(KERNEL_ELF) > $(KERNEL_DUMP)
	@echo "Disassembly written to $(KERNEL_DUMP)"

# ----------------------------------------------------------------------------
# Symbol / size info
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
	rm -f $(DISK_IMG) $(BOOT_SCR) $(BOOT_SOURCE) part1.img
	@echo "Cleaned."
