#include <stdarg.h>
#include <stdint.h>

static void print_char(char** out, char c) {
    if (out) {
        *(*out)++ = c;
    }
}

static void print_string(char** out, const char* str) {
    while (*str) {
        print_char(out, *str++);
    }
}

static void print_int(char** out, int value) {
    char buffer[16];
    int i = 0;
    int negative = 0;
    
    if (value < 0) {
        negative = 1;
        value = -value;
    }
    
    if (value == 0) {
        buffer[i++] = '0';
    } else {
        while (value > 0) {
            buffer[i++] = '0' + (value % 10);
            value /= 10;
        }
    }
    
    if (negative) {
        buffer[i++] = '-';
    }
    
    while (i > 0) {
        print_char(out, buffer[--i]);
    }
}

static void print_hex(char** out, unsigned int value) {
    char buffer[16];
    int i = 0;
    int started = 0;
    
    for (int j = 28; j >= 0; j -= 4) {
        int digit = (value >> j) & 0xF;
        if (digit != 0 || started || j == 0) {
            started = 1;
            if (digit < 10) {
                buffer[i++] = '0' + digit;
            } else {
                buffer[i++] = 'A' + (digit - 10);
            }
        }
    }
    
    for (int j = 0; j < i; j++) {
        print_char(out, buffer[j]);
    }
}

int sprintf(char* str, const char* format, ...) {
    char* out = str;
    va_list args;
    va_start(args, format);
    
    for (const char* p = format; *p; p++) {
        if (*p != '%') {
            *out++ = *p;
            continue;
        }
        
        p++; // переходим после %
        
        switch (*p) {
            case 'd': {
                int value = va_arg(args, int);
                print_int(&out, value);
                break;
            }
            case 'u': {
                unsigned int value = va_arg(args, unsigned int);
                print_int(&out, (int)value);
                break;
            }
            case 'x': {
                unsigned int value = va_arg(args, unsigned int);
                print_hex(&out, value);
                break;
            }
            case 's': {
                const char* s = va_arg(args, const char*);
                print_string(&out, s);
                break;
            }
            case 'c': {
                char c = (char)va_arg(args, int);
                *out++ = c;
                break;
            }
            case '%': {
                *out++ = '%';
                break;
            }
            default:
                *out++ = '%';
                *out++ = *p;
                break;
        }
    }
    
    *out = '\0';
    va_end(args);
    return out - str;
}

int vsprintf(char* buf, const char* fmt, va_list args) {
    char* out = buf;
    
    for (const char* p = fmt; *p; p++) {
        if (*p != '%') {
            *out++ = *p;
            continue;
        }
        
        p++; // переходим после %
        
        switch (*p) {
            case 'd': {
                int value = va_arg(args, int);
                print_int(&out, value);
                break;
            }
            case 'u': {
                unsigned int value = va_arg(args, unsigned int);
                print_int(&out, (int)value);
                break;
            }
            case 'x': {
                unsigned int value = va_arg(args, unsigned int);
                print_hex(&out, value);
                break;
            }
            case 's': {
                const char* s = va_arg(args, const char*);
                print_string(&out, s);
                break;
            }
            case 'c': {
                char c = (char)va_arg(args, int);
                *out++ = c;
                break;
            }
            case '%': {
                *out++ = '%';
                break;
            }
            default:
                *out++ = '%';
                *out++ = *p;
                break;
        }
    }
    
    *out = '\0';
    return out - buf;
}