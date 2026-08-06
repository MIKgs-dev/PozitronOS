#!/bin/bash

if [ $# -ne 2 ]; then
    echo "Usage: $0 <source.c> <output.poz>"
    exit 1
fi

SOURCE="$1"
OUTPUT="$2"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# Компиляция
gcc -m32 -ffreestanding -nostdlib \
    -fno-pic -fno-stack-protector -fno-builtin \
    -fno-asynchronous-unwind-tables \
    -fno-exceptions \
    -I"$SCRIPT_DIR/include" \
    -c "$SOURCE" -o /tmp/prog.o

gcc -m32 -ffreestanding -nostdlib \
    -fno-pic -fno-stack-protector -fno-builtin \
    -fno-asynchronous-unwind-tables \
    -c "$SCRIPT_DIR/crt/crt0.c" -o /tmp/crt0.o

# Линковка с минимальными секциями
ld -m elf_i386 \
    -static \
    --nmagic \
    -Ttext=0x40000000 \
    /tmp/crt0.o /tmp/prog.o \
    -o "$OUTPUT" 2>/dev/null

# Убиваем всё лишнее
strip --strip-all "$OUTPUT" 2>/dev/null

objcopy -R .eh_frame -R .comment -R .eh_frame_hdr -R .got -R .got.plt "$OUTPUT" 2>/dev/null

# Если мусорный сегмент всё ещё есть — пересобираем без него через скрипт
if readelf -l "$OUTPUT" 2>/dev/null | grep -q "3ffff000"; then
    echo "[FATAL] Still has garbage segment, using brute force..."
    # Создаём скрипт линковки на лету
    cat > /tmp/link.ld << 'EOF'
ENTRY(_start)
SECTIONS
{
    . = 0x40000000;
    .text : { *(.text) }
    .rodata : { *(.rodata) }
    /DISCARD/ : { *(*) }
}
EOF
    ld -m elf_i386 -static -T /tmp/link.ld /tmp/crt0.o /tmp/prog.o -o "$OUTPUT" 2>/dev/null
    rm -f /tmp/link.ld
fi

rm -f /tmp/prog.o /tmp/crt0.o

echo "Built: $OUTPUT"