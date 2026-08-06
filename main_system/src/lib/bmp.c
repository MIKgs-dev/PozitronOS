#include "lib/bmp.h"
#include "fs/vfs.h"
#include "kernel/memory.h"
#include "drivers/serial.h"

uint32_t* bmp_load(const char* path, int* width, int* height) {
    struct vfs_file* file = NULL;
    uint32_t bytes_read = 0;
    uint8_t header[138];

    if (vfs_open(path, FS_O_RDONLY, &file) != 0) {
        serial_puts("[BMP] Error: Cannot open file\n");
        return NULL;
    }

    vfs_lseek(file, 0, 2);
    uint32_t file_size = file->f_pos;
    vfs_lseek(file, 0, 0);

    if (file_size < 54) {
        vfs_close(file);
        return NULL;
    }

    uint32_t header_size = 138;
    if (file_size < header_size) header_size = file_size;

    if (vfs_read(file, header, header_size, &bytes_read) != 0 || bytes_read < 54) {
        serial_puts("[BMP] Error: Failed to read BMP header\n");
        vfs_close(file);
        return NULL;
    }

    if (header[0] != 0x42 || header[1] != 0x4D) {
        serial_puts("[BMP] Error: Not a valid BMP file\n");
        vfs_close(file);
        return NULL;
    }

    uint32_t bfOffBits  = header[0x0A] | (header[0x0B] << 8) | (header[0x0C] << 16) | (header[0x0D] << 24);
    int32_t biWidth     = header[0x12] | (header[0x13] << 8) | (header[0x14] << 16) | (header[0x15] << 24);
    int32_t biHeight    = header[0x16] | (header[0x17] << 8) | (header[0x18] << 16) | (header[0x19] << 24);
    uint16_t biBitCount = header[0x1C] | (header[0x1D] << 8);

    if (biBitCount != 24 && biBitCount != 32) {
        serial_puts("[BMP] Error: Only 24 or 32-bit supported\n");
        vfs_close(file);
        return NULL;
    }

    int flip = 1;
    if (biHeight < 0) {
        biHeight = -biHeight;
        flip = 0;
    }

    *width = biWidth;
    *height = biHeight;

    uint32_t pixel_count = biWidth * biHeight;
    uint32_t* buffer = (uint32_t*)kmalloc(pixel_count * sizeof(uint32_t));
    if (!buffer) {
        vfs_close(file);
        return NULL;
    }

    int bytes_per_pixel = biBitCount / 8;
    int row_stride = (biWidth * bytes_per_pixel + 3) & ~3;
    uint32_t pixel_data_size = row_stride * biHeight;

    uint8_t* row_buffer = (uint8_t*)kmalloc(row_stride);
    if (!row_buffer) {
        kfree(buffer);
        vfs_close(file);
        return NULL;
    }

    vfs_lseek(file, bfOffBits, 0);

    for (int y = 0; y < biHeight; y++) {
        if (vfs_read(file, row_buffer, row_stride, &bytes_read) != 0 || bytes_read < (uint32_t)row_stride) {
            break;
        }

        int target_y = flip ? (biHeight - 1 - y) : y;
        uint32_t* target_row = buffer + (target_y * biWidth);

        if (biBitCount == 24) {
            for (int x = 0; x < biWidth; x++) {
                uint8_t b = row_buffer[x * 3 + 0];
                uint8_t g = row_buffer[x * 3 + 1];
                uint8_t r = row_buffer[x * 3 + 2];
                target_row[x] = (r << 16) | (g << 8) | b | (0xFF << 24);
            }
        } else if (biBitCount == 32) {
            for (int x = 0; x < biWidth; x++) {
                uint32_t pixel = ((uint32_t*)row_buffer)[x];
                if ((pixel >> 24) == 0) {
                    pixel |= (0xFF << 24);
                }
                target_row[x] = pixel;
            }
        }
    }

    kfree(row_buffer);
    vfs_close(file);
    return buffer;
}