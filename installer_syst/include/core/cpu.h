#ifndef CPU_H
#define CPU_H

#include <stdint.h>

void cpu_init(void);

void cpu_init_fpu(void);
void cpu_init_sse(void);

#endif