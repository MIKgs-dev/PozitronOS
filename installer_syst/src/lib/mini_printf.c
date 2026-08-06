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

/* Исправление: отдельная функция для беззнаковых чисел, чтобы избежать ухода в минус */
static void print_unsigned(char** out, unsigned int value) {
    char buffer[16];
    int i = 0;
    
    if (value == 0) {
        buffer[i++] = '0';
    } else {
        while (value > 0) {
            buffer[i++] = '0' + (value % 10);
            value /= 10;
        }
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
                print_unsigned(&out, value); /* Исправлено */
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
                print_unsigned(&out, value); /* Исправлено */
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

static const unsigned short ctype_b_data[256] = {
    [' '] = 0x20, ['0'] = 0x08, ['1'] = 0x08, ['2'] = 0x08, ['3'] = 0x08,
    ['4'] = 0x08, ['5'] = 0x08, ['6'] = 0x08, ['7'] = 0x08, ['8'] = 0x08, ['9'] = 0x08,
    ['A'] = 0x01, ['B'] = 0x01, ['C'] = 0x01, ['D'] = 0x01, ['E'] = 0x01, ['F'] = 0x01,
    ['a'] = 0x02, ['b'] = 0x02, ['c'] = 0x02, ['d'] = 0x02, ['e'] = 0x02, ['f'] = 0x02
};

const unsigned short **__ctype_b_loc(void) {
    static const unsigned short *p = ctype_b_data;
    return &p;
}

static const int ctype_tolower_data[256] = { [0 ... 255] = 0 }; 
const int **__ctype_tolower_loc(void) {
    static const int *p = ctype_tolower_data;
    return &p;
}

const int **__ctype_toupper_loc(void) {
    static const int *p = ctype_tolower_data;
    return &p;
}

unsigned long long __udivdi3(unsigned long long num, unsigned long long den) {
    unsigned long long res = 0, rem = 0;
    for (int i = 63; i >= 0; i--) {
        rem = (rem << 1) | ((num >> i) & 1);
        if (rem >= den) { rem -= den; res |= (1ULL << i); }
    }
    return res;
}

unsigned long long __umoddi3(unsigned long long num, unsigned long long den) {
    unsigned long long rem = 0;
    for (int i = 63; i >= 0; i--) {
        rem = (rem << 1) | ((num >> i) & 1);
        if (rem >= den) rem -= den;
    }
    return rem;
}