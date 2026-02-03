#!/bin/bash

mkdir -p build

echo "Compiling bootloader..."
nasm -f elf32 src/boot/boot.asm -o build/boot.o

echo "Compiling assembly files..."
nasm -f elf32 src/core/gdt_asm.asm -o build/gdt_asm.o
nasm -f elf32 src/core/idt_asm.asm -o build/idt_asm.o
nasm -f elf32 src/core/isr_asm.asm -o build/isr_asm.o
nasm -f elf32 src/core/irq_asm.asm -o build/irq_asm.o

echo "Compiling C files..."
CFLAGS="-m32 -ffreestanding -O1 -Wall -I./include"

# Ядро и утилиты
gcc $CFLAGS -c src/kernel/main.c -o build/main.o
gcc $CFLAGS -c src/kernel/memory.c -o build/memory.o

# Драйверы
gcc $CFLAGS -c src/drivers/serial.c -o build/serial.o
gcc $CFLAGS -c src/drivers/vga.c -o build/vga.o
gcc $CFLAGS -c src/drivers/vesa.c -o build/vesa.o
gcc $CFLAGS -c src/drivers/keyboard.c -o build/keyboard.o
gcc $CFLAGS -c src/drivers/mouse.c -o build/mouse.o
gcc $CFLAGS -c src/drivers/timer.c -o build/timer.o
gcc $CFLAGS -c src/drivers/pic.c -o build/pic.o
gcc $CFLAGS -c src/drivers/ports.c -o build/ports.o
gcc $CFLAGS -c src/drivers/cursor.c -o build/cursor.o
gcc $CFLAGS -c src/drivers/cmos.c -o build/cmos.o
gcc $CFLAGS -c src/drivers/power.c -o build/power.o
gcc $CFLAGS -c src/drivers/ata.c -o build/ata.o
gcc $CFLAGS -c src/drivers/usb.c -o build/usb.o
gcc $CFLAGS -c src/drivers/uhci.c -o build/uhci.o
gcc $CFLAGS -c src/drivers/ehci.c -o build/ehci.o
gcc $CFLAGS -c src/drivers/ohci.c -o build/ohci.o
gcc $CFLAGS -c src/drivers/hid.c -o build/hid.o

# Система
gcc $CFLAGS -c src/core/event.c -o build/event.o
gcc $CFLAGS -c src/core/gdt.c -o build/gdt.o
gcc $CFLAGS -c src/core/idt.c -o build/idt.o
gcc $CFLAGS -c src/core/isr.c -o build/isr.o

# GUI
gcc $CFLAGS -c src/gui/core.c -o build/core.o
gcc $CFLAGS -c src/gui/wm.c -o build/wm.o
gcc $CFLAGS -c src/gui/wget.c -o build/wget.o
gcc $CFLAGS -c src/gui/taskbar.c -o build/taskbar.o
gcc $CFLAGS -c src/gui/shutdown.c -o build/shutdown.o

gcc $CFLAGS -c src/hw/scanner.c -o build/scanner.o

echo "Linking..."
ld -m elf_i386 -T linker.ld -o build/kernel.bin \
    build/boot.o \
    build/main.o \
    build/memory.o \
    build/gdt.o \
    build/gdt_asm.o \
    build/idt.o \
    build/idt_asm.o \
    build/isr.o \
    build/isr_asm.o \
    build/irq_asm.o \
    build/serial.o \
    build/vga.o \
    build/pic.o \
    build/timer.o \
    build/keyboard.o \
    build/mouse.o \
    build/vesa.o \
    build/ports.o \
    build/cursor.o \
    build/event.o \
    build/core.o \
    build/wm.o \
    build/wget.o \
    build/taskbar.o \
    build/cmos.o \
    build/scanner.o \
    build/power.o \
    build/shutdown.o \
    build/ata.o \
    build/usb.o \
    build/uhci.o \
    build/ehci.o \
    build/ohci.o \
    build/hid.o \
    -nostdlib

echo "Creating ISO..."
mkdir -p iso/boot/grub
cp build/kernel.bin iso/boot/
cp grub/grub.cfg iso/boot/grub/
grub-mkrescue -o pozitron.iso iso/

echo "Build complete!"
qemu-system-i386 -cdrom pozitron.iso -serial stdio -usb -device usb-tablet