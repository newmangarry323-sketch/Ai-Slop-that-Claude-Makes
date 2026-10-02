# SkarletOS build.
#
#   make            build build/skarletos.bin (the x86-64 kernel) and build/skarletos.elf
#   make iso        make build/skarletos.iso, a CD image that boots on BIOS and UEFI
#                   PCs and VMs such as VMware (downloads the Limine boot loader once)
#   make run        boot the ISO in QEMU (needs qemu-system-x86_64)
#   make run-kernel boot the kernel directly with QEMU's -kernel option (no ISO)
#   make session    build build/skarlet-session, SkarletOS as the desktop of a Linux (X11)
#                   system (needs libx11-dev and libxext-dev; libpam0g-dev for passwords)
#   make shell      build build/skarlet-sh, just the shell and file system on the command line
#   make test       build and run the scripted UI tests on Linux (screenshots in build/screens)
#   make qemutest   boot the ISO in QEMU, press keys and check the screen
#   make demo       a 21-step tour of the ISO in QEMU: checked PNG screenshots + a GIF in
#                   build/tour (needs QEMU and ImageMagick; ffmpeg for the GIF)
#   make fonts      regenerate src/font_data.c from the DejaVu fonts (needs FreeType)
#   make clean
#
# Set QEMU=/path/to/qemu-system-x86_64 to use another QEMU, and OVMF=/path/to/code.fd to
# boot "make run" / "make qemutest" with UEFI firmware instead of the BIOS.

CC      ?= gcc
LD      ?= ld
OBJCOPY ?= objcopy
PYTHON  ?= python3
QEMU    ?= qemu-system-x86_64

CORE_SRC := src/lib.c src/gfx.c src/font_data.c src/vfs.c src/shell.c src/wm.c src/apps.c \
            src/plasmoids.c src/workspace.c
WARN     := -Wall -Wextra -Wno-unused-parameter

# Kernel flags, each one matters:
#   -ffreestanding        no hosted C library is available
#   -mno-red-zone         the x86-64 ABI "red zone" is unsafe in kernels
#   -mgeneral-regs-only   no SSE/FPU code: we never enable those units
#   -fno-pic -fno-pie     fixed addresses; there is no dynamic loader
KCFLAGS := -std=gnu11 -O2 $(WARN) -m64 -ffreestanding -fno-stack-protector \
           -fno-pic -fno-pie -mno-red-zone -mgeneral-regs-only \
           -fno-asynchronous-unwind-tables -fno-builtin
KOBJS   := $(patsubst %.c,build/kernel/%.o,$(CORE_SRC) kernel/arch_x86_64.c) \
           build/kernel/boot/boot.o

HCFLAGS := -std=gnu11 -O1 -g $(WARN) -fsanitize=address,undefined -fno-omit-frame-pointer

.PHONY: all run run-kernel iso session shell test qemutest demo fonts clean

all: build/skarletos.bin

build/kernel/%.o: %.c src/*.h
	@mkdir -p $(dir $@)
	$(CC) $(KCFLAGS) -c $< -o $@

build/kernel/boot/boot.o: boot/boot.S
	@mkdir -p $(dir $@)
	$(CC) -m64 -c $< -o $@

build/skarletos.elf: $(KOBJS) boot/linker.ld
	$(LD) -m elf_x86_64 -nostdlib -z max-page-size=0x1000 -z noexecstack \
	      -T boot/linker.ld -o $@ $(KOBJS)

# A flat image: what the boot loader actually loads (see "a.out kludge" in boot.S).
build/skarletos.bin: build/skarletos.elf
	$(OBJCOPY) -O binary $< $@
	@echo "Built $@ ($$(stat -c %s $@) bytes)"

run-kernel: build/skarletos.bin
	$(QEMU) -kernel build/skarletos.bin -m 256M -vga vmware -serial stdio

# The Limine boot loader's prebuilt binaries (BSD-2-Clause licence).  Set
# LIMINE_DIR to use a copy you already have.
LIMINE_VERSION ?= v11.4.1
LIMINE_DIR     ?= build/limine

$(LIMINE_DIR)/limine-bios-cd.bin:
	git clone --depth=1 --branch $(LIMINE_VERSION)-binary \
	    https://github.com/limine-bootloader/limine.git $(LIMINE_DIR)

iso: build/skarletos.iso

build/skarletos.iso: build/skarletos.bin boot/limine.conf tools/mkiso.py $(LIMINE_DIR)/limine-bios-cd.bin
	rm -rf build/iso
	mkdir -p build/iso/boot/limine
	cp build/skarletos.bin build/iso/boot/skarletos.bin
	cp boot/limine.conf $(addprefix $(LIMINE_DIR)/,limine-bios.sys limine-bios-cd.bin \
	    limine-uefi-cd.bin) build/iso/boot/limine/
	cp $(LIMINE_DIR)/LICENSE build/iso/boot/limine/license.txt
	$(PYTHON) tools/mkiso.py $@ build/iso boot/limine/limine-bios-cd.bin \
	    boot/limine/limine-uefi-cd.bin

# -vga vmware is the same VMware SVGA II adapter VMware emulates; try -vga std too.
run: build/skarletos.iso
	$(QEMU) -cdrom build/skarletos.iso -m 256M -vga vmware -serial stdio \
	    $(if $(OVMF),-drive if=pflash$(comma)format=raw$(comma)readonly=on$(comma)file=$(OVMF))
comma := ,# a literal comma, for use inside $(if ...)

# The Linux session: the same desktop, as the X window manager of a Linux
# system.  Bigger screens and more windows than the kernel allows.
LINUX_SRC := $(wildcard linux/*.c)
# libvterm (linux/libvterm, MIT licence) is upstream code: built as it is,
# without our warning flags.
VTERM_OBJ := $(patsubst linux/libvterm/src/%.c,build/libvterm/%.o,$(wildcard linux/libvterm/src/*.c))
LCFLAGS   := -std=gnu11 -O2 -g $(WARN) -DSKARLET_LINUX -DGFX_MAX_W=3840 -DGFX_MAX_H=2160 \
             -DMAX_WIN=32 -DVFS_MAX_NODES=4096 -DVFS_NAME_MAX=256 -DVFS_FILE_MAX=262144 \
             -DVFS_PATH_MAX=512
# On Linux the real disk replaces the in-memory file system.
LINUX_CORE := $(filter-out src/vfs.c,$(CORE_SRC))
LLIBS     := -lX11 -lXext
ifneq ($(wildcard /usr/include/security/pam_appl.h),)
LCFLAGS += -DHAVE_PAM
LLIBS   += -lpam
endif

session: build/skarlet-session

build/libvterm/%.o: linux/libvterm/src/%.c
	@mkdir -p build/libvterm
	$(CC) -std=c99 -O2 -w -Ilinux/libvterm/include -c $< -o $@

build/skarlet-session: $(LINUX_CORE) $(LINUX_SRC) $(VTERM_OBJ) linux/*.h src/*.h
	@mkdir -p build
	$(CC) $(LCFLAGS) -Ilinux/libvterm/include -o $@ $(LINUX_CORE) $(LINUX_SRC) $(VTERM_OBJ) \
	    $(LLIBS) -lutil

shell: build/skarlet-sh

build/skarlet-sh: $(CORE_SRC) host/sh_repl.c src/*.h
	@mkdir -p build
	$(CC) -std=gnu11 -O2 $(WARN) -o $@ $(CORE_SRC) host/sh_repl.c

build/test_ui: $(CORE_SRC) host/test_ui.c src/*.h
	@mkdir -p build
	$(CC) $(HCFLAGS) -o $@ $(CORE_SRC) host/test_ui.c

test: build/test_ui
	./build/test_ui

qemutest: build/skarletos.iso
	$(PYTHON) tools/qemu_test.py --qemu $(QEMU) $(if $(OVMF),--uefi $(OVMF)) build/skarletos.iso

demo: build/skarletos.iso
	$(PYTHON) tools/qemu_tour.py --qemu $(QEMU) $(if $(OVMF),--uefi $(OVMF)) \
	    build/skarletos.iso build/skarletos.elf build/tour
	ffmpeg -loglevel error -y -framerate 0.6 -pattern_type glob -i 'build/tour/*.png' \
	    -vf "scale=960:-1:flags=lanczos,split[a][b];[a]palettegen=max_colors=128[p];[b][p]paletteuse=dither=bayer" \
	    -loop 0 build/tour/tour.gif

build/mkfont: tools/mkfont.c
	@mkdir -p build
	$(CC) -O2 $< -o $@ $$(pkg-config --cflags --libs freetype2)

fonts: build/mkfont
	./build/mkfont /usr/share/fonts/truetype/dejavu > src/font_data.c

clean:
	rm -rf build
