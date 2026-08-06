#ifndef LOADER_ELF_DRV_LOADER_H
#define LOADER_ELF_DRV_LOADER_H

#include <stdint.h>

// Результат загрузки
typedef struct {
    void* base_addr;
    uint32_t size;
    void* entry_point;
} elf_drv_load_result_t;

int elf_drv_load(const char* path, elf_drv_load_result_t* result);
int elf_drv_unload(elf_drv_load_result_t* result);

#endif