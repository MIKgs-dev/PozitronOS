#ifndef SERIAL_H
#define SERIAL_H

#include "kernel/types.h"

void serial_init(void);
char serial_read(void);
void serial_write(char c);
void serial_write_char(char c);
void serial_puts(const char* str);
void serial_puts_num(uint32_t num);
void serial_puts_num_hex(uint32_t num);

#endif