#!/bin/bash

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
DRIVERS_SRC="$SCRIPT_DIR/Drivers"
DRIVERS_DST="$PROJECT_ROOT/main_system/rootfs/PozitronOS/Sys32/Sysdriv"
INCLUDE_PATH="$PROJECT_ROOT/main_system/include"
LINKER_SCRIPT="$SCRIPT_DIR/linker.ld"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

log_info() { echo -e "${BLUE}[INFO]${NC} $1"; }
log_ok() { echo -e "${GREEN}[OK]${NC} $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; }

if [ ! -d "$DRIVERS_SRC" ]; then
    log_error "Drivers source directory not found: $DRIVERS_SRC"
    exit 1
fi

if [ ! -f "$LINKER_SCRIPT" ]; then
    log_error "Linker script not found: $LINKER_SCRIPT"
    exit 1
fi

mkdir -p "$DRIVERS_DST"

log_info "Cleaning old drivers from $DRIVERS_DST"
rm -f "$DRIVERS_DST"/*.drv

CFLAGS="-m32 -ffreestanding -nostdlib \
    -fno-pic -fno-stack-protector -fno-builtin \
    -fno-asynchronous-unwind-tables \
    -fno-exceptions -fno-leading-underscore \
    -fno-common \
    -I$INCLUDE_PATH \
    -w -O2"

# ld с подавлением всех предупреждений
LDFLAGS="-m elf_i386 -static --nmagic -T $LINKER_SCRIPT"

log_info "=========================================="
log_info "Building drivers for PozitronOS"
log_info "=========================================="
log_info "Source: $DRIVERS_SRC"
log_info "Destination: $DRIVERS_DST"
log_info "Include: $INCLUDE_PATH"
log_info ""

TOTAL=0
SUCCESS=0
FAILED=0
ERRORS=""

for source in "$DRIVERS_SRC"/*.c; do
    if [ ! -f "$source" ]; then
        continue
    fi
    
    TOTAL=$((TOTAL + 1))
    filename=$(basename "$source" .c)
    output="$DRIVERS_DST/$filename.drv"
    
    log_info "Building $filename..."
    
    # Компиляция - всё в /dev/null
    if ! gcc $CFLAGS -c "$source" -o "/tmp/$filename.o" 2>/dev/null; then
        # Если ошибка - пересобираем с выводом ошибки
        log_error "Failed to compile $filename"
        ERROR_MSG=$(gcc $CFLAGS -c "$source" -o "/tmp/$filename.o" 2>&1 >/dev/null)
        ERRORS="$ERRORS
=== COMPILATION ERROR: $filename ===
$ERROR_MSG
"
        FAILED=$((FAILED + 1))
        rm -f "/tmp/$filename.o"
        continue
    fi
    
    # Линковка - всё в /dev/null, проверяем только код возврата
    if ! ld $LDFLAGS "/tmp/$filename.o" -o "$output" 2>/dev/null; then
        # Если ошибка - пересобираем с выводом ошибки
        log_error "Failed to link $filename"
        ERROR_MSG=$(ld $LDFLAGS "/tmp/$filename.o" -o "$output" 2>&1 >/dev/null)
        ERRORS="$ERRORS
=== LINKER ERROR: $filename ===
$ERROR_MSG
"
        FAILED=$((FAILED + 1))
        rm -f "/tmp/$filename.o"
        continue
    fi
    
    # Убираем мусорные секции (тихо)
    strip --strip-all "$output" 2>/dev/null
    objcopy -R .eh_frame -R .comment -R .eh_frame_hdr -R .got -R .got.plt "$output" 2>/dev/null
    
    rm -f "/tmp/$filename.o"
    SUCCESS=$((SUCCESS + 1))
    log_ok "$filename.drv built successfully"
done

log_info ""
log_info "=========================================="
log_info "Build summary:"
log_info "  Total:  $TOTAL"
log_info "  Success: $SUCCESS"
log_info "  Failed:  $FAILED"
log_info "=========================================="

if [ -n "$ERRORS" ]; then
    log_error "ERRORS FOUND:"
    echo ""
    echo "$ERRORS"
    echo ""
    exit 1
elif [ $TOTAL -eq 0 ]; then
    log_warn "No drivers found to build"
    exit 0
else
    log_ok "All drivers built successfully!"
    exit 0
fi