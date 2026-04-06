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

static void disk_parse_mbr(disk_t* disk) {
    uint8_t mbr[512];
    
    if (disk->type == DISK_TYPE_ATAPI) {
        serial_puts("[DISK] Skipping MBR parse for ATAPI device\n");
        disk->partition_count = 0;
        return;
    }
    
    serial_puts("[DISK] Reading MBR from sector 0...\n");
    
    if (disk->read(disk, 0, 1, mbr) != 0) {
        serial_puts("[DISK] Failed to read MBR\n");
        disk->partition_count = 0;
        return;
    }
    
    if (mbr[510] != 0x55 || mbr[511] != 0xAA) {
        serial_puts("[DISK] No valid MBR signature (0x");
        serial_puts_num_hex(mbr[510]);
        serial_puts(" 0x");
        serial_puts_num_hex(mbr[511]);
        serial_puts(")\n");
        disk->partition_count = 0;
        return;
    }
    
    serial_puts("[DISK] Valid MBR found\n");
    
    disk->partition_count = 0;
    
    for (int i = 0; i < 4; i++) {
        uint32_t offset = 446 + i * 16;
        uint8_t bootable = mbr[offset];
        uint8_t type = mbr[offset + 4];
        uint32_t start_lba = *(uint32_t*)(mbr + offset + 8);
        uint32_t sector_count = *(uint32_t*)(mbr + offset + 12);
        
        if (type != 0x00 && sector_count > 0) {
            disk->partitions[disk->partition_count].type = type;
            disk->partitions[disk->partition_count].start_lba = start_lba;
            disk->partitions[disk->partition_count].sector_count = sector_count;
            disk->partitions[disk->partition_count].bootable = bootable == 0x80 ? 1 : 0;
            disk->partition_count++;
            
            serial_puts("[DISK] Partition ");
            serial_puts_num(disk->partition_count - 1);
            serial_puts(": type=0x");
            serial_puts_num_hex(type);
            serial_puts(" start=");
            serial_puts_num(start_lba);
            serial_puts(" size=");
            serial_puts_num(sector_count);
            serial_puts(" sectors");
            if (disk->partitions[disk->partition_count - 1].bootable) serial_puts(" BOOT");
            serial_puts("\n");
            
            if (disk->partition_count >= 16) break;
        }
    }
}

int disk_read_partition(disk_t* disk, int part_index, uint64_t lba, uint32_t count, void* buffer) {
    if (!disk || part_index < 0 || part_index >= disk->partition_count) return -1;
    
    uint64_t real_lba = (uint64_t)disk->partitions[part_index].start_lba + lba;
    
    if (real_lba + count > (uint64_t)disk->partitions[part_index].start_lba + disk->partitions[part_index].sector_count) {
        serial_puts("[DISK] Partition read out of bounds\n");
        return -1;
    }
    
    return disk->read(disk, real_lba, count, buffer);
}

int disk_write_partition(disk_t* disk, int part_index, uint64_t lba, uint32_t count, void* buffer) {
    if (!disk || part_index < 0 || part_index >= disk->partition_count) return -1;
    
    uint64_t real_lba = (uint64_t)disk->partitions[part_index].start_lba + lba;
    
    if (real_lba + count > (uint64_t)disk->partitions[part_index].start_lba + disk->partitions[part_index].sector_count) {
        serial_puts("[DISK] Partition write out of bounds\n");
        return -1;
    }
    
    return disk->write(disk, real_lba, count, buffer);
}

int disk_get_partition_offset(disk_t* disk, int part_index) {
    if (!disk || part_index < 0 || part_index >= disk->partition_count) return -1;
    return disk->partitions[part_index].start_lba;
}

int disk_find_partition_by_type(disk_t* disk, uint8_t type) {
    if (!disk) return -1;
    
    for (int i = 0; i < disk->partition_count; i++) {
        if (disk->partitions[i].type == type) {
            return i;
        }
    }
    
    return -1;
}

void disk_init(void) {
    if (disk_initialized) return;
    
    serial_puts("[DISK] Initializing disk subsystem...\n");
    
    disk_count = 0;
    memset(disks, 0, sizeof(disks));
    
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
        disk->partition_offset = 0;
        disk->partition_count = 0;
        
        memcpy(disk->model, dev->model, 40);
        memcpy(disk->serial, dev->serial, 20);
        
        disk_parse_mbr(disk);
        
        disk_count++;
        
        serial_puts("[DISK] Registered ATA disk: ");
        serial_puts(disk->model);
        serial_puts("\n");
    }
    
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
        disk->partition_offset = 0;
        disk->partition_count = 0;
        
        memcpy(disk->model, port->model, 40);
        memcpy(disk->serial, port->serial, 20);
        
        disk_parse_mbr(disk);
        
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
        
        if (d->partition_count > 0) {
            serial_puts("  Partitions:\n");
            for (int p = 0; p < d->partition_count; p++) {
                serial_puts("    ");
                serial_puts_num(p);
                serial_puts(": type=0x");
                serial_puts_num_hex(d->partitions[p].type);
                serial_puts(" start=");
                serial_puts_num(d->partitions[p].start_lba);
                serial_puts(" size=");
                serial_puts_num(d->partitions[p].sector_count);
                if (d->partitions[p].bootable) serial_puts(" BOOT");
                serial_puts("\n");
            }
        }
    }
    serial_puts("=============\n");
}