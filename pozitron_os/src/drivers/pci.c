#include "drivers/pci.h"
#include "drivers/serial.h"
#include "kernel/ports.h"

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

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

pci_device_t pci_find_class(uint8_t class, uint8_t subclass, uint8_t prog_if) {
    pci_device_t result = {0xFF, 0xFF, 0xFF};
    
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t dev = 0; dev < 32; dev++) {
            uint16_t vendor = pci_read16(bus, dev, 0, 0);
            if (vendor == 0xFFFF) continue;
            
            for (uint8_t func = 0; func < 8; func++) {
                vendor = pci_read16(bus, dev, func, 0);
                if (vendor == 0xFFFF) continue;
                
                uint8_t c = pci_read8(bus, dev, func, 0x0B);
                uint8_t s = pci_read8(bus, dev, func, 0x0A);
                uint8_t p = pci_read8(bus, dev, func, 0x09);
                
                if (c == class && (subclass == 0xFF || s == subclass) && 
                    (prog_if == 0xFF || p == prog_if)) {
                    result.bus = bus;
                    result.device = dev;
                    result.func = func;
                    return result;
                }
            }
        }
    }
    
    return result;
}