#ifndef BLOCK_H
#define BLOCK_H

#include <stdint.h>

struct drive_s {
    uint8_t type;
    uint8_t removable;
    uint32_t sectors;
    uint32_t sector_size;
    void *private;
};

struct disk_op_s {
    struct drive_s *drive_gf;
    void *buf_fl;
    uint32_t count;
};

#define DISK_RET_SUCCESS 0
#define DISK_RET_EBADTRACK -1

#endif