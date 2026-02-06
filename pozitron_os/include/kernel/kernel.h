#ifndef KERNEL_H
#define KERNEL_H

#include "types.h"
#include "ports.h"
#include "kernel/multiboot.h"

void kernel_main(uint32_t magic, multiboot_info_t* mb_info);
void panic(const char* message);

#endif