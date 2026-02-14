#ifndef PCI_H
#define PCI_H

#include <stdint.h>

typedef struct {
    uint8_t bus;
    uint8_t device;
    uint8_t func;
} pci_device_t;

// Чтение/запись
uint32_t pci_read32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset);
uint16_t pci_read16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset);
uint8_t pci_read8(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset);
void pci_write32(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint32_t value);
void pci_write16(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint16_t value);
void pci_write8(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint8_t value);

// Управление
void pci_enable_bus_master(uint8_t bus, uint8_t dev, uint8_t func);
void pci_enable_memory_space(uint8_t bus, uint8_t dev, uint8_t func);
void pci_enable_io_space(uint8_t bus, uint8_t dev, uint8_t func);

// Поиск
pci_device_t pci_find_class(uint8_t class, uint8_t subclass, uint8_t prog_if);

#endif