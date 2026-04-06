#!/bin/bash

# Очистка предыдущих монтирований и loop-устройств
sudo umount main_system/mnt_system 2>/dev/null || true
sudo losetup -d /dev/loop0 2>/dev/null || true
sudo losetup -d /dev/loop1 2>/dev/null || true
rmdir main_system/mnt_system 2>/dev/null || true

#*********************************
#This is start a build main_system
#*********************************

mkdir -p main_system/build

echo "1/11 [Main_system]Compiling kernel_enter..."
nasm -f elf32 main_system/src/boot/boot.asm -o main_system/build/boot.o

echo "2/11 [Main_system]Compiling .asm files..."
nasm -f elf32 main_system/src/core/gdt_asm.asm -o main_system/build/gdt_asm.o
nasm -f elf32 main_system/src/core/idt_asm.asm -o main_system/build/idt_asm.o
nasm -f elf32 main_system/src/core/isr_asm.asm -o main_system/build/isr_asm.o
nasm -f elf32 main_system/src/core/irq_asm.asm -o main_system/build/irq_asm.o

echo "3/11 [Main_system]Compiling С files..."
CFLAGS="-m32 -ffreestanding -w -O1 -Wall -I./main_system/include"

# Ядро и утилиты
gcc $CFLAGS -c main_system/src/kernel/main.c -o main_system/build/main.o
gcc $CFLAGS -c main_system/src/kernel/memory.c -o main_system/build/memory.o
gcc $CFLAGS -c main_system/src/kernel/multiboot.c -o main_system/build/multiboot.o
gcc $CFLAGS -c main_system/src/kernel/logo.c -o main_system/build/logo.o
gcc $CFLAGS -c main_system/src/kernel/scheduler.c -o main_system/build/scheduler.o
gcc $CFLAGS -c main_system/src/kernel/paging.c -o main_system/build/paging.o
gcc $CFLAGS -c main_system/src/kernel/userspace.c -o main_system/build/userspace.o
gcc $CFLAGS -c main_system/src/kernel/device.c -o main_system/build/device.o
gcc $CFLAGS -c main_system/src/kernel/callout.c -o main_system/build/callout.o
gcc $CFLAGS -c main_system/src/kernel/mutex.c -o main_system/build/mutex.o
gcc $CFLAGS -c main_system/src/kernel/notif.c -o main_system/build/notif.o
gcc $CFLAGS -c main_system/src/kernel/timer_utils.c -o main_system/build/timer_utils.o
gcc $CFLAGS -c main_system/src/kernel/semaphore.c -o main_system/build/semaphore.o

# Драйверы
gcc $CFLAGS -c main_system/src/drivers/serial.c -o main_system/build/serial.o
gcc $CFLAGS -c main_system/src/drivers/vga.c -o main_system/build/vga.o
gcc $CFLAGS -c main_system/src/drivers/vesa.c -o main_system/build/vesa.o
gcc $CFLAGS -c main_system/src/drivers/keyboard.c -o main_system/build/keyboard.o
gcc $CFLAGS -c main_system/src/drivers/mouse.c -o main_system/build/mouse.o
gcc $CFLAGS -c main_system/src/drivers/timer.c -o main_system/build/timer.o
gcc $CFLAGS -c main_system/src/drivers/pic.c -o main_system/build/pic.o
gcc $CFLAGS -c main_system/src/drivers/ports.c -o main_system/build/ports.o
gcc $CFLAGS -c main_system/src/drivers/cursor.c -o main_system/build/cursor.o
gcc $CFLAGS -c main_system/src/drivers/cmos.c -o main_system/build/cmos.o
gcc $CFLAGS -c main_system/src/drivers/power.c -o main_system/build/power.o
gcc $CFLAGS -c main_system/src/hw/scanner.c -o main_system/build/scanner.o
gcc $CFLAGS -c main_system/src/drivers/pci.c -o main_system/build/pci.o
gcc $CFLAGS -c main_system/src/drivers/ata.c -o main_system/build/ata.o
gcc $CFLAGS -c main_system/src/drivers/ahci.c -o main_system/build/ahci.o
gcc $CFLAGS -c main_system/src/drivers/disk.c -o main_system/build/disk.o

# Система
gcc $CFLAGS -c main_system/src/core/event.c -o main_system/build/event.o
gcc $CFLAGS -c main_system/src/core/gdt.c -o main_system/build/gdt.o
gcc $CFLAGS -c main_system/src/core/idt.c -o main_system/build/idt.o
gcc $CFLAGS -c main_system/src/core/isr.c -o main_system/build/isr.o

# GUI
gcc $CFLAGS -c main_system/src/gui/core.c -o main_system/build/core.o
gcc $CFLAGS -c main_system/src/gui/wm.c -o main_system/build/wm.o
gcc $CFLAGS -c main_system/src/gui/wget.c -o main_system/build/wget.o
gcc $CFLAGS -c main_system/src/gui/taskbar.c -o main_system/build/taskbar.o
gcc $CFLAGS -c main_system/src/gui/shutdown.c -o main_system/build/shutdown.o
gcc $CFLAGS -c main_system/src/gui/setup.c -o main_system/build/setup.o

# Библиотеки
gcc $CFLAGS -c main_system/src/lib/string.c -o main_system/build/string.o
gcc $CFLAGS -c main_system/src/lib/mini_printf.c -o main_system/build/mini_printf.o
gcc $CFLAGS -c main_system/src/lib/math.c -o main_system/build/math.o

# FS
gcc $CFLAGS -c main_system/src/fs/vfs.c -o main_system/build/vfs.o
gcc $CFLAGS -c main_system/src/fs/ext2.c -o main_system/build/ext2.o

echo "4/11 [Main_system]Linking..."
ld -m elf_i386 -T main_system/linker.ld -o main_system/build/kernel.bin \
    main_system/build/boot.o \
    main_system/build/main.o \
    main_system/build/memory.o \
    main_system/build/multiboot.o \
    main_system/build/logo.o \
    main_system/build/mutex.o \
    main_system/build/semaphore.o \
    main_system/build/callout.o \
    main_system/build/timer_utils.o \
    main_system/build/device.o \
    main_system/build/gdt.o \
    main_system/build/gdt_asm.o \
    main_system/build/idt.o \
    main_system/build/idt_asm.o \
    main_system/build/isr.o \
    main_system/build/isr_asm.o \
    main_system/build/irq_asm.o \
    main_system/build/serial.o \
    main_system/build/vga.o \
    main_system/build/pic.o \
    main_system/build/timer.o \
    main_system/build/keyboard.o \
    main_system/build/mouse.o \
    main_system/build/vesa.o \
    main_system/build/ports.o \
    main_system/build/cursor.o \
    main_system/build/event.o \
    main_system/build/core.o \
    main_system/build/wm.o \
    main_system/build/wget.o \
    main_system/build/taskbar.o \
    main_system/build/cmos.o \
    main_system/build/scanner.o \
    main_system/build/power.o \
    main_system/build/shutdown.o \
    main_system/build/string.o \
    main_system/build/math.o \
    main_system/build/pci.o \
    main_system/build/scheduler.o \
    main_system/build/paging.o \
    main_system/build/userspace.o \
    main_system/build/mini_printf.o \
    main_system/build/notif.o \
    main_system/build/ata.o \
    main_system/build/ahci.o \
    main_system/build/disk.o \
    main_system/build/ext2.o \
    main_system/build/vfs.o \
    main_system/build/setup.o \
    -nostdlib

cp main_system/build/kernel.bin main_system/

# ==============================================
# СОЗДАНИЕ ЧИСТОГО КОНФИГА LIMINE
# ==============================================

echo "5/11 [Main_system] Creating clean limine.conf..."

cat > main_system/Limine/limine.conf << 'EOF'
timeout:0
default_entry:0
graphics:yes

/PozitronOS
    protocol:multiboot1
    kernel_path:boot(1):/kernel.bin
EOF

if [ ! -f "main_system/Limine/limine.conf" ]; then
    echo "  ERROR: Failed to create limine.conf!"
    exit 1
fi

echo "  limine.conf created successfully"

# ==============================================
# СОЗДАНИЕ ОБРАЗА С LIMINE
# ==============================================

echo "6/11 [Main_system]Creating img with Limine (FAT32 + EXT2)..."

SYSTEM_IMG_SIZE=128
FAT32_SIZE_MB=64

dd if=/dev/zero of=main_system/system.img bs=1M count=$SYSTEM_IMG_SIZE status=progress

LOOP_DEV=$(sudo losetup --find --show --partscan main_system/system.img)

sudo parted -s "$LOOP_DEV" mklabel msdos
sudo parted -s "$LOOP_DEV" mkpart primary fat32 1MiB ${FAT32_SIZE_MB}MiB
sudo parted -s "$LOOP_DEV" set 1 boot on
sudo parted -s "$LOOP_DEV" mkpart primary ${FAT32_SIZE_MB}MiB 100%

sleep 2
sudo partprobe "$LOOP_DEV"
sleep 2

sudo mkfs.fat -F32 -s 1 -f 2 -S 512 -n "LIMINE" "${LOOP_DEV}p1"
sudo mkfs.ext2 -F -L "ROOTFS" "${LOOP_DEV}p2"

mkdir -p main_system/mnt_system
sudo mount "${LOOP_DEV}p1" main_system/mnt_system

sudo mkdir -p main_system/mnt_system/EFI/BOOT
sudo cp main_system/kernel.bin main_system/mnt_system/
sudo cp main_system/Limine/limine.conf main_system/mnt_system/
sudo cp main_system/Limine/limine-bios.sys main_system/mnt_system/

# Дублируем конфиг во все возможные места
sudo mkdir -p main_system/mnt_system/limine
sudo cp main_system/Limine/limine.conf main_system/mnt_system/limine/
sudo mkdir -p main_system/mnt_system/boot
sudo cp main_system/Limine/limine.conf main_system/mnt_system/boot/
sudo mkdir -p main_system/mnt_system/boot/limine
sudo cp main_system/Limine/limine.conf main_system/mnt_system/boot/limine/
sudo cp main_system/Limine/limine.conf main_system/mnt_system/EFI/BOOT/

if [ -f "main_system/Limine/BOOTX64.EFI" ]; then
    sudo cp main_system/Limine/BOOTX64.EFI main_system/mnt_system/EFI/BOOT/
fi

sync
sleep 1

sudo umount main_system/mnt_system

sudo mount "${LOOP_DEV}p2" main_system/mnt_system

if [ -d "main_system/rootfs" ]; then
    echo "Copying rootfs contents:"
    ls -la main_system/rootfs/
    sudo cp -r main_system/rootfs/. main_system/mnt_system/
fi

sync
sudo umount main_system/mnt_system

echo "  Installing Limine..."
sudo main_system/Limine/limine bios-install --force "$LOOP_DEV"

if [ $? -ne 0 ]; then
    echo "  ERROR: Limine installation failed!"
    sudo losetup -d "$LOOP_DEV"
    exit 1
fi

MBR_SIG=$(sudo dd if="$LOOP_DEV" bs=512 count=1 2>/dev/null | tail -c 2 | hexdump -C)
if echo "$MBR_SIG" | grep -q "55 aa"; then
    echo "  MBR signature OK"
else
    echo "  ERROR: MBR signature missing!"
    sudo losetup -d "$LOOP_DEV"
    exit 1
fi

sudo losetup -d "$LOOP_DEV"
rmdir main_system/mnt_system 2>/dev/null || true

echo "  System image created: main_system/system.img ($SYSTEM_IMG_SIZE MB)"


#************************************
#This is start a build installer_syst
#************************************

mkdir -p installer_syst/build

echo "7/11 [Installer_syst]Compiling kernel_enter..."
nasm -f elf32 installer_syst/src/boot/boot.asm -o installer_syst/build/boot.o

echo "8/11 [Installer_syst]Compiling .asm files..."
nasm -f elf32 installer_syst/src/core/gdt_asm.asm -o installer_syst/build/gdt_asm.o
nasm -f elf32 installer_syst/src/core/idt_asm.asm -o installer_syst/build/idt_asm.o
nasm -f elf32 installer_syst/src/core/isr_asm.asm -o installer_syst/build/isr_asm.o
nasm -f elf32 installer_syst/src/core/irq_asm.asm -o installer_syst/build/irq_asm.o

echo "9/11 [Installer_syst]Compiling C files..."
CFLAGS="-m32 -ffreestanding -w -O1 -Wall -I./installer_syst/include"

# Ядро и утилиты
gcc $CFLAGS -c installer_syst/src/kernel/main.c -o installer_syst/build/main.o
gcc $CFLAGS -c installer_syst/src/kernel/memory.c -o installer_syst/build/memory.o
gcc $CFLAGS -c installer_syst/src/kernel/multiboot.c -o installer_syst/build/multiboot.o
gcc $CFLAGS -c installer_syst/src/kernel/logo.c -o installer_syst/build/logo.o
gcc $CFLAGS -c installer_syst/src/kernel/scheduler.c -o installer_syst/build/scheduler.o
gcc $CFLAGS -c installer_syst/src/kernel/paging.c -o installer_syst/build/paging.o
gcc $CFLAGS -c installer_syst/src/kernel/userspace.c -o installer_syst/build/userspace.o
gcc $CFLAGS -c installer_syst/src/kernel/device.c -o installer_syst/build/device.o
gcc $CFLAGS -c installer_syst/src/kernel/callout.c -o installer_syst/build/callout.o
gcc $CFLAGS -c installer_syst/src/kernel/mutex.c -o installer_syst/build/mutex.o
gcc $CFLAGS -c installer_syst/src/kernel/notif.c -o installer_syst/build/notif.o
gcc $CFLAGS -c installer_syst/src/kernel/timer_utils.c -o installer_syst/build/timer_utils.o

# Драйверы
gcc $CFLAGS -c installer_syst/src/drivers/serial.c -o installer_syst/build/serial.o
gcc $CFLAGS -c installer_syst/src/drivers/vga.c -o installer_syst/build/vga.o
gcc $CFLAGS -c installer_syst/src/drivers/vesa.c -o installer_syst/build/vesa.o
gcc $CFLAGS -c installer_syst/src/drivers/keyboard.c -o installer_syst/build/keyboard.o
gcc $CFLAGS -c installer_syst/src/drivers/mouse.c -o installer_syst/build/mouse.o
gcc $CFLAGS -c installer_syst/src/drivers/timer.c -o installer_syst/build/timer.o
gcc $CFLAGS -c installer_syst/src/drivers/pic.c -o installer_syst/build/pic.o
gcc $CFLAGS -c installer_syst/src/drivers/ports.c -o installer_syst/build/ports.o
gcc $CFLAGS -c installer_syst/src/drivers/cursor.c -o installer_syst/build/cursor.o
gcc $CFLAGS -c installer_syst/src/drivers/cmos.c -o installer_syst/build/cmos.o
gcc $CFLAGS -c installer_syst/src/drivers/power.c -o installer_syst/build/power.o
gcc $CFLAGS -c installer_syst/src/hw/scanner.c -o installer_syst/build/scanner.o
gcc $CFLAGS -c installer_syst/src/drivers/pci.c -o installer_syst/build/pci.o
gcc $CFLAGS -c installer_syst/src/drivers/ata.c -o installer_syst/build/ata.o
gcc $CFLAGS -c installer_syst/src/drivers/ahci.c -o installer_syst/build/ahci.o
gcc $CFLAGS -c installer_syst/src/drivers/disk.c -o installer_syst/build/disk.o

# Система
gcc $CFLAGS -c installer_syst/src/core/event.c -o installer_syst/build/event.o
gcc $CFLAGS -c installer_syst/src/core/gdt.c -o installer_syst/build/gdt.o
gcc $CFLAGS -c installer_syst/src/core/idt.c -o installer_syst/build/idt.o
gcc $CFLAGS -c installer_syst/src/core/isr.c -o installer_syst/build/isr.o

# GUI
gcc $CFLAGS -c installer_syst/src/gui/core.c -o installer_syst/build/core.o
gcc $CFLAGS -c installer_syst/src/gui/wm.c -o installer_syst/build/wm.o
gcc $CFLAGS -c installer_syst/src/gui/wget.c -o installer_syst/build/wget.o
gcc $CFLAGS -c installer_syst/src/gui/taskbar.c -o installer_syst/build/taskbar.o
gcc $CFLAGS -c installer_syst/src/gui/shutdown.c -o installer_syst/build/shutdown.o

# Библиотеки
gcc $CFLAGS -c installer_syst/src/lib/string.c -o installer_syst/build/string.o
gcc $CFLAGS -c installer_syst/src/lib/mini_printf.c -o installer_syst/build/mini_printf.o
gcc $CFLAGS -c installer_syst/src/lib/math.c -o installer_syst/build/math.o

echo "10/11 [Installer_syst]Linking..."
ld -m elf_i386 -T installer_syst/linker.ld -o installer_syst/build/kernel.bin \
    installer_syst/build/boot.o \
    installer_syst/build/main.o \
    installer_syst/build/memory.o \
    installer_syst/build/multiboot.o \
    installer_syst/build/logo.o \
    installer_syst/build/mutex.o \
    installer_syst/build/callout.o \
    installer_syst/build/timer_utils.o \
    installer_syst/build/device.o \
    installer_syst/build/gdt.o \
    installer_syst/build/gdt_asm.o \
    installer_syst/build/idt.o \
    installer_syst/build/idt_asm.o \
    installer_syst/build/isr.o \
    installer_syst/build/isr_asm.o \
    installer_syst/build/irq_asm.o \
    installer_syst/build/serial.o \
    installer_syst/build/vga.o \
    installer_syst/build/pic.o \
    installer_syst/build/timer.o \
    installer_syst/build/keyboard.o \
    installer_syst/build/mouse.o \
    installer_syst/build/vesa.o \
    installer_syst/build/ports.o \
    installer_syst/build/cursor.o \
    installer_syst/build/event.o \
    installer_syst/build/core.o \
    installer_syst/build/wm.o \
    installer_syst/build/wget.o \
    installer_syst/build/taskbar.o \
    installer_syst/build/cmos.o \
    installer_syst/build/scanner.o \
    installer_syst/build/power.o \
    installer_syst/build/shutdown.o \
    installer_syst/build/string.o \
    installer_syst/build/math.o \
    installer_syst/build/pci.o \
    installer_syst/build/scheduler.o \
    installer_syst/build/paging.o \
    installer_syst/build/userspace.o \
    installer_syst/build/mini_printf.o \
    installer_syst/build/notif.o \
    installer_syst/build/ata.o \
    installer_syst/build/ahci.o \
    installer_syst/build/disk.o \
    -nostdlib

echo "11/11 [Installer_syst]Creating iso..."
mkdir -p installer_syst/iso/boot/grub
cp installer_syst/build/kernel.bin installer_syst/iso/boot/
cp main_system/system.img installer_syst/iso/boot/
cp installer_syst/grub/grub.cfg installer_syst/iso/boot/grub/
grub-mkrescue -o pozitron.iso installer_syst/iso/

echo "Cleaning up..."
rm -f main_system/system.img
rm -rf installer_syst/iso
rm -rf main_system/iso
rm -rf main_system/build
rm -rf installer_syst/build

echo "Build complete!"