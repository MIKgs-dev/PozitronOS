#ifndef SCANNER_H
#define SCANNER_H

#include "kernel/types.h"

// ============ ТИПЫ ШИН ============
typedef enum {
    BUS_UNKNOWN = 0,
    BUS_SYSTEM,     // CPU, память
    BUS_PCI,
    BUS_PCIE,
    BUS_ISA,
    BUS_LPC,
    BUS_USB,
    BUS_I2C,
    BUS_SMBUS,
    BUS_ACPI
} bus_type_t;

// ============ ТИПЫ УСТРОЙСТВ ============
typedef enum {
    DEV_UNKNOWN = 0,
    
    // Процессор и память
    DEV_CPU,
    DEV_MEMORY,
    DEV_CACHE,
    
    // Графика
    DEV_GPU_VGA,      // VGA text
    DEV_GPU_VESA,     // VESA framebuffer
    DEV_GPU_INTEL,    // Intel GMA, HD Graphics
    DEV_GPU_NVIDIA,   // NVIDIA
    DEV_GPU_AMD,      // AMD/ATI
    DEV_GPU_VIA,      // VIA Chrome
    DEV_GPU_SIS,      // SiS
    DEV_GPU_MATROX,   // Matrox
    DEV_GPU_CIRRUS,   // Cirrus Logic
    DEV_GPU_QEMU,     // QEMU virtual
    DEV_GPU_VMWARE,   // VMware SVGA
    
    // Накопители
    DEV_DISK_IDE,
    DEV_DISK_SATA,
    DEV_DISK_NVME,
    DEV_DISK_SCSI,
    DEV_DISK_SAS,
    DEV_FLOPPY,
    DEV_OPTICAL,
    DEV_FLASH,
    
    // Сеть
    DEV_NET_ETHERNET,
    DEV_NET_WIFI,
    DEV_NET_BLUETOOTH,
    DEV_NET_OTHER,
    
    // Звук
    DEV_AUDIO_AC97,
    DEV_AUDIO_HD,
    DEV_AUDIO_SB16,
    DEV_AUDIO_ESS,
    DEV_AUDIO_CIRRUS,
    DEV_AUDIO_OTHER,
    
    // Ввод
    DEV_INPUT_PS2_KBD,
    DEV_INPUT_PS2_MOUSE,
    DEV_INPUT_USB_KBD,
    DEV_INPUT_USB_MOUSE,
    DEV_INPUT_JOYSTICK,
    DEV_INPUT_TABLET,
    DEV_INPUT_OTHER,
    
    // Шины и контроллеры
    DEV_USB_HOST,
    DEV_USB_DEVICE,
    DEV_PCI_BRIDGE,
    DEV_PCI_TO_PCI,
    DEV_PCI_TO_ISA,
    DEV_HOST_BRIDGE,
    
    // Системные
    DEV_PIC,
    DEV_TIMER,
    DEV_RTC,
    DEV_PS2,
    DEV_SERIAL,
    DEV_PARALLEL,
    DEV_DMA,
    DEV_CMOS,
    DEV_BIOS,
    DEV_TPM,
    
    // Мультимедиа
    DEV_VIDEO_CAPTURE,
    DEV_TV_TUNER,
    
    // Виртуальные
    DEV_VIRTIO_NET,
    DEV_VIRTIO_BLOCK,
    DEV_VIRTIO_GPU,
    DEV_VIRTIO_INPUT,
    
    // Другие
    DEV_PRINTER,
    DEV_SCANNER,
    DEV_BATTERY,
    DEV_SENSOR
} device_type_t;

// ============ СТАТУС УСТРОЙСТВА ============
typedef enum {
    DEV_STATUS_UNKNOWN = 0,
    DEV_STATUS_WORKING,
    DEV_STATUS_DISABLED,
    DEV_STATUS_FAILED,
    DEV_STATUS_SLEEPING
} device_status_t;

// ============ КОНФИГУРАЦИЯ УСТРОЙСТВА ============
typedef struct {
    uint32_t io_ports[8];
    uint32_t memory_ranges[8];
    uint8_t irqs[8];
    uint8_t dma_channels[4];
    uint32_t clock_speed;      // Hz
    uint32_t data_width;       // bits
    uint32_t address_width;    // bits
} device_config_t;

// ============ PCI ИНФОРМАЦИЯ ============
typedef struct {
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t revision;
    uint8_t header_type;
    uint16_t command;
    uint16_t status;
    uint32_t bars[6];
    uint16_t subsystem_vendor;
    uint16_t subsystem_id;
    uint8_t interrupt_line;
    uint8_t interrupt_pin;
} pci_info_t;

// ============ ISA ИНФОРМАЦИЯ ============
typedef struct {
    uint16_t ports[4];
    uint8_t irqs[2];
    uint8_t dma_channels[2];
} isa_info_t;

// ============ УСТРОЙСТВО ============
typedef struct hw_device {
    // Идентификация
    bus_type_t bus;
    device_type_t type;
    device_status_t status;
    uint32_t instance_id;
    
    // Информация о шине
    union {
        pci_info_t pci;
        isa_info_t isa;
        struct {
            uint64_t base;
            uint64_t size;
            uint32_t type;
        } memory;
        struct {
            uint32_t family;
            uint32_t model;
            uint32_t stepping;
            uint32_t features;
        } cpu;
    };
    
    // Конфигурация
    device_config_t config;
    
    // Метаданные
    char name[64];
    char description[128];
    char driver_name[32];
    char firmware_version[16];
    char hardware_version[16];
    
    // Ресурсы
    void* driver_data;
    uint32_t resource_count;
    void** resources;
    
    // Связи
    struct hw_device* parent;
    struct hw_device* children;
    struct hw_device* sibling;
    struct hw_device* next;
    
    // Состояние
    uint8_t enabled;
    uint8_t initialized;
    uint8_t hot_plug;
    
} hw_device_t;

// ============ API ============

// Инициализация
void scanner_init(void);
void scanner_deinit(void);

// Сканирование
void scanner_scan_all(void);
void scanner_scan_pci(void);
void scanner_scan_isa(void);
void scanner_scan_cpu(void);

// Управление устройствами
hw_device_t* scanner_get_device_list(void);
hw_device_t* scanner_find_by_type(device_type_t type);
hw_device_t* scanner_find_by_pci(uint16_t vendor, uint16_t device);
hw_device_t* scanner_find_by_isa(uint16_t port);
hw_device_t* scanner_find_by_name(const char* name);

// Информация
void scanner_dump_all(void);
void scanner_dump_pci(void);
void scanner_dump_isa(void);
void scanner_dump_tree(void);

// Управление
int scanner_enable_device(hw_device_t* device);
int scanner_disable_device(hw_device_t* device);
int scanner_reset_device(hw_device_t* device);

// Утилиты
const char* scanner_bus_to_string(bus_type_t bus);
const char* scanner_type_to_string(device_type_t type);
const char* scanner_status_to_string(device_status_t status);
uint32_t scanner_get_device_count(void);
uint32_t scanner_get_device_count_by_type(device_type_t type);

// Отладка
void scanner_test_all(void);
void scanner_check_conflicts(void);

#endif // SCANNER_H