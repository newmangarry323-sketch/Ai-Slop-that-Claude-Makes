# Plasmix build.
#
#   make            build build/plasmix.bin (the x86-64 kernel) and build/plasmix.elf
#   make run        boot it in QEMU            (needs qemu-system-x86_64)
#   make iso        make a bootable CD image   (needs grub-mkrescue + xorriso)
#   make host       build build/plasmix-tty, the same desktop inside a Linux terminal
#   make shell      build build/plasmix-sh, just the shell and file system on the command line
#   make test       build and run the scripted UI tests on Linux
#   make emutest    boot the real kernel in the Unicorn CPU emulator (needs python unicorn)
#   make demo       record a tour of the real kernel and render PNG screenshots + a GIF
#                   into build/demo (needs python unicorn, Chromium, ImageMagick, ffmpeg)
#   make clean

CC      ?= gcc
LD      ?= ld
OBJCOPY ?= objcopy
PYTHON  ?= python3

CORE_SRC := src/lib.c src/gfx.c src/vfs.c src/shell.c src/wm.c src/apps.c \
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

.PHONY: all run iso host shell test emutest demo clean

all: build/plasmix.bin

build/kernel/%.o: %.c src/*.h
	@mkdir -p $(dir $@)
	$(CC) $(KCFLAGS) -c $< -o $@

build/kernel/boot/boot.o: boot/boot.S
	@mkdir -p $(dir $@)
	$(CC) -m64 -c $< -o $@

build/plasmix.elf: $(KOBJS) boot/linker.ld
	$(LD) -m elf_x86_64 -nostdlib -z max-page-size=0x1000 -z noexecstack \
	      -T boot/linker.ld -o $@ $(KOBJS)

# A flat image: what the boot loader actually loads (see "a.out kludge" in boot.S).
build/plasmix.bin: build/plasmix.elf
	$(OBJCOPY) -O binary $< $@
	@echo "Built $@ ($$(stat -c %s $@) bytes)"

run: build/plasmix.bin
	qemu-system-x86_64 -kernel build/plasmix.bin -m 128M

iso: build/plasmix.bin
	@mkdir -p build/iso/boot/grub
	cp build/plasmix.bin build/iso/boot/plasmix.bin
	printf 'set timeout=0\nmenuentry "Plasmix" {\n  multiboot /boot/plasmix.bin\n  boot\n}\n' \
	    > build/iso/boot/grub/grub.cfg
	grub-mkrescue -o build/plasmix.iso build/iso

host: build/plasmix-tty

build/plasmix-tty: $(CORE_SRC) host/host_tty.c host/cp437.h src/*.h
	@mkdir -p build
	$(CC) -std=gnu11 -O2 $(WARN) -o $@ $(CORE_SRC) host/host_tty.c

shell: build/plasmix-sh

build/plasmix-sh: $(CORE_SRC) host/sh_repl.c src/*.h
	@mkdir -p build
	$(CC) -std=gnu11 -O2 $(WARN) -o $@ $(CORE_SRC) host/sh_repl.c

build/test_ui: $(CORE_SRC) host/test_ui.c host/cp437.h src/*.h
	@mkdir -p build
	$(CC) $(HCFLAGS) -o $@ $(CORE_SRC) host/test_ui.c

test: build/test_ui
	./build/test_ui

emutest: build/plasmix.bin
	$(PYTHON) tools/emu_boot_test.py build/plasmix.elf build/plasmix.bin

demo: build/plasmix.bin
	$(PYTHON) tools/demo_tour.py build/plasmix.bin build/demo
	$(PYTHON) tools/vga_render.py build/demo/png build/demo/*.vga
	ffmpeg -loglevel error -y -framerate 0.6 -pattern_type glob -i 'build/demo/png/*.png' \
	    -vf "scale=640:-1:flags=neighbor,split[a][b];[a]palettegen=max_colors=32[p];[b][p]paletteuse=dither=none" \
	    -loop 0 build/demo/tour.gif

clean:
	rm -rf build
