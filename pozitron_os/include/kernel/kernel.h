#ifndef KERNEL_H
#define KERNEL_H

#include "types.h"
#include "ports.h"

void kernel_main(uint32_t magic, uint32_t* mb_info);
void panic(const char* message);

#endif