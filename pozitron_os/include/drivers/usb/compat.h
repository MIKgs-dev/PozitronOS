#ifndef USB_COMPAT_H
#define USB_COMPAT_H

#include <kernel/types.h>
#include <drivers/serial.h>
#include <drivers/timer.h>
#include <drivers/pci.h>
#include <kernel/memory.h>
#include <lib/string.h>
#include <stdarg.h>
#include <stddef.h>

// Определяем недостающие типы
typedef unsigned char uchar;
typedef unsigned short ushort;
typedef unsigned int uint;

// Заменяем etherboot.h
#define EOF (-1)

// printf с поддержкой форматов
int vsprintf(char *buf, const char *fmt, va_list args);

static inline void usb_printf(const char *fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsprintf(buf, fmt, args);
    va_end(args);
    serial_puts(buf);
}
#define printf usb_printf

// Заменяем DPRINTF и debug
#ifdef DEBUG_USB
#define DPRINTF usb_printf
#define debug usb_printf
#else
#define DPRINTF(...)
#define debug(...)
#endif

// Заменяем udelay/mdelay
#define udelay(us) timer_sleep_us(us)
#define mdelay(ms) timer_sleep_ms(ms)

// Заменяем аллокаторы
#define allot(size) kmalloc(size)
#define allot2(size, align) kmalloc_aligned(size, align)
#define forget(ptr) kfree(ptr)
#define forget2(ptr) kfree_aligned(ptr)

// Заменяем virt_to_bus / bus_to_virt
#define virt_to_bus virt_to_phys
#define bus_to_virt phys_to_virt

// Заменяем currticks (из timer.h)
#define currticks() timer_get_ticks()

// Макросы для работы с регистрами
#define readl(addr) inl((uint32_t)(addr))
#define writel(val, addr) outl((uint32_t)(addr), (val))

// Порядок байт (little-endian для x86)
#define cpu_to_le32(x) (x)
#define le32_to_cpu(x) (x)
#define le16_to_cpu(x) (x)
#define cpu_to_le16(x) (x)
#define le32_to_cpup(p) (le32_to_cpu(*(uint32_t*)(p)))

// Сетевой порядок байт (big-endian) для SCSI
#define ntohl(x) __builtin_bswap32(x)
#define htonl(x) __builtin_bswap32(x)
#define ntohs(x) __builtin_bswap16(x)
#define htons(x) __builtin_bswap16(x)

// Отладочные функции-заглушки
#define dump_device_descriptor(...)
#define dump_config_descriptor(...)
#define dump_hex(...)
#define dump_td(...)
#define dump_uhci(...)
#define dump_link(...)
#define dump_transaction(...)
#define dump_usbdev(...)
#define dump_all_usbdev(...)

#endif