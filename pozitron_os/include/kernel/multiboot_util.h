#ifndef MULTIBOOT_UTIL_H
#define MULTIBOOT_UTIL_H

#include "kernel/multiboot.h"

int multiboot_check(uint32_t magic);
void* multiboot_get_framebuffer(multiboot_info_t* mb_info);
void multiboot_get_resolution(multiboot_info_t* mb_info, 
                             uint32_t* width, uint32_t* height, uint32_t* bpp);
void multiboot_dump_info(multiboot_info_t* mb_info);

#endif