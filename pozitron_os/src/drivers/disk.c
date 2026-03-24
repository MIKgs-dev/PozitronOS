#include "drivers/disk.h"
#include "drivers/ata.h"
#include "drivers/ahci.h"
#include "drivers/serial.h"
#include "lib/string.h"
#include <stdio.h>

static disk_t disks[16];
static int disk_count = 0;
static uint8_t disk_initialized = 0;

static int disk_ahci_read(disk_t* disk, uint64_t lba, uint32_t count, void* buffer) {
    return ahci_read_sectors(disk->private_id, lba, count, buffer);
}

static int disk_ahci_write(disk_t* disk, uint64_t lba, uint32_t count, void* buffer) {
    return ahci_write_sectors(disk->private_id, lba, count, buffer);
}

static int disk_ahci_flush(disk_t* disk) {
    return ahci_flush_cache(disk->private_id);
}

static int disk_ata_read(disk_t* disk, uint64_t lba, uint32_t count, void* buffer) {
    ata_device_t* dev = (ata_device_t*)disk->private_data;
    return ata_read_sectors(dev, lba, count, buffer);
}

static int disk_ata_write(disk_t* disk, uint64_t lba, uint32_t count, void* buffer) {
    ata_device_t* dev = (ata_device_t*)disk->private_data;
    return ata_write_sectors(dev, lba, count, buffer);
}

static int disk_ata_flush(disk_t* disk) {
    ata_device_t* dev = (ata_device_t*)disk->private_data;
    return ata_flush_cache(dev);
}

void disk_init(void) {
    if (disk_initialized) return;
    
    serial_puts("[DISK] Initializing disk subsystem...\n");
    
    disk_count = 0;
    memset(disks, 0, sizeof(disks));
    
    // ATA
    ata_init();
    
    int ata_devices = ata_get_device_count();
    for (int i = 0; i < ata_devices && disk_count < 16; i++) {
        ata_device_t* dev = ata_get_device(i);
        if (!dev || !dev->present) continue;
        
        disk_t* disk = &disks[disk_count];
        disk->id = disk_count;
        disk->type = dev->type;
        disk->sectors = dev->sectors;
        disk->sector_size = dev->sector_size;
        disk->read = disk_ata_read;
        disk->write = disk_ata_write;
        disk->flush = disk_ata_flush;
        disk->private_data = dev;
        disk->private_id = i;
        
        memcpy(disk->model, dev->model, 40);
        memcpy(disk->serial, dev->serial, 20);
        
        disk_count++;
        
        serial_puts("[DISK] Registered ATA disk: ");
        serial_puts(disk->model);
        serial_puts("\n");
    }
    
    // AHCI
    ahci_init();
    
    int ahci_ports = ahci_get_port_count();
    for (int i = 0; i < ahci_ports && disk_count < 16; i++) {
        struct ahci_port* port = ahci_get_port(i);
        if (!port || !port->present) continue;
        
        disk_t* disk = &disks[disk_count];
        disk->id = disk_count;
        disk->type = port->atapi ? DISK_TYPE_ATAPI : DISK_TYPE_AHCI;
        disk->sectors = port->sectors;
        disk->sector_size = port->sector_size;
        disk->read = disk_ahci_read;
        disk->write = disk_ahci_write;
        disk->flush = disk_ahci_flush;
        disk->private_data = port;
        disk->private_id = i;
        
        memcpy(disk->model, port->model, 40);
        memcpy(disk->serial, port->serial, 20);
        
        disk_count++;
        
        serial_puts("[DISK] Registered AHCI disk: ");
        serial_puts(disk->model);
        if (!port->atapi) {
            serial_puts(" (");
            serial_puts_num_ulong(disk->sectors / 2 / 1024);
            serial_puts(" MB)");
        }
        serial_puts("\n");
    }
    
    disk_initialized = 1;
    
    serial_puts("[DISK] Subsystem initialized, found ");
    serial_puts_num(disk_count);
    serial_puts(" disks\n");
}

disk_t* disk_get(int index) {
    if (index >= disk_count) return NULL;
    return &disks[index];
}

int disk_get_count(void) {
    return disk_count;
}

void disk_dump_all(void) {
    serial_puts("\n=== DISKS ===\n");
    for (int i = 0; i < disk_count; i++) {
        disk_t* d = &disks[i];
        serial_puts("Disk ");
        serial_puts_num(i);
        serial_puts(": ");
        serial_puts(d->model);
        serial_puts("\n  Type: ");
        switch (d->type) {
            case DISK_TYPE_ATA: serial_puts("ATA"); break;
            case DISK_TYPE_ATAPI: serial_puts("ATAPI (CD/DVD)"); break;
            case DISK_TYPE_AHCI: serial_puts("AHCI (SATA)"); break;
            case DISK_TYPE_SATA: serial_puts("SATA"); break;
            default: serial_puts("Unknown");
        }
        serial_puts("\n  Size: ");
        if (d->sectors > 0) {
            serial_puts_num_ulong(d->sectors);
            serial_puts(" sectors (");
            serial_puts_num_ulong(d->sectors / 2 / 1024);
            serial_puts(" MB)");
        } else {
            serial_puts("N/A (removable media)");
        }
        serial_puts("\n");
        if (d->serial[0]) {
            serial_puts("  Serial: ");
            serial_puts(d->serial);
            serial_puts("\n");
        }
    }
    serial_puts("=============\n");
}