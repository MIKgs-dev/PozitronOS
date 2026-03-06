#include "drivers/pci.h"
#include "drivers/serial.h"
#include "kernel/ports.h"
#include "kernel/memory.h"
#include "lib/string.h"

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

// PCI cache entry
typedef struct {
    uint16_t vendor;
    uint16_t device;
    uint8_t class;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t bus;
    uint8_t dev;
    uint8_t func;
    uint8_t header;
} pci_cache_entry_t;

// PCI cache
static pci_cache_entry_t pci_cache[256];
static int pci_cache_count = 0;
static uint8_t pci_cache_initialized = 0;

// ==================== PCI CONFIGURATION ACCESS ====================

uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset) {
    uint32_t address = 0x80000000 | (bus << 16) | ((dev & 0x1F) << 11) | 
                       ((func & 0x07) << 8) | (offset & 0xFC);
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

uint16_t pci_read16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset) {
    uint32_t value = pci_read32(bus, dev, func, offset & 0xFC);
    return (value >> ((offset & 2) * 8)) & 0xFFFF;
}

uint8_t pci_read8(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset) {
    uint32_t value = pci_read32(bus, dev, func, offset & 0xFC);
    return (value >> ((offset & 3) * 8)) & 0xFF;
}

void pci_write32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint32_t value) {
    uint32_t address = 0x80000000 | (bus << 16) | ((dev & 0x1F) << 11) | 
                       ((func & 0x07) << 8) | (offset & 0xFC);
    outl(PCI_CONFIG_ADDRESS, address);
    outl(PCI_CONFIG_DATA, value);
}

void pci_write16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint16_t value) {
    uint32_t temp = pci_read32(bus, dev, func, offset & 0xFC);
    
    if (offset & 2) {
        temp = (temp & 0x0000FFFF) | ((uint32_t)value << 16);
    } else {
        temp = (temp & 0xFFFF0000) | value;
    }
    
    pci_write32(bus, dev, func, offset & 0xFC, temp);
}

void pci_write8(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint8_t value) {
    uint32_t temp = pci_read32(bus, dev, func, offset & 0xFC);
    int shift = (offset & 3) * 8;
    temp = (temp & ~(0xFF << shift)) | ((uint32_t)value << shift);
    pci_write32(bus, dev, func, offset & 0xFC, temp);
}

// ==================== PCI UTILITIES ====================

void pci_enable_bus_master(uint8_t bus, uint8_t dev, uint8_t func) {
    uint16_t cmd = pci_read16(bus, dev, func, 0x04);
    cmd |= 0x0004;  // Bus Master Enable
    pci_write16(bus, dev, func, 0x04, cmd);
}

void pci_enable_memory_space(uint8_t bus, uint8_t dev, uint8_t func) {
    uint16_t cmd = pci_read16(bus, dev, func, 0x04);
    cmd |= 0x0002;  // Memory Space Enable
    pci_write16(bus, dev, func, 0x04, cmd);
}

void pci_enable_io_space(uint8_t bus, uint8_t dev, uint8_t func) {
    uint16_t cmd = pci_read16(bus, dev, func, 0x04);
    cmd |= 0x0001;  // I/O Space Enable
    pci_write16(bus, dev, func, 0x04, cmd);
}

// ==================== PCI CACHE BUILD ====================

static void pci_build_cache(void) {
    if (pci_cache_initialized) return;
    
    serial_puts("[PCI] Building device cache...\n");
    
    pci_cache_count = 0;
    
    for (uint16_t bus = 0; bus < 256 && pci_cache_count < 256; bus++) {
        for (uint8_t dev = 0; dev < 32 && pci_cache_count < 256; dev++) {
            uint16_t vendor = pci_read16(bus, dev, 0, 0);
            if (vendor == 0xFFFF) continue;
            
            uint8_t header = pci_read8(bus, dev, 0, 0x0E);
            int max_func = (header & 0x80) ? 8 : 1;
            
            for (uint8_t func = 0; func < max_func && pci_cache_count < 256; func++) {
                vendor = pci_read16(bus, dev, func, 0);
                if (vendor == 0xFFFF) continue;
                
                pci_cache_entry_t* entry = &pci_cache[pci_cache_count++];
                entry->vendor = vendor;
                entry->device = pci_read16(bus, dev, func, 0x02);
                entry->class = pci_read8(bus, dev, func, 0x0B);
                entry->subclass = pci_read8(bus, dev, func, 0x0A);
                entry->prog_if = pci_read8(bus, dev, func, 0x09);
                entry->bus = bus;
                entry->dev = dev;
                entry->func = func;
                entry->header = header;
            }
        }
    }
    
    pci_cache_initialized = 1;
    serial_puts("[PCI] Cache built with ");
    serial_puts_num(pci_cache_count);
    serial_puts(" devices\n");
}

// ==================== PCI DEVICE FINDING ====================

pci_device_t pci_find_class(uint8_t class, uint8_t subclass, uint8_t prog_if) {
    pci_device_t result = {0xFF, 0xFF, 0xFF};
    
    pci_build_cache();
    
    for (int i = 0; i < pci_cache_count; i++) {
        pci_cache_entry_t* entry = &pci_cache[i];
        
        if (entry->class == class && 
            (subclass == 0xFF || entry->subclass == subclass) &&
            (prog_if == 0xFF || entry->prog_if == prog_if)) {
            
            result.bus = entry->bus;
            result.device = entry->dev;
            result.func = entry->func;
            return result;
        }
    }
    
    return result;
}

int pci_find_all_class(uint8_t class, uint8_t subclass, uint8_t prog_if, 
                       pci_device_t* devices, int max_devices) {
    pci_build_cache();
    
    int found = 0;
    
    for (int i = 0; i < pci_cache_count && found < max_devices; i++) {
        pci_cache_entry_t* entry = &pci_cache[i];
        
        if (entry->class == class && 
            (subclass == 0xFF || entry->subclass == subclass) &&
            (prog_if == 0xFF || entry->prog_if == prog_if)) {
            
            devices[found].bus = entry->bus;
            devices[found].device = entry->dev;
            devices[found].func = entry->func;
            found++;
        }
    }
    
    return found;
}

// ==================== PCI DEBUG ====================

void pci_dump_all(void) {
    pci_build_cache();
    
    serial_puts("\n=== PCI DEVICES ===\n");
    
    for (int i = 0; i < pci_cache_count; i++) {
        pci_cache_entry_t* entry = &pci_cache[i];
        
        serial_puts("[");
        if (entry->bus < 10) serial_puts("0");
        serial_puts_num(entry->bus);
        serial_puts(":");
        if (entry->dev < 10) serial_puts("0");
        serial_puts_num(entry->dev);
        serial_puts(".");
        serial_puts_num(entry->func);
        serial_puts("] ");
        
        // Vendor:Device
        serial_puts_num_hex(entry->vendor);
        serial_puts(":");
        serial_puts_num_hex(entry->device);
        serial_puts(" (Class ");
        serial_puts_num_hex(entry->class);
        serial_puts(".");
        serial_puts_num_hex(entry->subclass);
        serial_puts(".");
        serial_puts_num_hex(entry->prog_if);
        serial_puts(")\n");
    }
    
    serial_puts("Total: ");
    serial_puts_num(pci_cache_count);
    serial_puts(" devices\n");
    serial_puts("===================\n");
}

void pci_dump_class(uint8_t class) {
    pci_build_cache();
    
    serial_puts("\n=== PCI CLASS 0x");
    serial_puts_num_hex(class);
    serial_puts(" DEVICES ===\n");
    
    int count = 0;
    for (int i = 0; i < pci_cache_count; i++) {
        pci_cache_entry_t* entry = &pci_cache[i];
        
        if (entry->class == class) {
            serial_puts("[");
            if (entry->bus < 10) serial_puts("0");
            serial_puts_num(entry->bus);
            serial_puts(":");
            if (entry->dev < 10) serial_puts("0");
            serial_puts_num(entry->dev);
            serial_puts(".");
            serial_puts_num(entry->func);
            serial_puts("] ");
            
            serial_puts_num_hex(entry->vendor);
            serial_puts(":");
            serial_puts_num_hex(entry->device);
            serial_puts(" (");
            serial_puts_num_hex(entry->subclass);
            serial_puts(".");
            serial_puts_num_hex(entry->prog_if);
            serial_puts(")\n");
            count++;
        }
    }
    
    serial_puts("Total: ");
    serial_puts_num(count);
    serial_puts(" devices\n");
    serial_puts("========================\n");
}