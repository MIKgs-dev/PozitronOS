#pragma once
#include <stdint.h>

typedef enum {
    DISK_TYPE_NONE = 0,
    DISK_TYPE_ATA,
    DISK_TYPE_ATAPI,
    DISK_TYPE_SATA,
    DISK_TYPE_AHCI
} disk_type_t;

typedef struct disk_partition {
    uint8_t  type;
    uint32_t start_lba;
    uint32_t sector_count;
    uint8_t  bootable;
} disk_partition_t;

typedef struct disk {
    uint32_t id;
    disk_type_t type;
    uint64_t sectors;
    uint32_t sector_size;
    char model[41];
    char serial[21];
    
    int (*read)(struct disk* disk, uint64_t lba, uint32_t count, void* buffer);
    int (*write)(struct disk* disk, uint64_t lba, uint32_t count, void* buffer);
    int (*flush)(struct disk* disk);
    
    void* private_data;
    int private_id;
    
    uint64_t partition_offset;
    disk_partition_t partitions[16];
    int partition_count;
} disk_t;

void disk_init(void);
disk_t* disk_get(int index);
int disk_get_count(void);
void disk_dump_all(void);

int disk_read_partition(disk_t* disk, int part_index, uint64_t lba, uint32_t count, void* buffer);
int disk_write_partition(disk_t* disk, int part_index, uint64_t lba, uint32_t count, void* buffer);
int disk_get_partition_offset(disk_t* disk, int part_index);
int disk_find_partition_by_type(disk_t* disk, uint8_t type);

static inline int disk_read(disk_t* disk, uint64_t lba, uint32_t count, void* buffer) {
    if (!disk || !disk->read) return -1;
    return disk->read(disk, lba, count, buffer);
}

static inline int disk_write(disk_t* disk, uint64_t lba, uint32_t count, void* buffer) {
    if (!disk || !disk->write) return -1;
    return disk->write(disk, lba, count, buffer);
}

static inline int disk_flush(disk_t* disk) {
    if (!disk || !disk->flush) return -1;
    return disk->flush(disk);
}