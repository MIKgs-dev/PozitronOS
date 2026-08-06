#ifndef KERNEL_DRIVERMGR_H
#define KERNEL_DRIVERMGR_H

#include <stdint.h>
#include <stddef.h>
#include "hw/scanner.h"

#define DRV_API_VERSION_MAJOR 1
#define DRV_API_VERSION_MINOR 1
#define DRV_API_VERSION_PATCH 0
#define DRV_MAGIC 0x505A4452

typedef enum {
    DRV_STATE_UNLOADED = 0,
    DRV_STATE_LOADED,
    DRV_STATE_INITIALIZED,
    DRV_STATE_ACTIVE,
    DRV_STATE_FAILED
} drv_state_t;

typedef enum {
    DRV_TYPE_NONE = 0,
    DRV_TYPE_VIDEO,
    DRV_TYPE_AUDIO,
    DRV_TYPE_STORAGE,
    DRV_TYPE_INPUT,
    DRV_TYPE_OUTPUT,
    DRV_TYPE_NETWORK,
    DRV_TYPE_USB,
    DRV_TYPE_USB_HOST,
    DRV_TYPE_USB_DEVICE,
    DRV_TYPE_PCI,
    DRV_TYPE_ACPI,
    DRV_TYPE_OTHER,
} drv_type_t;

typedef enum {
    HW_TYPE_UNKNOWN = 0,
    HW_TYPE_PCI,
    HW_TYPE_USB,
    HW_TYPE_USB_HUB,
    HW_TYPE_USB_HID,
    HW_TYPE_USB_MSC,
    HW_TYPE_USB_CAMERA,
    HW_TYPE_USB_AUDIO,
    HW_TYPE_USB_PRINTER,
    HW_TYPE_USB_VENDOR,
} hw_type_t;

typedef enum {
    DEVICE_NOTIFY_ATTACH = 0,
    DEVICE_NOTIFY_DETACH,
    DEVICE_NOTIFY_STATE_CHANGE,
    DEVICE_NOTIFY_DATA_READY,
    DEVICE_NOTIFY_ERROR,
    DEVICE_NOTIFY_RESCAN_COMPLETE,
} device_notify_type_t;

typedef enum {
    USB_CLASS_UNKNOWN  = 0x00,
    USB_CLASS_AUDIO    = 0x01,
    USB_CLASS_CDC      = 0x02,
    USB_CLASS_HID      = 0x03,
    USB_CLASS_PRINTER  = 0x07,
    USB_CLASS_MSC      = 0x08,
    USB_CLASS_HUB      = 0x09,
    USB_CLASS_VIDEO    = 0x0E,
    USB_CLASS_VENDOR   = 0xFF,
} usb_device_class_t;

typedef enum {
    USB_HID_SUBCLASS_NONE = 0,
    USB_HID_SUBCLASS_BOOT = 1,
} usb_hid_subclass_t;

typedef enum {
    USB_HID_PROTO_NONE     = 0,
    USB_HID_PROTO_KEYBOARD = 1,
    USB_HID_PROTO_MOUSE    = 2,
} usb_hid_protocol_t;

typedef struct {
    usb_hid_subclass_t subclass;
    usb_hid_protocol_t protocol;
    int (*poll)(void* hid_data);
    void* hid_data;
} usb_hid_info_t;

typedef struct {
    uint8_t  drive_number;
    uint64_t capacity_sectors;
    uint32_t sector_size;
    uint8_t  removable;
    int (*read)(uint8_t drive, uint64_t lba, uint32_t count, void* buf);
    int (*write)(uint8_t drive, uint64_t lba, uint32_t count, const void* buf);
    int (*flush)(uint8_t drive);
} usb_msc_info_t;

typedef struct {
    uint16_t vendor_id;
    uint16_t product_id;
} usb_vendor_info_t;

typedef struct {
    device_notify_type_t  type;
    hw_device_t*          device;
    usb_device_class_t    dev_class;
    void*                 driver_priv;
    void*                 class_info;
} device_notification_t;

typedef void (*device_notify_callback_t)(const device_notification_t* notification, void* user_data);

typedef struct driver_api driver_api_t;
typedef struct driver_connector driver_connector_t;
typedef struct driver_module driver_module_t;

struct driver_api {
    uint32_t version_major;
    uint32_t version_minor;
    uint32_t version_patch;
    
    void* (*kmalloc)(uint32_t size);
    void (*kfree)(void* ptr);
    void* (*kmalloc_aligned)(uint32_t size, uint32_t align);
    void (*kfree_aligned)(void* ptr);
    void* (*dma_alloc)(uint32_t size, uint32_t* phys);
    void (*dma_free)(void* virt, uint32_t size);
    uint32_t (*virt_to_phys)(void* virt);
    
    void (*memset)(void* ptr, uint8_t value, uint32_t size);
    void (*memcpy)(void* dest, const void* src, uint32_t size);
    int (*memcmp)(const void* ptr1, const void* ptr2, uint32_t size);
    void* (*memmove)(void* dest, const void* src, uint32_t size);
    
    uint32_t (*pci_read)(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint8_t size);
    void (*pci_write)(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint8_t size, uint32_t value);
    void (*pci_enable_bus_master)(uint8_t bus, uint8_t dev, uint8_t func);
    void (*pci_enable_memory_space)(uint8_t bus, uint8_t dev, uint8_t func);
    void (*pci_enable_io_space)(uint8_t bus, uint8_t dev, uint8_t func);
    
    uint8_t (*inb)(uint16_t port);
    void (*outb)(uint16_t port, uint8_t value);
    uint16_t (*inw)(uint16_t port);
    void (*outw)(uint16_t port, uint16_t value);
    uint32_t (*inl)(uint16_t port);
    void (*outl)(uint16_t port, uint32_t value);
    
    uint8_t (*inb_timeout)(uint16_t port, uint32_t timeout_ms);
    void (*outb_timeout)(uint16_t port, uint8_t value, uint32_t timeout_ms);
    uint16_t (*inw_timeout)(uint16_t port, uint32_t timeout_ms);
    void (*outw_timeout)(uint16_t port, uint16_t value, uint32_t timeout_ms);
    uint32_t (*inl_timeout)(uint16_t port, uint32_t timeout_ms);
    void (*outl_timeout)(uint16_t port, uint32_t value, uint32_t timeout_ms);
    
    int (*irq_register)(uint8_t irq, void (*handler)(void*), void* data);
    void (*irq_unregister)(uint8_t irq);
    
    void (*sleep_ms)(uint32_t ms);
    void (*sleep_us)(uint32_t us);
    uint32_t (*get_ticks)(void);
    uint64_t (*get_ticks64)(void);
    
    void (*log)(const char* fmt, ...);
    void (*error)(const char* fmt, ...);
    void (*debug)(const char* fmt, ...);
    
    hw_device_t* (*device_create)(hw_type_t type, hw_device_t* parent);
    void (*device_destroy)(hw_device_t* dev);
    hw_device_t* (*device_find)(uint32_t id);
    uint32_t (*device_get_id)(hw_device_t* dev);
    
    void (*watchdog_start)(uint32_t timeout_ms);
    void (*watchdog_stop)(void);
    int (*watchdog_triggered)(void);
    
    int (*notify_device)(device_notification_t* notification);
};

struct driver_connector {
    drv_type_t type;
    const char* name;
    hw_device_t* device;
    void* private_data;
    
    void* (*fb_addr)(void);
    uint32_t (*fb_width)(void);
    uint32_t (*fb_height)(void);
    uint32_t (*fb_bpp)(void);
    void (*put_pixel)(uint32_t x, uint32_t y, uint32_t color);
    uint32_t (*get_pixel)(uint32_t x, uint32_t y);
    void (*fill)(uint32_t color);
    void (*swap)(void);
    int (*set_mode)(uint32_t w, uint32_t h, uint32_t bpp);
    
    int (*storage_read)(uint8_t drive, uint64_t lba, uint32_t count, void* buf);
    int (*storage_write)(uint8_t drive, uint64_t lba, uint32_t count, const void* buf);
    int (*storage_flush)(uint8_t drive);
    
    int (*keyboard_read)(uint8_t* scancode, uint8_t* ascii, uint8_t* mod);
    int (*mouse_read)(int32_t* x, int32_t* y, uint8_t* buttons);
    
    int (*net_send)(const void* buf, uint32_t len);
    int (*net_recv)(void* buf, uint32_t len);
    void (*net_set_mac)(const uint8_t* mac);
    
    int (*audio_play)(const void* data, uint32_t size);
    void (*audio_stop)(void);
    void (*audio_set_volume)(uint8_t volume);
    
    int (*usb_control)(uint8_t addr, uint8_t request, uint16_t value, uint16_t index, void* data, uint16_t len);
    int (*usb_bulk)(uint8_t addr, uint8_t ep, void* data, uint16_t len);
    int (*usb_interrupt)(uint8_t addr, uint8_t ep, void* data, uint16_t len);
    void (*usb_set_address)(uint8_t addr);
};

typedef struct {
    void* virt_addr;
    uint32_t size;
    uint32_t* phys_pages;
    uint32_t page_count;
    uint8_t is_mapped;
} drv_alloc_info_t;

struct driver_module {
    uint32_t magic;
    uint32_t api_version_major;
    uint32_t api_version_minor;
    uint32_t api_version_patch;
    
    char name[64];
    char version[32];
    char author[64];
    drv_type_t type;
    
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t match_any_vendor;
    uint8_t match_any_device;
    
    const driver_api_t* api;
    
    int (*init)(driver_module_t* mod, const driver_api_t* api);
    int (*probe)(driver_module_t* mod, hw_device_t* hw_dev);
    int (*attach)(driver_module_t* mod, hw_device_t* hw_dev, driver_connector_t* conn);
    int (*detach)(driver_module_t* mod);
    int (*unload)(driver_module_t* mod);
    
    void* private_data;
    drv_state_t state;
    uint32_t refcount;
    uint8_t loaded;
    uint8_t active;
    
    drv_alloc_info_t alloc_info;
};

typedef struct {
    void* (*video_fb)(void);
    uint32_t (*video_width)(void);
    uint32_t (*video_height)(void);
    uint32_t (*video_bpp)(void);
    void (*video_put_pixel)(uint32_t x, uint32_t y, uint32_t color);
    uint32_t (*video_get_pixel)(uint32_t x, uint32_t y);
    void (*video_fill)(uint32_t color);
    void (*video_swap)(void);
    int (*video_set_mode)(uint32_t w, uint32_t h, uint32_t bpp);
    uint8_t video_available;
    
    int (*storage_read)(uint8_t drive, uint64_t lba, uint32_t count, void* buf);
    int (*storage_write)(uint8_t drive, uint64_t lba, uint32_t count, const void* buf);
    int (*storage_flush)(uint8_t drive);
    uint8_t storage_available;
    
    int (*keyboard_read)(uint8_t* scancode, uint8_t* ascii, uint8_t* mod);
    int (*mouse_read)(int32_t* x, int32_t* y, uint8_t* buttons);
    uint8_t input_available;
    
    int (*audio_play)(const void* data, uint32_t size);
    void (*audio_stop)(void);
    void (*audio_set_volume)(uint8_t volume);
    uint8_t audio_available;
    
    int (*load)(const char* path);
    int (*unload)(const char* name);
    void (*scan)(void);
    void (*dump)(void);
    int (*force_load)(const char* name);
    
    hw_device_t* (*device_get_root)(void);
    hw_device_t* (*device_find)(uint32_t id);
    void (*device_dump)(void);
    
    int (*register_notify_callback)(device_notify_callback_t callback, void* user_data);
    int (*unregister_notify_callback)(device_notify_callback_t callback);
} drvman_api_t;

extern drvman_api_t drvman;

void drvman_init(void);
void drvman_set_path(const char* path);
int drvman_register_builtin(driver_module_t* module);
void drvman_autoprobe(void);
void drvman_scan(void);
int drvman_unload(const char* name);
int drvman_force_load(const char* name);
void drvman_test_pattern(void);

#define DRIVER_REGISTER(NAME, VER, AUTHOR, TYPE, VENDOR, DEVICE) \
    static int __##NAME##_init(driver_module_t* mod, const driver_api_t* api); \
    static int __##NAME##_probe(driver_module_t* mod, hw_device_t* hw_dev); \
    static int __##NAME##_attach(driver_module_t* mod, hw_device_t* hw_dev, driver_connector_t* conn); \
    static int __##NAME##_detach(driver_module_t* mod); \
    static int __##NAME##_unload(driver_module_t* mod); \
    static driver_module_t __##NAME##_module = { \
        .magic = DRV_MAGIC, \
        .api_version_major = DRV_API_VERSION_MAJOR, \
        .api_version_minor = DRV_API_VERSION_MINOR, \
        .api_version_patch = DRV_API_VERSION_PATCH, \
        .name = #NAME, \
        .version = VER, \
        .author = AUTHOR, \
        .type = TYPE, \
        .vendor_id = VENDOR, \
        .device_id = DEVICE, \
        .class_code = 0xFF, \
        .subclass = 0xFF, \
        .match_any_vendor = (VENDOR == 0xFFFF) ? 1 : 0, \
        .match_any_device = (DEVICE == 0xFFFF) ? 1 : 0, \
        .state = DRV_STATE_UNLOADED, \
        .refcount = 0, \
        .init = __##NAME##_init, \
        .probe = __##NAME##_probe, \
        .attach = __##NAME##_attach, \
        .detach = __##NAME##_detach, \
        .unload = __##NAME##_unload, \
    }; \
    __attribute__((constructor)) static void __register_##NAME(void) { \
        drvman_register_builtin(&__##NAME##_module); \
    } \
    static int __##NAME##_init(driver_module_t* mod, const driver_api_t* api) \
    static int __##NAME##_probe(driver_module_t* mod, hw_device_t* hw_dev) \
    static int __##NAME##_attach(driver_module_t* mod, hw_device_t* hw_dev, driver_connector_t* conn) \
    static int __##NAME##_detach(driver_module_t* mod) \
    static int __##NAME##_unload(driver_module_t* mod)

#endif