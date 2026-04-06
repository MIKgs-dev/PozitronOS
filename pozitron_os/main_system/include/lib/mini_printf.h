#ifndef MINI_PRINTF_H
#define MINI_PRINTF_H

#include <stdarg.h>

int sprintf(char* str, const char* format, ...);
int vsprintf(char* str, const char* format, va_list args);

#endif