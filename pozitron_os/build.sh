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
CFLAGS="-m32 -ffreestanding -w -O1 -Wall -I./include"

# Ядро и утилиты
gcc $CFLAGS -c src/kernel/main.c -o build/main.o
gcc $CFLAGS -c src/kernel/memory.c -o build/memory.o
gcc $CFLAGS -c src/kernel/multiboot.c -o build/multiboot.o
gcc $CFLAGS -c src/kernel/logo.c -o build/logo.o
gcc $CFLAGS -c src/kernel/scheduler.c -o build/scheduler.o
gcc $CFLAGS -c src/kernel/paging.c -o build/paging.o
gcc $CFLAGS -c src/kernel/userspace.c -o build/userspace.o
gcc $CFLAGS -c src/kernel/device.c -o build/device.o
gcc $CFLAGS -c src/kernel/callout.c -o build/callout.o
gcc $CFLAGS -c src/kernel/mutex.c -o build/mutex.o

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
gcc $CFLAGS -c src/hw/scanner.c -o build/scanner.o
gcc $CFLAGS -c src/drivers/pci.c -o build/pci.o
gcc $CFLAGS -c src/drivers/ahci.c -o build/ahci.o

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

# Библиотеки
gcc $CFLAGS -c src/lib/string.c -o build/string.o
gcc $CFLAGS -c src/lib/mini_printf.c -o build/mini_printf.o

# USB
gcc $CFLAGS -c src/drivers/usb/scsi_cmds.c -o build/scsi_cmds.o
gcc $CFLAGS -c src/drivers/usb/usb_scsi_low.c -o build/usb_scsi_low.o
gcc $CFLAGS -c src/drivers/usb/usb.c -o build/usb.o
gcc $CFLAGS -c src/drivers/usb/uhci.c -o build/uhci.o
gcc $CFLAGS -c src/drivers/usb/ohci.c -o build/ohci.o
gcc $CFLAGS -c src/drivers/usb/usb_x.c -o build/usb_x.o

echo "Linking..."
ld -m elf_i386 -T linker.ld -o build/kernel.bin \
    build/boot.o \
    build/main.o \
    build/memory.o \
    build/multiboot.o \
    build/logo.o \
    build/mutex.o \
    build/callout.o \
    build/device.o \
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
    build/string.o \
    build/pci.o \
    build/scheduler.o \
    build/paging.o \
    build/userspace.o \
    build/mini_printf.o \
    build/ahci.o \
    build/scsi_cmds.o \
    build/usb_scsi_low.o \
    build/usb.o \
    build/uhci.o \
    build/ohci.o \
    build/usb_x.o \
    -nostdlib

echo "Creating ISO..."
mkdir -p iso/boot/grub
cp build/kernel.bin iso/boot/
cp grub/grub.cfg iso/boot/grub/
grub-mkrescue -o pozitron.iso iso/

echo "Build complete!"
qemu-system-x86_64 -cdrom pozitron.iso -serial stdio