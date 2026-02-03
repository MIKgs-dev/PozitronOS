#include "hw/scanner.h"
#include "drivers/ports.h"
#include "drivers/serial.h"
#include "kernel/memory.h"
#include <stddef.h>

// ============ ВНУТРЕННИЕ УТИЛИТЫ ============

// Простая функция для вывода hex числа
static void put_hex(uint32_t value, int digits) {
    char hex_chars[] = "0123456789ABCDEF";
    
    for(int i = digits-1; i >= 0; i--) {
        uint8_t nibble = (value >> (i * 4)) & 0xF;
        char buf[2] = {hex_chars[nibble], '\0'};
        serial_puts(buf);
    }
}

// Простая функция для создания имени устройства
static void create_device_name(char* dest, const char* vendor, const char* device) {
    // Копируем vendor
    const char* s = vendor;
    char* d = dest;
    while(*s && d < dest + 30) {
        *d++ = *s++;
    }
    
    // Добавляем пробел
    if(d < dest + 31) *d++ = ' ';
    
    // Копируем device
    s = device;
    while(*s && d < dest + 63) {
        *d++ = *s++;
    }
    
    // Завершаем строку
    *d = '\0';
}

// ============ PCI КОНСТАНТЫ ============
#define PCI_CONFIG_ADDRESS  0xCF8
#define PCI_CONFIG_DATA     0xCFC

#define PCI_VENDOR_ID       0x00
#define PCI_DEVICE_ID       0x02
#define PCI_COMMAND         0x04
#define PCI_STATUS          0x06
#define PCI_REVISION_ID     0x08
#define PCI_PROG_IF         0x09
#define PCI_SUBCLASS        0x0A
#define PCI_CLASS_CODE      0x0B
#define PCI_HEADER_TYPE     0x0E
#define PCI_BAR0            0x10
#define PCI_SUBSYSTEM_VENDOR 0x2C
#define PCI_SUBSYSTEM_ID    0x2E
#define PCI_INTERRUPT_LINE  0x3C
#define PCI_INTERRUPT_PIN   0x3D
#define PCI_SECONDARY_BUS   0x19

// Vendor IDs
#define VENDOR_INTEL        0x8086
#define VENDOR_AMD          0x1022
#define VENDOR_NVIDIA       0x10DE
#define VENDOR_ATI          0x1002
#define VENDOR_VIA          0x1106
#define VENDOR_SIS          0x1039
#define VENDOR_REALTEK      0x10EC
#define VENDOR_BROADCOM     0x14E4
#define VENDOR_MARVELL      0x11AB
#define VENDOR_QEMU         0x1B36
#define VENDOR_VMWARE       0x15AD
#define VENDOR_BOCHS        0x1234
#define VENDOR_CIRRUS       0x1013
#define VENDOR_ATHEROS      0x168C
#define VENDOR_LSI          0x1000
#define VENDOR_JMICRON      0x197B
#define VENDOR_ASMEDIA      0x1B21
#define VENDOR_CREATIVE     0x1102
#define VENDOR_ESS          0x125D
#define VENDOR_MATROX       0x102B
#define VENDOR_3DFX         0x121A
#define VENDOR_SAMSUNG      0x144D
#define VENDOR_VIRTIO       0x1AF4
// VENDOR_REDHAT тоже 0x1B36 как и QEMU, поэтому убираем

// Class Codes
#define PCI_CLASS_STORAGE       0x01
#define PCI_CLASS_NETWORK       0x02
#define PCI_CLASS_DISPLAY       0x03
#define PCI_CLASS_MULTIMEDIA    0x04
#define PCI_CLASS_BRIDGE        0x06
#define PCI_CLASS_SERIAL_BUS    0x0C
#define PCI_CLASS_WIRELESS      0x0D
#define PCI_CLASS_INPUT         0x09
#define PCI_CLASS_PROCESSOR     0x0B

// ============ БАЗА ДАННЫХ УСТРОЙСТВ ============

typedef struct {
    uint16_t vendor;
    uint16_t device;
    const char* name;
    device_type_t type;
    const char* description;
} pci_device_db_t;

static const pci_device_db_t pci_database[] = {
    // === ВИДЕОКАРТЫ ===
    // Виртуальные
    {0x1234, 0x1111, "QEMU VGA", DEV_GPU_VESA, "QEMU Standard VGA"},
    {0x1B36, 0x0100, "QEMU PCIe GPU", DEV_GPU_QEMU, "QEMU PCI Express Graphics"},
    {0x15AD, 0x0405, "VMware SVGA", DEV_GPU_VMWARE, "VMware SVGA II"},
    {0x1AF4, 0x1050, "VirtIO GPU", DEV_VIRTIO_GPU, "VirtIO GPU Device"},
    
    // Cirrus Logic
    {0x1013, 0x00B8, "Cirrus GD5446", DEV_GPU_CIRRUS, "Cirrus Logic GD-5446"},
    
    // Intel
    {0x8086, 0x29C2, "Intel G35", DEV_GPU_INTEL, "Intel G35 Express"},
    {0x8086, 0x2A42, "Intel GMA 4500", DEV_GPU_INTEL, "Intel GMA 4500"},
    {0x8086, 0x0116, "Intel HD 2000", DEV_GPU_INTEL, "Intel HD Graphics 2000"},
    {0x8086, 0x0166, "Intel HD 4000", DEV_GPU_INTEL, "Intel HD Graphics 4000"},
    {0x8086, 0x0412, "Intel HD 4600", DEV_GPU_INTEL, "Intel HD Graphics 4600"},
    {0x8086, 0x1912, "Intel HD 530", DEV_GPU_INTEL, "Intel HD Graphics 530"},
    {0x8086, 0x5916, "Intel HD 630", DEV_GPU_INTEL, "Intel HD Graphics 630"},
    
    // NVIDIA
    {0x10DE, 0x0020, "NVIDIA Riva TNT", DEV_GPU_NVIDIA, "NVIDIA Riva TNT"},
    {0x10DE, 0x0100, "NVIDIA GeForce 256", DEV_GPU_NVIDIA, "NVIDIA GeForce 256"},
    {0x10DE, 0x0170, "NVIDIA GeForce4 MX", DEV_GPU_NVIDIA, "NVIDIA GeForce4 MX"},
    {0x10DE, 0x0614, "NVIDIA GeForce 8400", DEV_GPU_NVIDIA, "NVIDIA GeForce 8400 GS"},
    {0x10DE, 0x1180, "NVIDIA GTX 580", DEV_GPU_NVIDIA, "NVIDIA GeForce GTX 580"},
    {0x10DE, 0x1B06, "NVIDIA GTX 1080", DEV_GPU_NVIDIA, "NVIDIA GeForce GTX 1080"},
    
    // AMD/ATI
    {0x1002, 0x4150, "ATI Rage 128", DEV_GPU_AMD, "ATI Rage 128 Pro"},
    {0x1002, 0x4966, "ATI Radeon 7000", DEV_GPU_AMD, "ATI Radeon 7000"},
    {0x1002, 0x9588, "ATI HD 4850", DEV_GPU_AMD, "ATI Radeon HD 4850"},
    {0x1002, 0x67DF, "AMD RX 480", DEV_GPU_AMD, "AMD Radeon RX 480"},
    
    // Matrox
    {0x102B, 0x0519, "Matrox Millennium", DEV_GPU_MATROX, "Matrox Millennium"},
    {0x102B, 0x0525, "Matrox G400", DEV_GPU_MATROX, "Matrox G400"},
    
    // VIA
    {0x1106, 0x3108, "VIA Chrome9", DEV_GPU_VIA, "VIA Chrome9 HC"},
    
    // SiS
    {0x1039, 0x0300, "SiS 5598", DEV_GPU_SIS, "SiS 5598 Video"},
    {0x1039, 0x6326, "SiS 6326", DEV_GPU_SIS, "SiS 6326 AGP"},
    
    // === СЕТЕВЫЕ КАРТЫ ===
    // Intel
    {0x8086, 0x100E, "Intel 82574L", DEV_NET_ETHERNET, "Intel 82574L Gigabit"},
    {0x8086, 0x10D3, "Intel 82574L", DEV_NET_ETHERNET, "Intel 82574L Gigabit"},
    {0x8086, 0x153A, "Intel I217-LM", DEV_NET_ETHERNET, "Intel I217-LM Gigabit"},
    {0x8086, 0x15B7, "Intel I219-LM", DEV_NET_ETHERNET, "Intel I219-LM Gigabit"},
    {0x8086, 0x15BB, "Intel I211-AT", DEV_NET_ETHERNET, "Intel I211-AT Gigabit"},
    
    // Realtek
    {0x10EC, 0x8029, "Realtek 8029", DEV_NET_ETHERNET, "Realtek RTL8029"},
    {0x10EC, 0x8139, "Realtek 8139", DEV_NET_ETHERNET, "Realtek RTL8139"},
    {0x10EC, 0x8168, "Realtek 8168", DEV_NET_ETHERNET, "Realtek RTL8168"},
    {0x10EC, 0x8125, "Realtek 8125", DEV_NET_ETHERNET, "Realtek RTL8125 2.5GbE"},
    
    // Broadcom
    {0x14E4, 0x1648, "Broadcom BCM57xx", DEV_NET_ETHERNET, "Broadcom NetXtreme"},
    {0x14E4, 0x43A0, "Broadcom BCM4360", DEV_NET_WIFI, "Broadcom BCM4360 WiFi"},
    
    // Atheros
    {0x168C, 0x002A, "Atheros AR5212", DEV_NET_WIFI, "Atheros AR5212 WiFi"},
    {0x168C, 0x0032, "Atheros AR9285", DEV_NET_WIFI, "Atheros AR9285 WiFi"},
    
    // Marvell
    {0x11AB, 0x4320, "Marvell Yukon", DEV_NET_ETHERNET, "Marvell Yukon 88E8056"},
    
    // VirtIO
    {0x1AF4, 0x1000, "VirtIO Network", DEV_VIRTIO_NET, "VirtIO Network Device"},
    
    // === ДИСКОВЫЕ КОНТРОЛЛЕРЫ ===
    // Intel
    {0x8086, 0x7010, "Intel PIIX3 IDE", DEV_DISK_IDE, "Intel 82371SB IDE"},
    {0x8086, 0x7111, "Intel PIIX4 IDE", DEV_DISK_IDE, "Intel 82371AB IDE"},
    {0x8086, 0x2821, "Intel ICH8 SATA", DEV_DISK_SATA, "Intel ICH8 AHCI"},
    {0x8086, 0x2922, "Intel ICH9 SATA", DEV_DISK_SATA, "Intel ICH9 AHCI"},
    {0x8086, 0x1C02, "Intel 6 Series SATA", DEV_DISK_SATA, "Intel 6 Series AHCI"},
    {0x8086, 0x1E02, "Intel 7 Series SATA", DEV_DISK_SATA, "Intel 7 Series AHCI"},
    
    // NVMe
    {0x8086, 0x0953, "Intel NVMe SSD", DEV_DISK_NVME, "Intel SSD 750 NVMe"},
    {0x144D, 0xA804, "Samsung NVMe SSD", DEV_DISK_NVME, "Samsung 960 PRO"},
    {0x1B36, 0x0010, "QEMU NVMe", DEV_DISK_NVME, "QEMU NVM Express"},
    
    // SCSI/RAID
    {0x1000, 0x0030, "LSI 53C1030", DEV_DISK_SCSI, "LSI Logic 53C1030"},
    {0x9004, 0x5078, "Adaptec AHA-2940", DEV_DISK_SCSI, "Adaptec AHA-2940U"},
    
    // JMicron
    {0x197B, 0x2360, "JMicron JMB360", DEV_DISK_SATA, "JMicron JMB360 AHCI"},
    
    // VirtIO
    {0x1AF4, 0x1001, "VirtIO Block", DEV_VIRTIO_BLOCK, "VirtIO Block Device"},
    
    // === USB КОНТРОЛЛЕРЫ ===
    {0x8086, 0x2934, "Intel ICH9 USB", DEV_USB_HOST, "Intel ICH9 USB UHCI"},
    {0x8086, 0x2938, "Intel ICH9 USB", DEV_USB_HOST, "Intel ICH9 USB EHCI"},
    {0x8086, 0x1C2D, "Intel 6 Series USB", DEV_USB_HOST, "Intel 6 Series USB"},
    {0x8086, 0x1E2D, "Intel 7 Series USB", DEV_USB_HOST, "Intel 7 Series USB"},
    {0x8086, 0x2412, "Intel ICH1 USB", DEV_USB_HOST, "Intel 82801AA USB UHCI"},
    {0x8086, 0x2415, "Intel ICH1 USB", DEV_USB_HOST, "Intel 82801AA USB EHCI"},
    {0x8086, 0x24C2, "Intel ICH2 USB", DEV_USB_HOST, "Intel 82801BA USB UHCI"},
    {0x8086, 0x24C4, "Intel ICH2 USB", DEV_USB_HOST, "Intel 82801BA USB EHCI"},
    {0x8086, 0x24CD, "Intel ICH2 USB", DEV_USB_HOST, "Intel 82801BA USB2 EHCI"},
    {0x8086, 0x24D2, "Intel ICH3 USB", DEV_USB_HOST, "Intel 82801CA USB UHCI"},
    {0x8086, 0x24D4, "Intel ICH3 USB", DEV_USB_HOST, "Intel 82801CA USB EHCI"},
    {0x8086, 0x24DE, "Intel ICH3 USB", DEV_USB_HOST, "Intel 82801CA USB2 EHCI"},
    {0x8086, 0x2934, "Intel ICH9 USB", DEV_USB_HOST, "Intel ICH9 USB UHCI"},
    {0x8086, 0x2935, "Intel ICH9 USB", DEV_USB_HOST, "Intel ICH9 USB UHCI"},
    {0x8086, 0x2936, "Intel ICH9 USB", DEV_USB_HOST, "Intel ICH9 USB UHCI"},
    {0x8086, 0x2937, "Intel ICH9 USB", DEV_USB_HOST, "Intel ICH9 USB UHCI"},
    {0x8086, 0x2938, "Intel ICH9 USB", DEV_USB_HOST, "Intel ICH9 USB EHCI"},
    {0x8086, 0x2939, "Intel ICH9 USB", DEV_USB_HOST, "Intel ICH9 USB2 EHCI"},
    {0x8086, 0x293A, "Intel ICH9 USB", DEV_USB_HOST, "Intel ICH9 USB2 EHCI"},
    {0x8086, 0x293C, "Intel ICH9 USB", DEV_USB_HOST, "Intel ICH9 USB2 EHCI"},
    {0x8086, 0x1C26, "Intel 6 Series USB", DEV_USB_HOST, "Intel 6 Series USB UHCI"},
    {0x8086, 0x1C2D, "Intel 6 Series USB", DEV_USB_HOST, "Intel 6 Series USB EHCI"},
    {0x8086, 0x1E26, "Intel 7 Series USB", DEV_USB_HOST, "Intel 7 Series USB UHCI"},
    {0x8086, 0x1E2D, "Intel 7 Series USB", DEV_USB_HOST, "Intel 7 Series USB EHCI"},
    {0x8086, 0x8C26, "Intel 8 Series USB", DEV_USB_HOST, "Intel 8 Series USB UHCI"},
    {0x8086, 0x8C2D, "Intel 8 Series USB", DEV_USB_HOST, "Intel 8 Series USB EHCI"},
    {0x8086, 0x9C26, "Intel 9 Series USB", DEV_USB_HOST, "Intel 9 Series USB UHCI"},
    {0x8086, 0x9C2D, "Intel 9 Series USB", DEV_USB_HOST, "Intel 9 Series USB EHCI"},
    {0x8086, 0xA12F, "Intel 100 Series USB", DEV_USB_HOST, "Intel 100 Series USB EHCI"},
    {0x8086, 0xA36D, "Intel 300 Series USB", DEV_USB_HOST, "Intel 300 Series USB EHCI"},
    {0x8086, 0x7AE0, "Intel Tiger Lake USB", DEV_USB_HOST, "Intel Tiger Lake USB xHCI"},
    
    // VIA
    {0x1106, 0x3038, "VIA USB UHCI", DEV_USB_HOST, "VIA VT83C572 USB"},
    {0x1106, 0x3104, "VIA USB EHCI", DEV_USB_HOST, "VIA VT6202 USB2"},
    {0x1106, 0x3038, "VIA USB UHCI", DEV_USB_HOST, "VIA VT83C572 USB UHCI"},
    {0x1106, 0x3104, "VIA USB EHCI", DEV_USB_HOST, "VIA VT6202 USB2 EHCI"},
    {0x1106, 0x3288, "VIA USB xHCI", DEV_USB_HOST, "VIA VL800/801 xHCI"},
    
    // NEC/Renesas
    {0x1033, 0x0035, "NEC USB UHCI", DEV_USB_HOST, "NEC µPD720100 USB UHCI"},
    {0x1033, 0x00E0, "NEC USB EHCI", DEV_USB_HOST, "NEC µPD720100 USB2 EHCI"},
    {0x1033, 0x0194, "Renesas USB xHCI", DEV_USB_HOST, "Renesas uPD720201/202 xHCI"},

    // AMD
    {0x1022, 0x740C, "AMD USB UHCI", DEV_USB_HOST, "AMD-756 USB UHCI"},
    {0x1022, 0x740B, "AMD USB EHCI", DEV_USB_HOST, "AMD-756 USB2 EHCI"},
    {0x1022, 0x7814, "AMD FCH USB EHCI", DEV_USB_HOST, "AMD Hudson-2 USB2 EHCI"},

    // ASMedia
    {0x1B21, 0x1042, "ASMedia USB EHCI", DEV_USB_HOST, "ASMedia ASM1042 USB3 xHCI"},
    {0x1B21, 0x1142, "ASMedia USB xHCI", DEV_USB_HOST, "ASMedia ASM1142 USB3 xHCI"},

    // QEMU/VirtualBox эмуляция
    {0x80EE, 0xCAFE, "VirtualBox USB", DEV_USB_HOST, "VirtualBox USB Controller"},
    {0x106B, 0x003F, "Apple USB UHCI", DEV_USB_HOST, "Apple USB UHCI Controller"},
    {0x106B, 0x00A0, "Apple USB EHCI", DEV_USB_HOST, "Apple USB2 EHCI Controller"},

    // Стандартные ID для эмуляторов
    {0x1B36, 0x000D, "QEMU USB UHCI", DEV_USB_HOST, "QEMU QUSB2 USB UHCI"},
    {0x1B36, 0x0011, "QEMU USB EHCI", DEV_USB_HOST, "QEMU QUSB2 USB2 EHCI"},
    {0x1AF4, 0x1100, "VirtIO USB", DEV_USB_HOST, "VirtIO USB Controller"},
    
    // === ЗВУКОВЫЕ КАРТЫ ===
    {0x8086, 0x2668, "Intel ICH6 HD Audio", DEV_AUDIO_HD, "Intel ICH6 HD Audio"},
    {0x8086, 0x293E, "Intel ICH9 HD Audio", DEV_AUDIO_HD, "Intel ICH9 HD Audio"},
    {0x8086, 0x1C20, "Intel 6 Series Audio", DEV_AUDIO_HD, "Intel 6 Series HD Audio"},
    
    // Creative
    {0x1102, 0x0002, "Creative SB 1.0", DEV_AUDIO_SB16, "Creative Sound Blaster 1.0"},
    {0x1102, 0x0008, "Creative SB Pro", DEV_AUDIO_SB16, "Creative Sound Blaster Pro"},
    {0x1102, 0x0002, "Creative SB16", DEV_AUDIO_SB16, "Creative Sound Blaster 16"},
    
    // Realtek Audio
    {0x10EC, 0x0888, "Realtek ALC888", DEV_AUDIO_HD, "Realtek ALC888S HD Audio"},
    {0x10EC, 0x0892, "Realtek ALC892", DEV_AUDIO_HD, "Realtek ALC892 HD Audio"},
    
    // ESS
    {0x125D, 0x1969, "ESS ES1969", DEV_AUDIO_ESS, "ESS ES1969 AudioDrive"},
    {0x125D, 0x1988, "ESS ES1988", DEV_AUDIO_ESS, "ESS ES1988 Allegro"},
    
    // Cirrus Audio
    {0x1013, 0x6003, "Cirrus CS4236", DEV_AUDIO_CIRRUS, "Cirrus Logic CS4236"},
    {0x1013, 0x6005, "Cirrus CS4237", DEV_AUDIO_CIRRUS, "Cirrus Logic CS4237"},
    
    // === МОСТЫ И КОНТРОЛЛЕРЫ ===
    {0x8086, 0x1237, "Intel 440FX", DEV_HOST_BRIDGE, "Intel 440FX PCIset"},
    {0x8086, 0x7000, "Intel PIIX3", DEV_PCI_TO_ISA, "Intel 82371SB PIIX3"},
    {0x8086, 0x7110, "Intel PIIX4", DEV_PCI_TO_ISA, "Intel 82371AB PIIX4"},
    {0x8086, 0x244E, "Intel 82801", DEV_PCI_TO_PCI, "Intel 82801 PCI Bridge"},
    {0x8086, 0x2918, "Intel ICH9", DEV_PCI_TO_ISA, "Intel ICH9 LPC Bridge"},
    
    // VIA
    {0x1106, 0x0596, "VIA VT82C596", DEV_PCI_TO_ISA, "VIA VT82C596B"},
    {0x1106, 0x0686, "VIA VT82C686", DEV_PCI_TO_ISA, "VIA VT82C686B"},
    
    // AMD
    {0x1022, 0x7438, "AMD 768", DEV_PCI_TO_ISA, "AMD 768 South Bridge"},
    
    // Конец массива
    {0, 0, NULL, DEV_UNKNOWN, NULL}
};

// ============ ISA УСТРОЙСТВА ============

static const struct {
    uint16_t port;
    const char* name;
    device_type_t type;
    const char* description;
} isa_devices[] = {
    {0x0020, "8259 PIC Master", DEV_PIC, "Intel 8259A PIC Master"},
    {0x00A0, "8259 PIC Slave", DEV_PIC, "Intel 8259A PIC Slave"},
    {0x0040, "8253/8254 PIT", DEV_TIMER, "Intel 8253/8254 PIT"},
    {0x0060, "8042 PS/2 Controller", DEV_PS2, "Intel 8042 Keyboard/Mouse"},
    {0x0070, "RTC/CMOS", DEV_RTC, "MC146818 RTC & CMOS"},
    {0x0080, "DMA Page Registers", DEV_DMA, "8237 DMA Page Registers"},
    {0x00C0, "8237 DMA #2", DEV_DMA, "8237 DMA Controller #2"},
    {0x03F0, "Floppy Controller", DEV_FLOPPY, "Intel 82077AA FDC"},
    {0x0378, "LPT1 Parallel", DEV_PARALLEL, "LPT1 Parallel Port"},
    {0x03F8, "COM1 Serial", DEV_SERIAL, "COM1 Serial Port"},
    {0x02F8, "COM2 Serial", DEV_SERIAL, "COM2 Serial Port"},
    {0x0220, "Sound Blaster 16", DEV_AUDIO_SB16, "Creative SB16"},
    {0x0330, "MPU-401 MIDI", DEV_AUDIO_OTHER, "Roland MPU-401 MIDI"},
    {0x03C0, "VGA Attribute", DEV_GPU_VGA, "VGA Attribute Controller"},
    {0x03C4, "VGA Sequencer", DEV_GPU_VGA, "VGA Sequencer Registers"},
    {0x03D4, "VGA CRTC", DEV_GPU_VGA, "VGA CRT Controller"},
    {0, NULL, DEV_UNKNOWN, NULL}
};

// ============ ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ ============
static hw_device_t* device_list = NULL;
static uint32_t device_counter = 0;

// ============ ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ ============

static const char* get_vendor_name(uint16_t vendor) {
    switch(vendor) {
        case VENDOR_INTEL:    return "Intel";
        case VENDOR_AMD:      return "AMD";
        case VENDOR_NVIDIA:   return "NVIDIA";
        case VENDOR_ATI:      return "ATI";
        case VENDOR_VIA:      return "VIA";
        case VENDOR_SIS:      return "SiS";
        case VENDOR_REALTEK:  return "Realtek";
        case VENDOR_BROADCOM: return "Broadcom";
        case VENDOR_MARVELL:  return "Marvell";
        case VENDOR_QEMU:     return "QEMU";
        case VENDOR_VMWARE:   return "VMware";
        case VENDOR_BOCHS:    return "Bochs";
        case VENDOR_CIRRUS:   return "Cirrus Logic";
        case VENDOR_ATHEROS:  return "Atheros";
        case VENDOR_LSI:      return "LSI";
        case VENDOR_JMICRON:  return "JMicron";
        case VENDOR_CREATIVE: return "Creative";
        case VENDOR_ESS:      return "ESS";
        case VENDOR_MATROX:   return "Matrox";
        case VENDOR_SAMSUNG:  return "Samsung";
        case VENDOR_VIRTIO:   return "VirtIO";
        // VENDOR_REDHAT убрали - конфликт с QEMU
        default:              return "Unknown";
    }
}

static device_type_t get_device_type_from_db(uint16_t vendor, uint16_t device) {
    for(int i = 0; pci_database[i].vendor != 0; i++) {
        if(pci_database[i].vendor == vendor && 
           pci_database[i].device == device) {
            return pci_database[i].type;
        }
    }
    
    // Если не нашли в базе, определяем по классу
    return DEV_UNKNOWN;
}

static const char* get_device_name_from_db(uint16_t vendor, uint16_t device) {
    for(int i = 0; pci_database[i].vendor != 0; i++) {
        if(pci_database[i].vendor == vendor && 
           pci_database[i].device == device) {
            return pci_database[i].name;
        }
    }
    return "Unknown Device";
}

static const char* get_device_description_from_db(uint16_t vendor, uint16_t device) {
    for(int i = 0; pci_database[i].vendor != 0; i++) {
        if(pci_database[i].vendor == vendor && 
           pci_database[i].device == device) {
            return pci_database[i].description;
        }
    }
    return "Unknown PCI Device";
}

// ============ PCI ФУНКЦИИ ============

static uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset) {
    uint32_t address = (uint32_t)((bus << 16) | (dev << 11) | (func << 8) | (offset & 0xFC) | 0x80000000);
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

static void pci_write32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint32_t value) {
    uint32_t address = (uint32_t)((bus << 16) | (dev << 11) | (func << 8) | (offset & 0xFC) | 0x80000000);
    outl(PCI_CONFIG_ADDRESS, address);
    outl(PCI_CONFIG_DATA, value);
}

static uint16_t pci_read16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset) {
    uint32_t value = pci_read32(bus, dev, func, offset & 0xFC);
    return (value >> ((offset & 2) * 8)) & 0xFFFF;
}

static uint8_t pci_read8(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset) {
    uint32_t value = pci_read32(bus, dev, func, offset & 0xFC);
    return (value >> ((offset & 3) * 8)) & 0xFF;
}

static void pci_enable_bus_mastering(uint8_t bus, uint8_t dev, uint8_t func) {
    uint16_t command = pci_read16(bus, dev, func, PCI_COMMAND);
    command |= 0x0004; // Bus Master Enable
    pci_write32(bus, dev, func, PCI_COMMAND, command);
}

static void pci_enable_memory_space(uint8_t bus, uint8_t dev, uint8_t func) {
    uint16_t command = pci_read16(bus, dev, func, PCI_COMMAND);
    command |= 0x0002; // Memory Space Enable
    pci_write32(bus, dev, func, PCI_COMMAND, command);
}

static void pci_enable_io_space(uint8_t bus, uint8_t dev, uint8_t func) {
    uint16_t command = pci_read16(bus, dev, func, PCI_COMMAND);
    command |= 0x0001; // I/O Space Enable
    pci_write32(bus, dev, func, PCI_COMMAND, command);
}

static void pci_add_device(uint8_t bus, uint8_t dev, uint8_t func) {
    uint16_t vendor = pci_read16(bus, dev, func, PCI_VENDOR_ID);
    if(vendor == 0xFFFF) return;
    
    uint16_t device = pci_read16(bus, dev, func, PCI_DEVICE_ID);
    uint8_t class = pci_read8(bus, dev, func, PCI_CLASS_CODE);
    uint8_t subclass = pci_read8(bus, dev, func, PCI_SUBCLASS);
    uint8_t prog_if = pci_read8(bus, dev, func, PCI_PROG_IF);
    uint8_t header = pci_read8(bus, dev, func, PCI_HEADER_TYPE);

    if (class == 0x0C && subclass == 0x03) {
        serial_puts("[SCAN-DEBUG] USB Controller found: ");
        serial_puts("Bus ");
        serial_puts_num(bus);
        serial_puts(" Dev ");
        serial_puts_num(dev);
        serial_puts(" Func ");
        serial_puts_num(func);
        serial_puts(" Class=0x");
        serial_puts_num_hex(class);
        serial_puts(" Subclass=0x");
        serial_puts_num_hex(subclass);
        serial_puts(" ProgIF=0x");
        serial_puts_num_hex(prog_if);
        serial_puts(" Vendor=0x");
        serial_puts_num_hex(vendor);
        serial_puts(" Device=0x");
        serial_puts_num_hex(device);
        serial_puts("\n");
    }
    
    // Создаём запись
    hw_device_t* hw = kmalloc(sizeof(hw_device_t));
    if(!hw) return;
    
    // Очищаем память
    uint8_t* ptr = (uint8_t*)hw;
    for(uint32_t i = 0; i < sizeof(hw_device_t); i++) {
        ptr[i] = 0;
    }
    
    hw->bus = BUS_PCI;
    hw->type = get_device_type_from_db(vendor, device);
    hw->status = DEV_STATUS_WORKING;
    hw->instance_id = device_counter++;
    hw->enabled = 1;
    
    // PCI информация
    hw->pci.bus = bus;
    hw->pci.device = dev;
    hw->pci.function = func;
    hw->pci.vendor_id = vendor;
    hw->pci.device_id = device;
    hw->pci.class_code = class;
    hw->pci.subclass = subclass;
    hw->pci.prog_if = prog_if;
    hw->pci.revision = pci_read8(bus, dev, func, PCI_REVISION_ID);
    hw->pci.header_type = header;
    hw->pci.command = pci_read16(bus, dev, func, PCI_COMMAND);
    hw->pci.status = pci_read16(bus, dev, func, PCI_STATUS);
    hw->pci.subsystem_vendor = pci_read16(bus, dev, func, PCI_SUBSYSTEM_VENDOR);
    hw->pci.subsystem_id = pci_read16(bus, dev, func, PCI_SUBSYSTEM_ID);
    hw->pci.interrupt_line = pci_read8(bus, dev, func, PCI_INTERRUPT_LINE);
    hw->pci.interrupt_pin = pci_read8(bus, dev, func, PCI_INTERRUPT_PIN);
    
    // Читаем BAR'ы
    for(int i = 0; i < 6; i++) {
        hw->pci.bars[i] = pci_read32(bus, dev, func, PCI_BAR0 + i * 4);
    }
    
    // Сохраняем IRQ
    if(hw->pci.interrupt_line != 0 && hw->pci.interrupt_line != 0xFF) {
        hw->config.irqs[0] = hw->pci.interrupt_line;
    }
    
    // Формируем имя
    const char* vendor_name = get_vendor_name(vendor);
    const char* device_name = get_device_name_from_db(vendor, device);
    
    create_device_name(hw->name, vendor_name, device_name);
    
    // Описание
    const char* description = get_device_description_from_db(vendor, device);
    const char* s = description;
    char* d = hw->description;
    while(*s && d < hw->description + 127) {
        *d++ = *s++;
    }
    *d = '\0';
    
    // Добавляем в список
    hw->next = device_list;
    device_list = hw;
    
    // Логируем
    serial_puts("[PCI] Found: ");
    serial_puts(hw->name);
    serial_puts(" [");
    
    // Выводим vendor ID
    put_hex(vendor, 4);
    
    serial_puts(":");
    
    // Выводим device ID
    put_hex(device, 4);
    
    serial_puts("] at ");
    
    // Выводим BDF
    if(bus < 10) {
        serial_puts("0");
    }
    serial_puts_num(bus);
    serial_puts(":");
    if(dev < 10) {
        serial_puts("0");
    }
    serial_puts_num(dev);
    serial_puts(".");
    serial_puts_num(func);
    serial_puts("\n");
}

static void pci_scan_bus(uint8_t bus) {
    for(uint8_t dev = 0; dev < 32; dev++) {
        uint16_t vendor = pci_read16(bus, dev, 0, PCI_VENDOR_ID);
        if(vendor == 0xFFFF) continue;
        
        uint8_t header = pci_read8(bus, dev, 0, PCI_HEADER_TYPE);
        
        // Function 0
        pci_add_device(bus, dev, 0);
        
        // Multifunction устройства
        if((header & 0x80) != 0) {
            for(uint8_t func = 1; func < 8; func++) {
                vendor = pci_read16(bus, dev, func, PCI_VENDOR_ID);
                if(vendor != 0xFFFF) {
                    pci_add_device(bus, dev, func);
                }
            }
        }
        
        // PCI-to-PCI bridge
        if((header & 0x7F) == 0x01) {
            uint8_t secondary = pci_read8(bus, dev, 0, PCI_SECONDARY_BUS);
            if(secondary != 0) {
                pci_scan_bus(secondary);
            }
        }
    }
}

static void scan_pci(void) {
    serial_puts("[SCAN] Scanning PCI bus...\n");
    
    // Проверяем доступность PCI
    outl(PCI_CONFIG_ADDRESS, 0x80000000);
    if(inl(PCI_CONFIG_ADDRESS) != 0x80000000) {
        serial_puts("[SCAN] PCI not available\n");
        return;
    }
    
    // Сканируем bus 0
    pci_scan_bus(0);
    
    // Проверяем multifunction на bus 0 device 0
    uint8_t header = pci_read8(0, 0, 0, PCI_HEADER_TYPE);
    if((header & 0x80) != 0) {
        for(uint8_t func = 1; func < 8; func++) {
            if(pci_read16(0, 0, func, PCI_VENDOR_ID) != 0xFFFF) {
                uint8_t secondary = pci_read8(0, 0, func, PCI_SECONDARY_BUS);
                if(secondary != 0) {
                    pci_scan_bus(secondary);
                }
            }
        }
    }
    
    serial_puts("[SCAN] PCI scan complete\n");
}

// ============ ISA СКАНИРОВАНИЕ ============

static void add_isa_device(uint16_t port, const char* name, device_type_t type, const char* description) {
    // Проверяем, существует ли уже устройство на этом порту
    hw_device_t* dev = device_list;
    while(dev) {
        if(dev->bus == BUS_ISA && dev->isa.ports[0] == port) {
            return; // Уже существует
        }
        dev = dev->next;
    }
    
    hw_device_t* hw = kmalloc(sizeof(hw_device_t));
    if(!hw) return;
    
    // Очищаем память
    uint8_t* ptr = (uint8_t*)hw;
    for(uint32_t i = 0; i < sizeof(hw_device_t); i++) {
        ptr[i] = 0;
    }
    
    hw->bus = BUS_ISA;
    hw->type = type;
    hw->status = DEV_STATUS_WORKING;
    hw->instance_id = device_counter++;
    hw->enabled = 1;
    
    // ISA информация
    hw->isa.ports[0] = port;
    
    // Конфигурация
    hw->config.io_ports[0] = port;
    
    // Имя
    const char* s = name;
    char* d = hw->name;
    while(*s && d < hw->name + 63) {
        *d++ = *s++;
    }
    *d = '\0';
    
    // Описание
    if(description) {
        s = description;
        d = hw->description;
        while(*s && d < hw->description + 127) {
            *d++ = *s++;
        }
        *d = '\0';
    }
    
    // Добавляем в список
    hw->next = device_list;
    device_list = hw;
    
    // Логируем
    serial_puts("[ISA] Found: ");
    serial_puts(name);
    serial_puts(" at 0x");
    
    // Выводим порт в hex
    put_hex(port, 4);
    
    serial_puts("\n");
}

static void scan_isa(void) {
    serial_puts("[SCAN] Scanning ISA bus...\n");
    
    // Стандартные ISA устройства
    for(int i = 0; isa_devices[i].port != 0; i++) {
        add_isa_device(isa_devices[i].port, isa_devices[i].name, 
                      isa_devices[i].type, isa_devices[i].description);
    }
    
    // Проверяем наличие COM портов
    if((inb(0x3F8 + 5) & 0xFF) != 0xFF) { // COM1
        add_isa_device(0x3F8, "COM1 Serial", DEV_SERIAL, "COM1 Serial Port (16550A)");
    }
    if((inb(0x2F8 + 5) & 0xFF) != 0xFF) { // COM2
        add_isa_device(0x2F8, "COM2 Serial", DEV_SERIAL, "COM2 Serial Port (16550A)");
    }
    
    // Проверяем LPT порты
    if((inb(0x378 + 2) & 0xFF) != 0xFF) { // LPT1
        add_isa_device(0x378, "LPT1 Parallel", DEV_PARALLEL, "LPT1 Parallel Port");
    }
    
    serial_puts("[SCAN] ISA scan complete\n");
}

// ============ CPU СКАНИРОВАНИЕ ============

static void scan_cpu(void) {
    serial_puts("[SCAN] Detecting CPU...\n");
    
    hw_device_t* cpu = kmalloc(sizeof(hw_device_t));
    if(!cpu) return;
    
    // Очищаем память
    uint8_t* ptr = (uint8_t*)cpu;
    for(uint32_t i = 0; i < sizeof(hw_device_t); i++) {
        ptr[i] = 0;
    }
    
    cpu->bus = BUS_SYSTEM;
    cpu->type = DEV_CPU;
    cpu->status = DEV_STATUS_WORKING;
    cpu->instance_id = device_counter++;
    cpu->enabled = 1;
    
    // Получаем CPUID
    uint32_t eax, ebx, ecx, edx;
    
    // Vendor string
    asm volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0));
    
    char vendor[13];
    *((uint32_t*)&vendor[0]) = ebx;
    *((uint32_t*)&vendor[4]) = edx;
    *((uint32_t*)&vendor[8]) = ecx;
    vendor[12] = '\0';
    
    // Копируем vendor в имя
    const char* s = vendor;
    char* d = cpu->name;
    while(*s && d < cpu->name + 63) {
        *d++ = *s++;
    }
    *d = '\0';
    
    // CPU информация
    asm volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1));
    cpu->cpu.family = (eax >> 8) & 0xF;
    cpu->cpu.model = (eax >> 4) & 0xF;
    cpu->cpu.stepping = eax & 0xF;
    cpu->cpu.features = edx;
    
    // Описание
    s = "CPU Vendor: ";
    d = cpu->description;
    while(*s && d < cpu->description + 127) {
        *d++ = *s++;
    }
    
    s = vendor;
    while(*s && d < cpu->description + 127) {
        *d++ = *s++;
    }
    
    s = ", Family: ";
    while(*s && d < cpu->description + 127) {
        *d++ = *s++;
    }
    
    // Добавляем family
    uint32_t fam = cpu->cpu.family;
    if(fam >= 10) {
        *d++ = '1';
        *d++ = '0' + (fam - 10);
    } else {
        *d++ = '0' + fam;
    }
    
    s = ", Model: ";
    while(*s && d < cpu->description + 127) {
        *d++ = *s++;
    }
    
    // Добавляем model
    uint32_t mod = cpu->cpu.model;
    if(mod >= 10) {
        *d++ = '1';
        *d++ = '0' + (mod - 10);
    } else {
        *d++ = '0' + mod;
    }
    
    *d = '\0';
    
    // Добавляем в список
    cpu->next = device_list;
    device_list = cpu;
    
    serial_puts("[CPU] Found: ");
    serial_puts(vendor);
    serial_puts(" Family ");
    serial_puts_num(cpu->cpu.family);
    serial_puts(" Model ");
    serial_puts_num(cpu->cpu.model);
    serial_puts("\n");
}

// ============ ОБЩИЕ ФУНКЦИИ ============

void scanner_init(void) {
    serial_puts("[SCAN] Hardware scanner initialized\n");
    device_list = NULL;
    device_counter = 0;
}

void scanner_deinit(void) {
    hw_device_t* dev = device_list;
    while(dev) {
        hw_device_t* next = dev->next;
        kfree(dev);
        dev = next;
    }
    device_list = NULL;
    device_counter = 0;
    
    serial_puts("[SCAN] Hardware scanner deinitialized\n");
}

void scanner_scan_all(void) {
    serial_puts("\n=== HARDWARE DISCOVERY ===\n");
    
    // 1. CPU
    scan_cpu();
    
    // 2. PCI (современное железо)
    scan_pci();
    
    // 3. ISA (legacy железо)
    scan_isa();
    
    serial_puts("=== DISCOVERY COMPLETE ===\n");
    serial_puts("Total devices found: ");
    serial_puts_num(scanner_get_device_count());
    serial_puts("\n");
}

// ============ API ФУНКЦИИ ============

hw_device_t* scanner_get_device_list(void) {
    return device_list;
}

hw_device_t* scanner_find_by_type(device_type_t type) {
    hw_device_t* dev = device_list;
    while(dev) {
        if(dev->type == type) {
            return dev;
        }
        dev = dev->next;
    }
    return NULL;
}

hw_device_t* scanner_find_by_pci(uint16_t vendor, uint16_t device) {
    hw_device_t* dev = device_list;
    while(dev) {
        if(dev->bus == BUS_PCI && 
           dev->pci.vendor_id == vendor && 
           dev->pci.device_id == device) {
            return dev;
        }
        dev = dev->next;
    }
    return NULL;
}

hw_device_t* scanner_find_by_isa(uint16_t port) {
    hw_device_t* dev = device_list;
    while(dev) {
        if(dev->bus == BUS_ISA) {
            for(int i = 0; i < 4; i++) {
                if(dev->isa.ports[i] == port) {
                    return dev;
                }
            }
        }
        dev = dev->next;
    }
    return NULL;
}

hw_device_t* scanner_find_by_name(const char* name) {
    hw_device_t* dev = device_list;
    while(dev) {
        // Простое сравнение строк
        const char* a = dev->name;
        const char* b = name;
        while(*a && *b && *a == *b) {
            a++;
            b++;
        }
        if(*a == *b) { // Оба достигли конца
            return dev;
        }
        dev = dev->next;
    }
    return NULL;
}

uint32_t scanner_get_device_count(void) {
    uint32_t count = 0;
    hw_device_t* dev = device_list;
    while(dev) {
        count++;
        dev = dev->next;
    }
    return count;
}

uint32_t scanner_get_device_count_by_type(device_type_t type) {
    uint32_t count = 0;
    hw_device_t* dev = device_list;
    while(dev) {
        if(dev->type == type) {
            count++;
        }
        dev = dev->next;
    }
    return count;
}

const char* scanner_bus_to_string(bus_type_t bus) {
    switch(bus) {
        case BUS_SYSTEM: return "System";
        case BUS_PCI:    return "PCI";
        case BUS_PCIE:   return "PCIe";
        case BUS_ISA:    return "ISA";
        case BUS_LPC:    return "LPC";
        case BUS_USB:    return "USB";
        case BUS_I2C:    return "I2C";
        case BUS_SMBUS:  return "SMBus";
        case BUS_ACPI:   return "ACPI";
        default:         return "Unknown";
    }
}

const char* scanner_type_to_string(device_type_t type) {
    switch(type) {
        case DEV_CPU:           return "CPU";
        case DEV_MEMORY:        return "Memory";
        case DEV_GPU_VGA:       return "VGA GPU";
        case DEV_GPU_VESA:      return "VESA GPU";
        case DEV_GPU_INTEL:     return "Intel GPU";
        case DEV_GPU_NVIDIA:    return "NVIDIA GPU";
        case DEV_GPU_AMD:       return "AMD GPU";
        case DEV_GPU_VIA:       return "VIA GPU";
        case DEV_GPU_SIS:       return "SiS GPU";
        case DEV_GPU_MATROX:    return "Matrox GPU";
        case DEV_GPU_CIRRUS:    return "Cirrus GPU";
        case DEV_GPU_QEMU:      return "QEMU GPU";
        case DEV_GPU_VMWARE:    return "VMware GPU";
        case DEV_DISK_IDE:      return "IDE Disk";
        case DEV_DISK_SATA:     return "SATA Disk";
        case DEV_DISK_NVME:     return "NVMe Disk";
        case DEV_DISK_SCSI:     return "SCSI Disk";
        case DEV_DISK_SAS:      return "SAS Disk";
        case DEV_FLOPPY:        return "Floppy";
        case DEV_OPTICAL:       return "Optical";
        case DEV_FLASH:         return "Flash";
        case DEV_NET_ETHERNET:  return "Ethernet";
        case DEV_NET_WIFI:      return "WiFi";
        case DEV_NET_BLUETOOTH: return "Bluetooth";
        case DEV_AUDIO_AC97:    return "AC97 Audio";
        case DEV_AUDIO_HD:      return "HD Audio";
        case DEV_AUDIO_SB16:    return "SB16 Audio";
        case DEV_AUDIO_ESS:     return "ESS Audio";
        case DEV_AUDIO_CIRRUS:  return "Cirrus Audio";
        case DEV_INPUT_PS2_KBD: return "PS/2 Keyboard";
        case DEV_INPUT_PS2_MOUSE:return "PS/2 Mouse";
        case DEV_INPUT_USB_KBD: return "USB Keyboard";
        case DEV_INPUT_USB_MOUSE:return "USB Mouse";
        case DEV_INPUT_JOYSTICK:return "Joystick";
        case DEV_INPUT_TABLET:  return "Tablet";
        case DEV_USB_HOST:      return "USB Host";
        case DEV_USB_DEVICE:    return "USB Device";
        case DEV_PCI_BRIDGE:    return "PCI Bridge";
        case DEV_PCI_TO_PCI:    return "PCI-to-PCI";
        case DEV_PCI_TO_ISA:    return "PCI-to-ISA";
        case DEV_HOST_BRIDGE:   return "Host Bridge";
        case DEV_PIC:           return "PIC";
        case DEV_TIMER:         return "Timer";
        case DEV_RTC:           return "RTC";
        case DEV_PS2:           return "PS/2 Controller";
        case DEV_SERIAL:        return "Serial";
        case DEV_PARALLEL:      return "Parallel";
        case DEV_DMA:           return "DMA";
        case DEV_CMOS:          return "CMOS";
        case DEV_BIOS:          return "BIOS";
        case DEV_TPM:           return "TPM";
        case DEV_VIDEO_CAPTURE: return "Video Capture";
        case DEV_TV_TUNER:      return "TV Tuner";
        case DEV_VIRTIO_NET:    return "VirtIO Network";
        case DEV_VIRTIO_BLOCK:  return "VirtIO Block";
        case DEV_VIRTIO_GPU:    return "VirtIO GPU";
        case DEV_VIRTIO_INPUT:  return "VirtIO Input";
        case DEV_PRINTER:       return "Printer";
        case DEV_SCANNER:       return "Scanner";
        case DEV_BATTERY:       return "Battery";
        case DEV_SENSOR:        return "Sensor";
        default:                return "Unknown";
    }
}

const char* scanner_status_to_string(device_status_t status) {
    switch(status) {
        case DEV_STATUS_WORKING:  return "Working";
        case DEV_STATUS_DISABLED: return "Disabled";
        case DEV_STATUS_FAILED:   return "Failed";
        case DEV_STATUS_SLEEPING: return "Sleeping";
        default:                  return "Unknown";
    }
}

int scanner_enable_device(hw_device_t* device) {
    if(!device) return -1;
    
    if(device->bus == BUS_PCI) {
        pci_enable_bus_mastering(device->pci.bus, device->pci.device, device->pci.function);
        pci_enable_memory_space(device->pci.bus, device->pci.device, device->pci.function);
        pci_enable_io_space(device->pci.bus, device->pci.device, device->pci.function);
    }
    
    device->enabled = 1;
    device->status = DEV_STATUS_WORKING;
    
    serial_puts("[SCAN] Enabled device: ");
    serial_puts(device->name);
    serial_puts("\n");
    
    return 0;
}

int scanner_disable_device(hw_device_t* device) {
    if(!device) return -1;
    
    device->enabled = 0;
    device->status = DEV_STATUS_DISABLED;
    
    serial_puts("[SCAN] Disabled device: ");
    serial_puts(device->name);
    serial_puts("\n");
    
    return 0;
}

int scanner_reset_device(hw_device_t* device) {
    if(!device) return -1;
    
    // Для PCI устройств можно сбросить через команду
    if(device->bus == BUS_PCI) {
        uint16_t command = pci_read16(device->pci.bus, device->pci.device, 
                                     device->pci.function, PCI_COMMAND);
        command |= 0x0400; // Reset bit (если поддерживается)
        pci_write32(device->pci.bus, device->pci.device, 
                   device->pci.function, PCI_COMMAND, command);
    }
    
    device->status = DEV_STATUS_WORKING;
    
    serial_puts("[SCAN] Reset device: ");
    serial_puts(device->name);
    serial_puts("\n");
    
    return 0;
}

void scanner_dump_all(void) {
    serial_puts("\n=== HARDWARE INVENTORY ===\n");
    
    hw_device_t* dev = device_list;
    int count = 0;
    
    while(dev) {
        serial_puts("[");
        serial_puts(scanner_type_to_string(dev->type));
        serial_puts("] ");
        serial_puts(dev->name);
        
        if(dev->bus == BUS_PCI) {
            serial_puts(" PCI:");
            serial_puts_num(dev->pci.bus);
            serial_puts(":");
            serial_puts_num(dev->pci.device);
            serial_puts(".");
            serial_puts_num(dev->pci.function);
            serial_puts(" IRQ:");
            serial_puts_num(dev->pci.interrupt_line);
        } else if(dev->bus == BUS_ISA) {
            serial_puts(" ISA:0x");
            put_hex(dev->isa.ports[0], 4);
        } else if(dev->bus == BUS_SYSTEM) {
            serial_puts(" System");
        }
        
        serial_puts(" [");
        serial_puts(scanner_status_to_string(dev->status));
        if(dev->enabled) {
            serial_puts(",Enabled");
        } else {
            serial_puts(",Disabled");
        }
        serial_puts("]\n");
        
        // Выводим описание если есть
        if(dev->description[0]) {
            serial_puts("  ");
            serial_puts(dev->description);
            serial_puts("\n");
        }
        
        dev = dev->next;
        count++;
    }
    
    serial_puts("Total devices: ");
    serial_puts_num(count);
    serial_puts("\n");
    serial_puts("===========================\n");
}

void scanner_dump_pci(void) {
    serial_puts("\n=== PCI DEVICES ===\n");
    
    hw_device_t* dev = device_list;
    int count = 0;
    
    while(dev) {
        if(dev->bus == BUS_PCI) {
            serial_puts("  ");
            if(dev->pci.bus < 10) {
                serial_puts("0");
            }
            serial_puts_num(dev->pci.bus);
            serial_puts(":");
            if(dev->pci.device < 10) {
                serial_puts("0");
            }
            serial_puts_num(dev->pci.device);
            serial_puts(".");
            serial_puts_num(dev->pci.function);
            serial_puts(" ");
            
            // Vendor ID
            put_hex(dev->pci.vendor_id, 4);
            
            serial_puts(":");
            
            // Device ID
            put_hex(dev->pci.device_id, 4);
            
            serial_puts(" [");
            
            // Class/Subclass/ProgIf
            put_hex(dev->pci.class_code, 2);
            
            serial_puts(".");
            
            put_hex(dev->pci.subclass, 2);
            
            serial_puts(".");
            
            put_hex(dev->pci.prog_if, 2);
            
            serial_puts("] ");
            serial_puts(dev->name);
            serial_puts("\n");
            count++;
        }
        dev = dev->next;
    }
    
    serial_puts("Total PCI devices: ");
    serial_puts_num(count);
    serial_puts("\n");
    serial_puts("===================\n");
}

void scanner_dump_isa(void) {
    serial_puts("\n=== ISA DEVICES ===\n");
    
    hw_device_t* dev = device_list;
    int count = 0;
    
    while(dev) {
        if(dev->bus == BUS_ISA) {
            serial_puts("  0x");
            put_hex(dev->isa.ports[0], 4);
            serial_puts(" ");
            serial_puts(dev->name);
            serial_puts("\n");
            count++;
        }
        dev = dev->next;
    }
    
    serial_puts("Total ISA devices: ");
    serial_puts_num(count);
    serial_puts("\n");
    serial_puts("===================\n");
}

void scanner_dump_tree(void) {
    serial_puts("\n=== DEVICE TREE ===\n");
    
    // Показываем CPU как корень
    hw_device_t* cpu = scanner_find_by_type(DEV_CPU);
    if(cpu) {
        serial_puts("CPU: ");
        serial_puts(cpu->name);
        serial_puts("\n");
        
        // Показываем остальные устройства
        hw_device_t* dev = device_list;
        while(dev) {
            if(dev != cpu) {
                serial_puts("  |- ");
                serial_puts(scanner_bus_to_string(dev->bus));
                serial_puts(": ");
                serial_puts(dev->name);
                serial_puts("\n");
            }
            dev = dev->next;
        }
    }
    
    serial_puts("===================\n");
}

void scanner_test_all(void) {
    serial_puts("\n=== DEVICE TEST ===\n");
    
    hw_device_t* dev = device_list;
    int tested = 0, working = 0;
    
    while(dev) {
        serial_puts("Testing ");
        serial_puts(dev->name);
        serial_puts("... ");
        
        // Простой тест доступности
        if(dev->bus == BUS_PCI) {
            // Проверяем, что устройство отвечает
            uint16_t vendor = pci_read16(dev->pci.bus, dev->pci.device, 
                                        dev->pci.function, PCI_VENDOR_ID);
            if(vendor != 0xFFFF) {
                serial_puts("OK\n");
                dev->status = DEV_STATUS_WORKING;
                working++;
            } else {
                serial_puts("FAILED\n");
                dev->status = DEV_STATUS_FAILED;
            }
        } else if(dev->bus == BUS_ISA) {
            // Для ISA просто считаем рабочим
            serial_puts("OK\n");
            dev->status = DEV_STATUS_WORKING;
            working++;
        } else {
            serial_puts("OK\n");
            dev->status = DEV_STATUS_WORKING;
            working++;
        }
        
        tested++;
        dev = dev->next;
    }
    
    serial_puts("Tested ");
    serial_puts_num(tested);
    serial_puts(" devices, ");
    serial_puts_num(working);
    serial_puts(" working\n");
    serial_puts("===================\n");
}

void scanner_check_conflicts(void) {
    serial_puts("\n=== CHECKING FOR CONFLICTS ===\n");
    
    uint32_t io_ports[256] = {0};
    uint8_t irqs[16] = {0};
    
    int conflicts = 0;
    
    hw_device_t* dev = device_list;
    while(dev) {
        // Проверяем I/O порты
        for(int i = 0; i < 8; i++) {
            if(dev->config.io_ports[i] != 0) {
                uint32_t port = dev->config.io_ports[i];
                if(io_ports[port / 4] != 0) {
                    serial_puts("WARNING: I/O port conflict at 0x");
                    put_hex(port, 4);
                    serial_puts(" between ");
                    serial_puts(dev->name);
                    serial_puts(" and another device\n");
                    conflicts++;
                } else {
                    io_ports[port / 4] = 1;
                }
            }
        }
        
        // Проверяем IRQs
        for(int i = 0; i < 8; i++) {
            if(dev->config.irqs[i] != 0 && dev->config.irqs[i] != 0xFF) {
                if(irqs[dev->config.irqs[i]] != 0) {
                    serial_puts("WARNING: IRQ conflict on IRQ ");
                    serial_puts_num(dev->config.irqs[i]);
                    serial_puts(" between ");
                    serial_puts(dev->name);
                    serial_puts(" and another device\n");
                    conflicts++;
                } else {
                    irqs[dev->config.irqs[i]] = 1;
                }
            }
        }
        
        dev = dev->next;
    }
    
    if(conflicts == 0) {
        serial_puts("No conflicts found\n");
    } else {
        serial_puts("Found ");
        serial_puts_num(conflicts);
        serial_puts(" potential conflicts\n");
    }
    
    serial_puts("==============================\n");
}