#include "kernel/drivermgr.h"
#include "loader/elf_drv_loader.h"
#include "kernel/memory.h"
#include "kernel/paging.h"
#include "drivers/serial.h"
#include "drivers/timer.h"
#include "drivers/pci.h"
#include "drivers/pic.h"
#include "hw/scanner.h"
#include "fs/vfs.h"
#include "lib/string.h"
#include "lib/mini_printf.h"
#include "core/event.h"
#include <stdarg.h>

static driver_module_t** modules = NULL;
static uint32_t module_count = 0;
static uint32_t module_capacity = 0;
static char driver_path[256] = "/PozitronOS/Sys32/Sysdriv/";
static uint8_t initialized = 0;

static driver_connector_t* video_drv = NULL;
static driver_connector_t* storage_drv = NULL;
static driver_connector_t* input_drv = NULL;
static driver_connector_t* audio_drv = NULL;
static driver_connector_t* network_drv = NULL;

static uint32_t next_device_id = 1;

typedef struct {
    void (*handler)(void*);
    void* data;
    uint8_t registered;
} irq_wrapper_t;

static irq_wrapper_t irq_wrappers[256];

#define MAX_NOTIFY_CALLBACKS 8

static device_notify_callback_t notify_callbacks[MAX_NOTIFY_CALLBACKS];
static void* notify_userdata[MAX_NOTIFY_CALLBACKS];
static uint32_t notify_callback_count = 0;

static uint32_t watchdog_timeout = 0;
static uint32_t watchdog_start_ticks = 0;
static uint8_t watchdog_triggered = 0;
static uint8_t watchdog_active = 0;

static void drv_watchdog_start(uint32_t timeout_ms) {
    watchdog_timeout = timeout_ms;
    watchdog_start_ticks = timer_get_ticks();
    watchdog_triggered = 0;
    watchdog_active = 1;
}

static void drv_watchdog_stop(void) {
    watchdog_active = 0;
}

static int drv_watchdog_triggered(void) {
    if (!watchdog_active) return 0;
    if (timer_get_ticks() - watchdog_start_ticks > watchdog_timeout) {
        watchdog_triggered = 1;
        watchdog_active = 0;
        return 1;
    }
    return 0;
}

static void drv_log(const char* fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsprintf(buf, fmt, args);
    va_end(args);
    serial_puts("[INF] ");
    serial_puts(buf);
}

static void drv_error(const char* fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsprintf(buf, fmt, args);
    va_end(args);
    serial_puts("[ERR] ");
    serial_puts(buf);
}

static void drv_warn(const char* fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsprintf(buf, fmt, args);
    va_end(args);
    serial_puts("[WRN] ");
    serial_puts(buf);
}

static hw_device_t* drv_device_create(hw_type_t type, hw_device_t* parent) {
    hw_device_t* dev = (hw_device_t*)kmalloc(sizeof(hw_device_t));
    if (!dev) return NULL;
    
    memset(dev, 0, sizeof(hw_device_t));
    dev->id = next_device_id++;
    dev->parent = parent;
    
    if (parent) {
        dev->sibling = parent->children;
        parent->children = dev;
        parent->child_count++;
    }
    
    return dev;
}

static void drv_device_destroy(hw_device_t* dev) {
    if (!dev) return;
    
    hw_device_t* child = dev->children;
    while (child) {
        hw_device_t* next = child->sibling;
        drv_device_destroy(child);
        child = next;
    }
    
    if (dev->parent) {
        hw_device_t* prev = NULL;
        hw_device_t* cur = dev->parent->children;
        while (cur) {
            if (cur == dev) {
                if (prev) prev->sibling = dev->sibling;
                else dev->parent->children = dev->sibling;
                dev->parent->child_count--;
                break;
            }
            prev = cur;
            cur = cur->sibling;
        }
    }
    
    kfree(dev);
}

static hw_device_t* drv_device_find(uint32_t id) {
    hw_device_t* dev = scanner_get_device_list();
    while (dev) {
        if (dev->id == id) return dev;
        dev = dev->next;
    }
    return NULL;
}

static void drv_device_dump_recursive(hw_device_t* dev, int level) {
    if (!dev) return;
    
    char indent[32];
    for (int i = 0; i < level * 2 && i < 30; i++) {
        indent[i] = ' ';
    }
    indent[level * 2] = '\0';
    
    const char* type_str = scanner_type_to_string(dev->type);
    serial_printf("%s[%d] %s (id=%d)\n", indent, level, type_str, dev->id);
    
    hw_device_t* child = dev->children;
    while (child) {
        drv_device_dump_recursive(child, level + 1);
        child = child->sibling;
    }
}

static void drv_device_dump(void) {
    serial_puts("\n=== DEVICE TREE ===\n\n");
    
    hw_device_t* dev = scanner_get_device_list();
    
    serial_puts("PozitronOS Device Tree:\n");
    serial_puts("========================\n\n");
    
    serial_puts("[CPU]\n");
    
    serial_puts("  +-- [PCI Bus]\n");
    
    hw_device_t* pci_dev = dev;
    int pci_total = 0;
    while (pci_dev) {
        if (pci_dev->bus == BUS_PCI) pci_total++;
        pci_dev = pci_dev->next;
    }
    
    int pci_cur = 0;
    while (dev) {
        if (dev->bus == BUS_PCI) {
            pci_cur++;
            const char* type_str = scanner_type_to_string(dev->type);
            const char* drv_name = "none";
            
            if (video_drv && video_drv->device == dev) drv_name = video_drv->name;
            else if (storage_drv && storage_drv->device == dev) drv_name = storage_drv->name;
            else if (input_drv && input_drv->device == dev) drv_name = input_drv->name;
            else if (audio_drv && audio_drv->device == dev) drv_name = audio_drv->name;
            else if (network_drv && network_drv->device == dev) drv_name = network_drv->name;
            
            serial_printf("  %s   +-- [%s] %s -> %s\n",
                         pci_cur == pci_total ? " " : "|",
                         type_str, dev->name, drv_name);
        }
        dev = dev->next;
    }
    
    serial_puts("  |\n");
    
    dev = scanner_get_device_list();
    serial_puts("  +-- [ISA Bus]\n");
    
    int isa_total = 0;
    hw_device_t* isa_dev = dev;
    while (isa_dev) {
        if (isa_dev->bus == BUS_ISA) isa_total++;
        isa_dev = isa_dev->next;
    }
    
    int isa_cur = 0;
    while (dev) {
        if (dev->bus == BUS_ISA) {
            isa_cur++;
            const char* type_str = scanner_type_to_string(dev->type);
            
            serial_printf("       %s   +-- [%s] %s\n",
                         isa_cur == isa_total ? " " : "|",
                         type_str, dev->name);
        }
        dev = dev->next;
    }
    
    serial_puts("\n========================\n");
}

static void* drv_kmalloc(uint32_t size) { return kmalloc(size); }
static void drv_kfree(void* ptr) { kfree(ptr); }
static void* drv_kmalloc_aligned(uint32_t size, uint32_t align) { return kmalloc_aligned(size, align); }
static void drv_kfree_aligned(void* ptr) { kfree_aligned(ptr); }
static void* drv_dma_alloc(uint32_t size, uint32_t* phys) { return kmalloc_dma_region(size, phys); }
static void drv_dma_free(void* virt, uint32_t size) { kfree_dma_region(virt, size); }
static uint32_t drv_virt_to_phys(void* virt) { return virt_to_phys(virt); }

static uint32_t drv_pci_read(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint8_t size) {
    switch(size) {
        case 1: return pci_read8(bus, dev, func, offset);
        case 2: return pci_read16(bus, dev, func, offset);
        case 4: return pci_read32(bus, dev, func, offset);
        default: return 0;
    }
}

static void drv_pci_write(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint8_t size, uint32_t val) {
    switch(size) {
        case 1: pci_write8(bus, dev, func, offset, (uint8_t)val); break;
        case 2: pci_write16(bus, dev, func, offset, (uint16_t)val); break;
        case 4: pci_write32(bus, dev, func, offset, val); break;
    }
}

static void drv_pci_enable_bus_master(uint8_t bus, uint8_t dev, uint8_t func) {
    pci_enable_bus_master(bus, dev, func);
}

static void drv_pci_enable_memory_space(uint8_t bus, uint8_t dev, uint8_t func) {
    pci_enable_memory_space(bus, dev, func);
}

static void drv_pci_enable_io_space(uint8_t bus, uint8_t dev, uint8_t func) {
    pci_enable_io_space(bus, dev, func);
}

static uint8_t drv_inb(uint16_t port) { return inb(port); }
static void drv_outb(uint16_t port, uint8_t val) { outb(port, val); }
static uint16_t drv_inw(uint16_t port) { return inw(port); }
static void drv_outw(uint16_t port, uint16_t val) { outw(port, val); }
static uint32_t drv_inl(uint16_t port) { return inl(port); }
static void drv_outl(uint16_t port, uint32_t val) { outl(port, val); }

static uint8_t drv_inb_timeout(uint16_t port, uint32_t timeout_ms) {
    uint32_t start = timer_get_ticks();
    uint8_t val;
    
    drv_watchdog_start(timeout_ms);
    do {
        val = inb(port);
        if (drv_watchdog_triggered()) {
            drv_error("Port 0x%x timeout after %d ms\n", port, timeout_ms);
            drv_watchdog_stop();
            return 0xFF;
        }
    } while (0);
    drv_watchdog_stop();
    return val;
}

static void drv_outb_timeout(uint16_t port, uint8_t value, uint32_t timeout_ms) {
    uint32_t start = timer_get_ticks();
    
    drv_watchdog_start(timeout_ms);
    do {
        outb(port, value);
        if (drv_watchdog_triggered()) {
            drv_error("Port 0x%x write timeout after %d ms\n", port, timeout_ms);
            drv_watchdog_stop();
            return;
        }
    } while (0);
    drv_watchdog_stop();
}

static uint16_t drv_inw_timeout(uint16_t port, uint32_t timeout_ms) {
    uint32_t start = timer_get_ticks();
    uint16_t val;
    
    drv_watchdog_start(timeout_ms);
    do {
        val = inw(port);
        if (drv_watchdog_triggered()) {
            drv_error("Port 0x%x timeout after %d ms\n", port, timeout_ms);
            drv_watchdog_stop();
            return 0xFFFF;
        }
    } while (0);
    drv_watchdog_stop();
    return val;
}

static void drv_outw_timeout(uint16_t port, uint16_t value, uint32_t timeout_ms) {
    uint32_t start = timer_get_ticks();
    
    drv_watchdog_start(timeout_ms);
    do {
        outw(port, value);
        if (drv_watchdog_triggered()) {
            drv_error("Port 0x%x write timeout after %d ms\n", port, timeout_ms);
            drv_watchdog_stop();
            return;
        }
    } while (0);
    drv_watchdog_stop();
}

static uint32_t drv_inl_timeout(uint16_t port, uint32_t timeout_ms) {
    uint32_t start = timer_get_ticks();
    uint32_t val;
    
    drv_watchdog_start(timeout_ms);
    do {
        val = inl(port);
        if (drv_watchdog_triggered()) {
            drv_error("Port 0x%x timeout after %d ms\n", port, timeout_ms);
            drv_watchdog_stop();
            return 0xFFFFFFFF;
        }
    } while (0);
    drv_watchdog_stop();
    return val;
}

static void drv_outl_timeout(uint16_t port, uint32_t value, uint32_t timeout_ms) {
    uint32_t start = timer_get_ticks();
    
    drv_watchdog_start(timeout_ms);
    do {
        outl(port, value);
        if (drv_watchdog_triggered()) {
            drv_error("Port 0x%x write timeout after %d ms\n", port, timeout_ms);
            drv_watchdog_stop();
            return;
        }
    } while (0);
    drv_watchdog_stop();
}

static void drv_memset(void* ptr, uint8_t value, uint32_t size) {
    if (!ptr) return;
    uint8_t* p = (uint8_t*)ptr;
    for (uint32_t i = 0; i < size; i++) {
        p[i] = value;
    }
}

static void drv_memcpy(void* dest, const void* src, uint32_t size) {
    if (!dest || !src) return;
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    for (uint32_t i = 0; i < size; i++) {
        d[i] = s[i];
    }
}

static int drv_memcmp(const void* ptr1, const void* ptr2, uint32_t size) {
    if (!ptr1 || !ptr2) return -1;
    const uint8_t* p1 = (const uint8_t*)ptr1;
    const uint8_t* p2 = (const uint8_t*)ptr2;
    for (uint32_t i = 0; i < size; i++) {
        if (p1[i] != p2[i]) return p1[i] - p2[i];
    }
    return 0;
}

static void* drv_memmove(void* dest, const void* src, uint32_t size) {
    if (!dest || !src) return dest;
    if (dest == src) return dest;
    
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    
    if (d < s) {
        for (uint32_t i = 0; i < size; i++) {
            d[i] = s[i];
        }
    } else {
        for (uint32_t i = size; i > 0; i--) {
            d[i - 1] = s[i - 1];
        }
    }
    return dest;
}

static void irq_wrapper_handler(registers_t* regs) {
    uint8_t irq = regs->int_no - 32;
    if (irq < 256 && irq_wrappers[irq].registered && irq_wrappers[irq].handler) {
        irq_wrappers[irq].handler(irq_wrappers[irq].data);
    }
    pic_send_eoi(irq);
}

static int drv_irq_register(uint8_t irq, void (*handler)(void*), void* data) {
    if (irq >= 256) return -1;
    irq_wrappers[irq].handler = handler;
    irq_wrappers[irq].data = data;
    irq_wrappers[irq].registered = 1;
    irq_install_handler(irq, irq_wrapper_handler);
    return 0;
}

static void drv_irq_unregister(uint8_t irq) {
    if (irq < 256) {
        irq_wrappers[irq].registered = 0;
        irq_wrappers[irq].handler = NULL;
        irq_wrappers[irq].data = NULL;
        irq_uninstall_handler(irq);
    }
}

static void drv_sleep_ms(uint32_t ms) { timer_sleep_ms(ms); }
static void drv_sleep_us(uint32_t us) { timer_sleep_us(us); }
static uint32_t drv_get_ticks(void) { return timer_get_ticks(); }
static uint64_t drv_get_ticks64(void) { return timer_get_ticks(); }

static hw_device_t* drv_api_device_create(hw_type_t type, hw_device_t* parent) {
    return drv_device_create(type, parent);
}

static void drv_api_device_destroy(hw_device_t* dev) {
    drv_device_destroy(dev);
}

static hw_device_t* drv_api_device_find(uint32_t id) {
    return drv_device_find(id);
}

static uint32_t drv_api_device_get_id(hw_device_t* dev) {
    return dev ? dev->id : 0;
}

static int drv_notify_device(device_notification_t* notification) {
    if (!notification || !notification->device) {
        drv_error("Invalid notification!\n");
        return -1;
    }
    
    const char* action = notification->type == DEVICE_NOTIFY_ATTACH ? "attached" :
                         notification->type == DEVICE_NOTIFY_DETACH ? "detached" :
                         notification->type == DEVICE_NOTIFY_STATE_CHANGE ? "changed" : "event";
    
    const char* class_str = "unknown";
    switch (notification->dev_class) {
        case USB_CLASS_HID:   class_str = "HID"; break;
        case USB_CLASS_MSC:   class_str = "MSC"; break;
        case USB_CLASS_HUB:   class_str = "HUB"; break;
        case USB_CLASS_VIDEO: class_str = "VIDEO"; break;
        case USB_CLASS_AUDIO: class_str = "AUDIO"; break;
        default: break;
    }
    
    drv_log("Device notification: %s [%s] %s (id=%d)\n",
            action, class_str,
            notification->device->name ? notification->device->name : "unnamed",
            notification->device->id);
    
    if (notification->type == DEVICE_NOTIFY_ATTACH) {
        switch (notification->dev_class) {
            case USB_CLASS_HID: {
                usb_hid_info_t* hid = (usb_hid_info_t*)notification->class_info;
                if (!hid) break;
                
                if (hid->protocol == USB_HID_PROTO_MOUSE && hid->poll) {
                    if (input_drv) {
                        drv_warn("Replacing existing input driver with USB HID mouse\n");
                    }
                    
                    driver_connector_t* conn = drv_kmalloc(sizeof(driver_connector_t));
                    if (conn) {
                        memset(conn, 0, sizeof(driver_connector_t));
                        conn->type = DRV_TYPE_INPUT;
                        conn->name = notification->device->name;
                        conn->device = notification->device;
                        conn->private_data = notification->driver_priv;
                        conn->mouse_read = (int(*)(int32_t*, int32_t*, uint8_t*))hid->poll;
                        input_drv = conn;
                        drvman.input_available = 1;
                        drv_log("USB HID mouse registered via notification\n");
                    }
                } else if (hid->protocol == USB_HID_PROTO_KEYBOARD && hid->poll) {
                    if (input_drv) {
                        drv_warn("Replacing existing input driver with USB HID keyboard\n");
                    }
                    
                    driver_connector_t* conn = drv_kmalloc(sizeof(driver_connector_t));
                    if (conn) {
                        memset(conn, 0, sizeof(driver_connector_t));
                        conn->type = DRV_TYPE_INPUT;
                        conn->name = notification->device->name;
                        conn->device = notification->device;
                        conn->private_data = notification->driver_priv;
                        conn->keyboard_read = (int(*)(uint8_t*, uint8_t*, uint8_t*))hid->poll;
                        input_drv = conn;
                        drvman.input_available = 1;
                        drv_log("USB HID keyboard registered via notification\n");
                    }
                }
                break;
            }
            
            case USB_CLASS_MSC: {
                usb_msc_info_t* msc = (usb_msc_info_t*)notification->class_info;
                if (!msc) break;
                
                if (msc->read && msc->write) {
                    if (storage_drv) {
                        drv_warn("Replacing existing storage driver with USB MSC\n");
                    }
                    
                    driver_connector_t* conn = drv_kmalloc(sizeof(driver_connector_t));
                    if (conn) {
                        memset(conn, 0, sizeof(driver_connector_t));
                        conn->type = DRV_TYPE_STORAGE;
                        conn->name = notification->device->name;
                        conn->device = notification->device;
                        conn->private_data = notification->driver_priv;
                        conn->storage_read = msc->read;
                        conn->storage_write = msc->write;
                        conn->storage_flush = msc->flush;
                        storage_drv = conn;
                        drvman.storage_available = 1;
                        drv_log("USB MSC storage registered: drive %d, %d sectors\n",
                                msc->drive_number, (uint32_t)msc->capacity_sectors);
                    }
                }
                break;
            }
            
            default:
                drv_log("Device class 0x%02X attached, no specific handler yet\n",
                        notification->dev_class);
                break;
        }
    }
    
    if (notification->type == DEVICE_NOTIFY_DETACH) {
        switch (notification->dev_class) {
            case USB_CLASS_HID:
                if (input_drv && input_drv->device == notification->device) {
                    drv_kfree(input_drv);
                    input_drv = NULL;
                    drvman.input_available = 0;
                    drv_log("Input device detached\n");
                }
                break;
                
            case USB_CLASS_MSC:
                if (storage_drv && storage_drv->device == notification->device) {
                    drv_kfree(storage_drv);
                    storage_drv = NULL;
                    drvman.storage_available = 0;
                    drv_log("Storage device detached\n");
                }
                break;
                
            default:
                break;
        }
    }
    
    event_t ev;
    memset(&ev, 0, sizeof(event_t));
    
    switch (notification->type) {
        case DEVICE_NOTIFY_ATTACH:
            ev.type = (notification->dev_class == USB_CLASS_MSC) ?
                      EVENT_USB_MSC_ATTACH : EVENT_USB_DEVICE_ATTACH;
            break;
        case DEVICE_NOTIFY_DETACH:
            ev.type = (notification->dev_class == USB_CLASS_MSC) ?
                      EVENT_USB_MSC_DETACH : EVENT_USB_DEVICE_DETACH;
            break;
        default:
            ev.type = EVENT_NONE;
            break;
    }
    
    ev.data1 = notification->device->id;
    ev.data2 = (uint32_t)notification->dev_class;
    event_post(ev);
    
    for (uint32_t i = 0; i < notify_callback_count; i++) {
        if (notify_callbacks[i]) {
            notify_callbacks[i](notification, notify_userdata[i]);
        }
    }
    
    return 0;
}

static int drvman_register_notify_callback(device_notify_callback_t callback, void* user_data) {
    if (!callback) return -1;
    if (notify_callback_count >= MAX_NOTIFY_CALLBACKS) {
        drv_error("Too many notification callbacks!\n");
        return -1;
    }
    
    for (uint32_t i = 0; i < notify_callback_count; i++) {
        if (notify_callbacks[i] == callback) {
            notify_userdata[i] = user_data;
            return 0;
        }
    }
    
    notify_callbacks[notify_callback_count] = callback;
    notify_userdata[notify_callback_count] = user_data;
    notify_callback_count++;
    return 0;
}

static int drvman_unregister_notify_callback(device_notify_callback_t callback) {
    if (!callback) return -1;
    
    for (uint32_t i = 0; i < notify_callback_count; i++) {
        if (notify_callbacks[i] == callback) {
            for (uint32_t j = i; j < notify_callback_count - 1; j++) {
                notify_callbacks[j] = notify_callbacks[j + 1];
                notify_userdata[j] = notify_userdata[j + 1];
            }
            notify_callback_count--;
            notify_callbacks[notify_callback_count] = NULL;
            notify_userdata[notify_callback_count] = NULL;
            return 0;
        }
    }
    return -1;
}

static driver_api_t drv_api = {
    .version_major = DRV_API_VERSION_MAJOR,
    .version_minor = DRV_API_VERSION_MINOR,
    .version_patch = DRV_API_VERSION_PATCH,
    .kmalloc = drv_kmalloc,
    .kfree = drv_kfree,
    .kmalloc_aligned = drv_kmalloc_aligned,
    .kfree_aligned = drv_kfree_aligned,
    .dma_alloc = drv_dma_alloc,
    .dma_free = drv_dma_free,
    .virt_to_phys = drv_virt_to_phys,
    .memset = drv_memset,
    .memcpy = drv_memcpy,
    .memcmp = drv_memcmp,
    .memmove = drv_memmove,
    .pci_read = drv_pci_read,
    .pci_write = drv_pci_write,
    .pci_enable_bus_master = drv_pci_enable_bus_master,
    .pci_enable_memory_space = drv_pci_enable_memory_space,
    .pci_enable_io_space = drv_pci_enable_io_space,
    .inb = drv_inb,
    .outb = drv_outb,
    .inw = drv_inw,
    .outw = drv_outw,
    .inl = drv_inl,
    .outl = drv_outl,
    .inb_timeout = drv_inb_timeout,
    .outb_timeout = drv_outb_timeout,
    .inw_timeout = drv_inw_timeout,
    .outw_timeout = drv_outw_timeout,
    .inl_timeout = drv_inl_timeout,
    .outl_timeout = drv_outl_timeout,
    .irq_register = drv_irq_register,
    .irq_unregister = drv_irq_unregister,
    .sleep_ms = drv_sleep_ms,
    .sleep_us = drv_sleep_us,
    .get_ticks = drv_get_ticks,
    .get_ticks64 = drv_get_ticks64,
    .log = drv_log,
    .error = drv_error,
    .debug = drv_log,
    .device_create = drv_api_device_create,
    .device_destroy = drv_api_device_destroy,
    .device_find = drv_api_device_find,
    .device_get_id = drv_api_device_get_id,
    .watchdog_start = drv_watchdog_start,
    .watchdog_stop = drv_watchdog_stop,
    .watchdog_triggered = drv_watchdog_triggered,
    .notify_device = drv_notify_device,
};

static void* drvman_video_fb(void) { return video_drv ? video_drv->fb_addr() : NULL; }
static uint32_t drvman_video_width(void) { return video_drv ? video_drv->fb_width() : 0; }
static uint32_t drvman_video_height(void) { return video_drv ? video_drv->fb_height() : 0; }
static uint32_t drvman_video_bpp(void) { return video_drv ? video_drv->fb_bpp() : 0; }
static void drvman_video_put_pixel(uint32_t x, uint32_t y, uint32_t c) { if (video_drv && video_drv->put_pixel) video_drv->put_pixel(x, y, c); }
static uint32_t drvman_video_get_pixel(uint32_t x, uint32_t y) { return video_drv && video_drv->get_pixel ? video_drv->get_pixel(x, y) : 0; }
static void drvman_video_fill(uint32_t c) { if (video_drv && video_drv->fill) video_drv->fill(c); }
static void drvman_video_swap(void) { if (video_drv && video_drv->swap) video_drv->swap(); }
static int drvman_video_set_mode(uint32_t w, uint32_t h, uint32_t bpp) { return video_drv && video_drv->set_mode ? video_drv->set_mode(w, h, bpp) : -1; }

static int drvman_storage_read(uint8_t drive, uint64_t lba, uint32_t count, void* buf) {
    return storage_drv && storage_drv->storage_read ? storage_drv->storage_read(drive, lba, count, buf) : -1;
}
static int drvman_storage_write(uint8_t drive, uint64_t lba, uint32_t count, const void* buf) {
    return storage_drv && storage_drv->storage_write ? storage_drv->storage_write(drive, lba, count, buf) : -1;
}
static int drvman_storage_flush(uint8_t drive) {
    return storage_drv && storage_drv->storage_flush ? storage_drv->storage_flush(drive) : -1;
}

static int drvman_keyboard_read(uint8_t* sc, uint8_t* ascii, uint8_t* mod) {
    return input_drv && input_drv->keyboard_read ? input_drv->keyboard_read(sc, ascii, mod) : -1;
}
static int drvman_mouse_read(int32_t* x, int32_t* y, uint8_t* btns) {
    return input_drv && input_drv->mouse_read ? input_drv->mouse_read(x, y, btns) : -1;
}

static int drvman_audio_play(const void* data, uint32_t size) {
    return audio_drv && audio_drv->audio_play ? audio_drv->audio_play(data, size) : -1;
}
static void drvman_audio_stop(void) { if (audio_drv && audio_drv->audio_stop) audio_drv->audio_stop(); }
static void drvman_audio_set_volume(uint8_t vol) { if (audio_drv && audio_drv->audio_set_volume) audio_drv->audio_set_volume(vol); }

int drvman_register_builtin(driver_module_t* module) {
    if (!module || !module->name) return -1;
    
    for (uint32_t i = 0; i < module_count; i++) {
        if (modules[i] == module) return 0;
        if (strcmp(modules[i]->name, module->name) == 0) {
            drv_error("Driver '%s' already registered!\n", module->name);
            return -1;
        }
    }
    
    if (module_count >= module_capacity) {
        uint32_t new_cap = module_capacity ? module_capacity * 2 : 16;
        driver_module_t** new_arr = drv_kmalloc(new_cap * sizeof(driver_module_t*));
        if (!new_arr) return -1;
        
        for (uint32_t i = 0; i < module_count; i++) new_arr[i] = modules[i];
        if (modules) drv_kfree(modules);
        modules = new_arr;
        module_capacity = new_cap;
    }
    
    modules[module_count++] = module;
    module->api = &drv_api;
    module->loaded = 1;
    module->state = DRV_STATE_LOADED;
    
    if (module->init) {
        if (module->init(module, &drv_api) == 0) {
            module->state = DRV_STATE_INITIALIZED;
        } else {
            module->state = DRV_STATE_FAILED;
            drv_error("Init failed for %s\n", module->name);
        }
    }
    
    drv_log("Registered builtin: %s v%s\n", module->name, module->version);
    return 0;
}

static int drvman_load(const char* path) {
    drv_log("Loading driver: %s\n", path);
    
    elf_drv_load_result_t load_result;
    if (elf_drv_load(path, &load_result) != 0) {
        drv_error("Failed to load ELF driver: %s\n", path);
        return -1;
    }
    
    driver_module_t* (*entry)(const driver_api_t*) = 
        (driver_module_t* (*)(const driver_api_t*))load_result.entry_point;
    
    driver_module_t* mod = entry(&drv_api);
    if (!mod || mod->magic != DRV_MAGIC) {
        drv_error("Invalid driver entry point\n");
        elf_drv_unload(&load_result);
        return -1;
    }
    
    mod->api = &drv_api;
    mod->loaded = 1;
    mod->state = DRV_STATE_LOADED;
    mod->alloc_info.virt_addr = load_result.base_addr;
    mod->alloc_info.size = load_result.size;
    mod->alloc_info.is_mapped = 1;
    
    if (module_count >= module_capacity) {
        uint32_t new_cap = module_capacity ? module_capacity * 2 : 16;
        driver_module_t** new_arr = drv_kmalloc(new_cap * sizeof(driver_module_t*));
        if (!new_arr) {
            elf_drv_unload(&load_result);
            return -1;
        }
        for (uint32_t i = 0; i < module_count; i++) new_arr[i] = modules[i];
        if (modules) drv_kfree(modules);
        modules = new_arr;
        module_capacity = new_cap;
    }
    
    modules[module_count++] = mod;
    
    if (mod->init) {
        if (mod->init(mod, &drv_api) == 0) {
            mod->state = DRV_STATE_INITIALIZED;
            drv_log("Driver %s initialized\n", mod->name);
        } else {
            mod->state = DRV_STATE_FAILED;
            drv_error("Init failed for %s\n", mod->name);
        }
    }
    
    drv_log("Loaded driver: %s v%s by %s\n", 
            mod->name, mod->version, mod->author);
    
    return 0;
}

static void drv_module_ref(driver_module_t* mod) {
    if (mod) mod->refcount++;
}

static void drv_module_unref(driver_module_t* mod) {
    if (mod && mod->refcount > 0) {
        mod->refcount--;
        if (mod->refcount == 0 && mod->state == DRV_STATE_UNLOADED) {
            if (mod->unload) mod->unload(mod);
            if (mod->alloc_info.virt_addr && mod->alloc_info.is_mapped) {
                for (uint32_t i = 0; i < mod->alloc_info.page_count; i++) {
                    paging_unmap_page(current_directory, 
                                     (uint32_t)mod->alloc_info.virt_addr + i * 4096);
                }
                drv_api.kfree_aligned(mod->alloc_info.virt_addr);
                mod->alloc_info.virt_addr = NULL;
                mod->alloc_info.is_mapped = 0;
            }
        }
    }
}

static int drvman_attach_driver(driver_module_t* mod, hw_device_t* hw_dev) {
    if (!mod || !hw_dev) return -1;
    if (mod->state == DRV_STATE_FAILED) return -1;
    
    driver_connector_t* conn = drv_kmalloc(sizeof(driver_connector_t));
    if (!conn) return -1;
    
    memset(conn, 0, sizeof(driver_connector_t));
    conn->type = mod->type;
    conn->name = mod->name;
    conn->device = hw_dev;
    
    if (!mod->attach) {
        drv_kfree(conn);
        return -1;
    }
    
    drv_watchdog_start(2000);
    int result = mod->attach(mod, hw_dev, conn);
    drv_watchdog_stop();
    
    if (drv_watchdog_triggered()) {
        drv_error("Driver %s attach timed out!\n", mod->name);
        drv_kfree(conn);
        mod->state = DRV_STATE_FAILED;
        return -1;
    }
    
    if (result != 0) {
        drv_kfree(conn);
        return -1;
    }
    
    mod->state = DRV_STATE_ACTIVE;
    mod->active = 1;
    
    switch (mod->type) {
        case DRV_TYPE_VIDEO:
            if (video_drv) {
                drv_warn("Replacing video driver %s with %s\n", video_drv->name, mod->name);
            }
            video_drv = conn;
            drvman.video_available = 1;
            break;
        case DRV_TYPE_STORAGE:
            storage_drv = conn;
            drvman.storage_available = 1;
            break;
        case DRV_TYPE_INPUT:
            input_drv = conn;
            drvman.input_available = 1;
            break;
        case DRV_TYPE_AUDIO:
            audio_drv = conn;
            drvman.audio_available = 1;
            break;
        case DRV_TYPE_NETWORK:
            network_drv = conn;
            break;
        default: break;
    }
    
    const char* type_str = 
        mod->type == DRV_TYPE_VIDEO ? "VIDEO" :
        mod->type == DRV_TYPE_STORAGE ? "STORAGE" :
        mod->type == DRV_TYPE_INPUT ? "INPUT" :
        mod->type == DRV_TYPE_AUDIO ? "AUDIO" :
        mod->type == DRV_TYPE_NETWORK ? "NETWORK" : "OTHER";
    
    drv_log("Attached %s (%s) to device %d\n", mod->name, type_str, hw_dev->id);
    return 0;
}

void drvman_autoprobe(void) {
    drv_log("Autoprobing hardware...\n");
    
    hw_device_t* hw = scanner_get_device_list();
    if (!hw) {
        drv_warn("No hardware devices found!\n");
        return;
    }
    
    uint32_t attached_count = 0;
    
    while (hw) {
        if (hw->bus != BUS_PCI) {
            hw = hw->next;
            continue;
        }
        
        for (uint32_t i = 0; i < module_count; i++) {
            driver_module_t* mod = modules[i];
            
            if (mod->state == DRV_STATE_FAILED) continue;
            if (mod->active) continue;
            
            int match = 1;
            
            if (!mod->match_any_vendor && mod->vendor_id != 0xFFFF) {
                if (mod->vendor_id != hw->pci.vendor_id) match = 0;
            }
            if (match && !mod->match_any_device && mod->device_id != 0xFFFF) {
                if (mod->device_id != hw->pci.device_id) match = 0;
            }
            if (match && mod->class_code != 0xFF) {
                if (mod->class_code != hw->pci.class_code) match = 0;
            }
            if (match && mod->subclass != 0xFF) {
                if (mod->subclass != hw->pci.subclass) match = 0;
            }
            
            if (!match) continue;
            
            if (mod->probe) {
                drv_watchdog_start(1000);
                int result = mod->probe(mod, hw);
                drv_watchdog_stop();
                
                if (drv_watchdog_triggered()) {
                    drv_error("Driver %s probe timed out!\n", mod->name);
                    mod->state = DRV_STATE_FAILED;
                    continue;
                }
                
                if (result == 0) {
                    if (drvman_attach_driver(mod, hw) == 0) {
                        attached_count++;
                        break;
                    }
                }
            }
        }
        hw = hw->next;
    }
    
    drv_log("Autoprobe complete: %d drivers attached\n", attached_count);
}

void drvman_scan(void) {
    drv_log("Scanning %s for .drv files\n", driver_path);
    
    struct vfs_file* dir;
    if (vfs_open(driver_path, FS_O_RDONLY | FS_O_DIRECTORY, &dir) != 0) {
        drv_log("Directory not found: %s\n", driver_path);
        return;
    }
    
    struct vfs_dirent dirent;
    uint32_t bytes;
    uint32_t loaded_count = 0;
    
    while (vfs_readdir(dir, &dirent, &bytes) == 0 && bytes > 0) {
        if (dirent.d_name[0] == '.') continue;
        
        size_t len = strlen(dirent.d_name);
        if (len < 5) continue;
        
        const char* ext = dirent.d_name + len - 4;
        if (strcmp(ext, ".drv") == 0) {
            char fullpath[512];
            sprintf(fullpath, "%s/%s", driver_path, dirent.d_name);
            if (drvman_load(fullpath) == 0) {
                loaded_count++;
            }
        }
    }
    
    vfs_close(dir);
    drv_log("Loaded %d external drivers\n", loaded_count);
}

int drvman_unload(const char* name) {
    if (!name) return -1;
    
    for (uint32_t i = 0; i < module_count; i++) {
        if (strcmp(modules[i]->name, name) == 0) {
            driver_module_t* mod = modules[i];
            
            if (mod->refcount > 0) {
                drv_error("Driver %s is in use (refcount=%d)\n", name, mod->refcount);
                return -1;
            }
            
            if (mod->active) {
                if (mod->detach) mod->detach(mod);
                mod->active = 0;
                mod->state = DRV_STATE_UNLOADED;
            }
            
            if (mod->alloc_info.virt_addr && mod->alloc_info.is_mapped) {
                for (uint32_t j = 0; j < mod->alloc_info.page_count; j++) {
                    paging_unmap_page(current_directory, 
                                     (uint32_t)mod->alloc_info.virt_addr + j * 4096);
                }
                drv_api.kfree_aligned(mod->alloc_info.virt_addr);
                if (mod->alloc_info.phys_pages) {
                    drv_kfree(mod->alloc_info.phys_pages);
                }
                mod->alloc_info.virt_addr = NULL;
                mod->alloc_info.is_mapped = 0;
            }
            
            mod->loaded = 0;
            drv_log("Unloaded driver: %s\n", name);
            return 0;
        }
    }
    
    drv_error("Driver %s not found\n", name);
    return -1;
}

int drvman_force_load(const char* name) {
    if (!name) return -1;
    
    for (uint32_t i = 0; i < module_count; i++) {
        if (strcmp(modules[i]->name, name) == 0) {
            driver_module_t* mod = modules[i];
            
            if (mod->state == DRV_STATE_ACTIVE) {
                drv_log("Driver %s already active\n", name);
                return 0;
            }
            
            if (mod->state == DRV_STATE_FAILED) {
                drv_error("Driver %s is in failed state\n", name);
                return -1;
            }
            
            if (mod->init) {
                if (mod->init(mod, &drv_api) == 0) {
                    mod->state = DRV_STATE_INITIALIZED;
                    mod->loaded = 1;
                    drv_log("Force loaded driver: %s\n", name);
                    return 0;
                } else {
                    mod->state = DRV_STATE_FAILED;
                    drv_error("Force load failed for %s\n", name);
                    return -1;
                }
            }
        }
    }
    
    drv_error("Driver %s not found\n", name);
    return -1;
}

void drvman_test_pattern(void) {
    if (!drvman.video_available) {
        drv_error("Cannot draw pattern - no video\n");
        return;
    }
    
    uint32_t w = drvman.video_width();
    uint32_t h = drvman.video_height();
    
    if (w == 0 || h == 0) {
        drv_error("Invalid video resolution: %dx%d\n", w, h);
        return;
    }
    
    drv_log("Drawing test pattern %dx%d\n", w, h);
    
    for (uint32_t y = 0; y < h; y++) {
        for (uint32_t x = 0; x < w; x++) {
            uint32_t r = (x * 255) / w;
            uint32_t g = (y * 255) / h;
            uint32_t b = ((x + y) * 255) / (w + h);
            uint32_t color = (r << 16) | (g << 8) | b;
            drvman.video_put_pixel(x, y, color);
        }
    }
    
    uint32_t border_color = 0x00FF00;
    for (uint32_t x = 0; x < w; x++) {
        drvman.video_put_pixel(x, 0, border_color);
        drvman.video_put_pixel(x, h-1, border_color);
    }
    for (uint32_t y = 0; y < h; y++) {
        drvman.video_put_pixel(0, y, border_color);
        drvman.video_put_pixel(w-1, y, border_color);
    }
    
    if (drvman.video_swap) {
        drvman.video_swap();
    }
    
    drv_log("Test pattern drawn!\n");
}

static void drvman_dump(void) {
    drv_log("=== DRIVER MANAGER ===\n");
    drv_log("Video: %s (%s)\n", 
            video_drv ? video_drv->name : "none",
            video_drv ? "available" : "not available");
    drv_log("Storage: %s\n", storage_drv ? storage_drv->name : "none");
    drv_log("Input: %s\n", input_drv ? input_drv->name : "none");
    drv_log("Audio: %s\n", audio_drv ? audio_drv->name : "none");
    drv_log("Network: %s\n", network_drv ? network_drv->name : "none");
    drv_log("Total modules: %d\n", module_count);
    
    drv_log("--- Modules ---\n");
    for (uint32_t i = 0; i < module_count; i++) {
        driver_module_t* mod = modules[i];
        const char* state_str = 
            mod->state == DRV_STATE_UNLOADED ? "UNLOADED" :
            mod->state == DRV_STATE_LOADED ? "LOADED" :
            mod->state == DRV_STATE_INITIALIZED ? "INITIALIZED" :
            mod->state == DRV_STATE_ACTIVE ? "ACTIVE" :
            mod->state == DRV_STATE_FAILED ? "FAILED" : "UNKNOWN";
        
        drv_log("  %s: %s v%s [%s] ref=%d\n", 
                mod->name, state_str, mod->version, 
                mod->active ? "active" : "inactive",
                mod->refcount);
    }
    drv_log("=====================\n");
    
    drv_device_dump();
}

static hw_device_t* drvman_device_get_root(void) {
    hw_device_t* root = scanner_get_device_list();
    while (root && root->parent) root = root->parent;
    return root;
}

static hw_device_t* drvman_device_find(uint32_t id) {
    return drv_device_find(id);
}

static void drvman_device_dump(void) {
    drv_device_dump();
}

drvman_api_t drvman = {
    .video_fb = drvman_video_fb,
    .video_width = drvman_video_width,
    .video_height = drvman_video_height,
    .video_bpp = drvman_video_bpp,
    .video_put_pixel = drvman_video_put_pixel,
    .video_get_pixel = drvman_video_get_pixel,
    .video_fill = drvman_video_fill,
    .video_swap = drvman_video_swap,
    .video_set_mode = drvman_video_set_mode,
    .video_available = 0,
    
    .storage_read = drvman_storage_read,
    .storage_write = drvman_storage_write,
    .storage_flush = drvman_storage_flush,
    .storage_available = 0,
    
    .keyboard_read = drvman_keyboard_read,
    .mouse_read = drvman_mouse_read,
    .input_available = 0,
    
    .audio_play = drvman_audio_play,
    .audio_stop = drvman_audio_stop,
    .audio_set_volume = drvman_audio_set_volume,
    .audio_available = 0,
    
    .load = drvman_load,
    .unload = drvman_unload,
    .scan = drvman_scan,
    .dump = drvman_dump,
    .force_load = drvman_force_load,
    
    .device_get_root = drvman_device_get_root,
    .device_find = drvman_device_find,
    .device_dump = drvman_device_dump,
    
    .register_notify_callback = drvman_register_notify_callback,
    .unregister_notify_callback = drvman_unregister_notify_callback,
};

void drvman_init(void) {
    if (initialized) return;
    
    modules = NULL;
    module_count = 0;
    module_capacity = 0;
    next_device_id = 1;
    
    memset(irq_wrappers, 0, sizeof(irq_wrappers));
    memset(notify_callbacks, 0, sizeof(notify_callbacks));
    memset(notify_userdata, 0, sizeof(notify_userdata));
    notify_callback_count = 0;
    
    initialized = 1;
    drv_log("Driver manager initialized\n");
}

void drvman_set_path(const char* path) {
    if (path) {
        strncpy(driver_path, path, 255);
        driver_path[255] = '\0';
        drv_log("Driver path set to: %s\n", driver_path);
    }
}