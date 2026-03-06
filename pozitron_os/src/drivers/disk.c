#include "drivers/disk.h"
#include "drivers/ahci.h"
#include "drivers/serial.h"
#include "lib/string.h"

static disk_info_t disks[MAX_DISKS];
static int disk_count = 0;

void disk_init(void) {
    for (int i = 0; i < MAX_DISKS; i++) {
        disks[i].present = 0;
    }
    disk_count = 0;
    serial_puts("[DISK] Manager initialized\n");
}

int disk_register(disk_type_t type, uint8_t bus, uint8_t device,
                  uint64_t total_sectors, const char* model, uint8_t removable) {
    if (disk_count >= MAX_DISKS) {
        serial_puts("[DISK] Too many disks, registration failed\n");
        return -1;
    }

    disk_info_t* d = &disks[disk_count];
    d->present = 1;
    d->type = type;
    d->bus = bus;
    d->device = device;
    d->total_sectors = total_sectors;
    d->sector_size = 512;
    d->size_mb = (uint32_t)((total_sectors * 512) / (1024 * 1024));
    d->removable = removable;

    // Копируем модель (обрезаем до 40 символов)
    const char* src = model;
    char* dst = d->model;
    int i = 0;
    while (*src && i < 40) {
        *dst++ = *src++;
        i++;
    }
    *dst = '\0';

    serial_puts("[DISK] Registered: ");
    serial_puts(d->model);
    serial_puts(" (");
    serial_puts_num(d->size_mb);
    serial_puts(" MB) as disk ");
    serial_puts_num(disk_count);
    serial_puts("\n");

    disk_count++;
    return disk_count - 1;
}

int disk_get_count(void) {
    return disk_count;
}

const disk_info_t* disk_get_info(int index) {
    if (index < 0 || index >= disk_count) return NULL;
    return &disks[index];
}

int disk_read(int disk_num, uint64_t lba, uint32_t count, void* buffer) {
    if (disk_num < 0 || disk_num >= disk_count) return -1;
    disk_info_t* d = &disks[disk_num];
    if (!d->present) return -1;

    switch (d->type) {
        case DISK_TYPE_AHCI:
            return ahci_read_sectors(d->bus, (uint32_t)lba, count, buffer);
        case DISK_TYPE_ATA_PIO:
            // TODO: добавить позже
            serial_puts("[DISK] ATA PIO not yet implemented\n");
            return -1;
        default:
            return -1;
    }
}

int disk_write(int disk_num, uint64_t lba, uint32_t count, const void* buffer) {
    if (disk_num < 0 || disk_num >= disk_count) return -1;
    disk_info_t* d = &disks[disk_num];
    if (!d->present) return -1;

    switch (d->type) {
        case DISK_TYPE_AHCI:
            return ahci_write_sectors(d->bus, (uint32_t)lba, count, (void*)buffer);
        case DISK_TYPE_ATA_PIO:
            serial_puts("[DISK] ATA PIO not yet implemented\n");
            return -1;
        default:
            return -1;
    }
}