#include <stddef.h>
#include <stdint.h>

// ===================== MEMORY FUNCTIONS =====================

void* memset(void* ptr, int value, size_t num) {
    unsigned char* p = (unsigned char*)ptr;
    for (size_t i = 0; i < num; i++) {
        p[i] = (unsigned char)value;
    }
    return ptr;
}

void* memcpy(void* dest, const void* src, size_t num) {
    unsigned char* d = (unsigned char*)dest;
    const unsigned char* s = (const unsigned char*)src;
    for (size_t i = 0; i < num; i++) {
        d[i] = s[i];
    }
    return dest;
}

void* memmove(void* dest, const void* src, size_t num) {
    unsigned char* d = (unsigned char*)dest;
    const unsigned char* s = (const unsigned char*)src;
    
    if (d < s) {
        // Копируем вперед
        for (size_t i = 0; i < num; i++) {
            d[i] = s[i];
        }
    } else if (d > s) {
        // Копируем назад (для перекрывающихся областей)
        for (size_t i = num; i > 0; i--) {
            d[i - 1] = s[i - 1];
        }
    }
    return dest;
}

int memcmp(const void* ptr1, const void* ptr2, size_t num) {
    const unsigned char* p1 = (const unsigned char*)ptr1;
    const unsigned char* p2 = (const unsigned char*)ptr2;
    
    for (size_t i = 0; i < num; i++) {
        if (p1[i] != p2[i]) {
            return (int)p1[i] - (int)p2[i];
        }
    }
    return 0;
}

// ===================== STRING FUNCTIONS =====================

size_t strlen(const char* str) {
    size_t len = 0;
    while (str[len] != '\0') {
        len++;
    }
    return len;
}

char* strcpy(char* dest, const char* src) {
    char* d = dest;
    while ((*d++ = *src++) != '\0');
    return dest;
}

char* strncpy(char* dest, const char* src, size_t num) {
    char* d = dest;
    
    for (size_t i = 0; i < num; i++) {
        if ((*d = *src) != '\0') {
            src++;
        }
        d++;
    }
    return dest;
}

char* strcat(char* dest, const char* src) {
    char* d = dest;
    
    // Находим конец строки dest
    while (*d != '\0') {
        d++;
    }
    
    // Копируем src в конец
    while ((*d++ = *src++) != '\0');
    
    return dest;
}

char* strncat(char* dest, const char* src, size_t num) {
    char* d = dest;
    
    // Находим конец строки dest
    while (*d != '\0') {
        d++;
    }
    
    // Копируем не более num символов
    for (size_t i = 0; i < num; i++) {
        if ((*d = *src) == '\0') {
            break;
        }
        d++;
        src++;
    }
    
    *d = '\0'; // Всегда добавляем нулевой терминатор
    return dest;
}

int strcmp(const char* str1, const char* str2) {
    while (*str1 && (*str1 == *str2)) {
        str1++;
        str2++;
    }
    return *(const unsigned char*)str1 - *(const unsigned char*)str2;
}

int strncmp(const char* str1, const char* str2, size_t num) {
    if (num == 0) return 0;
    
    while (--num && *str1 && (*str1 == *str2)) {
        str1++;
        str2++;
    }
    return *(const unsigned char*)str1 - *(const unsigned char*)str2;
}

char* strchr(const char* str, int ch) {
    while (*str != '\0') {
        if (*str == (char)ch) {
            return (char*)str;
        }
        str++;
    }
    
    if (ch == '\0') {
        return (char*)str;
    }
    
    return NULL;
}

char* strrchr(const char* str, int ch) {
    const char* last = NULL;
    
    while (*str != '\0') {
        if (*str == (char)ch) {
            last = str;
        }
        str++;
    }
    
    if (ch == '\0') {
        return (char*)str;
    }
    
    return (char*)last;
}

size_t strspn(const char* str1, const char* str2) {
    size_t count = 0;
    
    while (*str1 != '\0') {
        const char* c = str2;
        while (*c != '\0') {
            if (*str1 == *c) {
                break;
            }
            c++;
        }
        
        if (*c == '\0') {
            return count;
        }
        
        count++;
        str1++;
    }
    
    return count;
}

size_t strcspn(const char* str1, const char* str2) {
    size_t count = 0;
    
    while (*str1 != '\0') {
        const char* c = str2;
        while (*c != '\0') {
            if (*str1 == *c) {
                return count;
            }
            c++;
        }
        
        count++;
        str1++;
    }
    
    return count;
}

char* strpbrk(const char* str1, const char* str2) {
    while (*str1 != '\0') {
        const char* c = str2;
        while (*c != '\0') {
            if (*str1 == *c) {
                return (char*)str1;
            }
            c++;
        }
        str1++;
    }
    
    return NULL;
}

// ===================== ADDITIONAL FUNCTIONS =====================

// Копирование с обратным порядком (для некоторых алгоритмов)
void* memrev(void* ptr, size_t num) {
    unsigned char* p = (unsigned char*)ptr;
    unsigned char temp;
    
    for (size_t i = 0; i < num / 2; i++) {
        temp = p[i];
        p[i] = p[num - 1 - i];
        p[num - 1 - i] = temp;
    }
    
    return ptr;
}

// Поиск в памяти
void* memchr(const void* ptr, int value, size_t num) {
    const unsigned char* p = (const unsigned char*)ptr;
    
    for (size_t i = 0; i < num; i++) {
        if (p[i] == (unsigned char)value) {
            return (void*)(p + i);
        }
    }
    
    return NULL;
}

// Сравнение без учета регистра (для некоторых драйверов)
int memcasecmp(const void* ptr1, const void* ptr2, size_t num) {
    const unsigned char* p1 = (const unsigned char*)ptr1;
    const unsigned char* p2 = (const unsigned char*)ptr2;
    
    for (size_t i = 0; i < num; i++) {
        unsigned char c1 = p1[i];
        unsigned char c2 = p2[i];
        
        // Приводим к нижнему регистру
        if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
        if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
        
        if (c1 != c2) {
            return (int)c1 - (int)c2;
        }
    }
    
    return 0;
}

// ===================== SIMPLE ITOA (для отладки) =====================

static void reverse_string(char* str, size_t length) {
    size_t start = 0;
    size_t end = length - 1;
    
    while (start < end) {
        char temp = str[start];
        str[start] = str[end];
        str[end] = temp;
        start++;
        end--;
    }
}

char* itoa(int value, char* str, int base) {
    if (base < 2 || base > 36) {
        *str = '\0';
        return str;
    }
    
    int i = 0;
    int is_negative = 0;
    
    // Обрабатываем 0 отдельно
    if (value == 0) {
        str[i++] = '0';
        str[i] = '\0';
        return str;
    }
    
    // Обрабатываем отрицательные числа только для десятичной системы
    if (value < 0 && base == 10) {
        is_negative = 1;
        value = -value;
    }
    
    // Конвертируем число
    while (value != 0) {
        int remainder = value % base;
        if (remainder > 9) {
            str[i++] = (remainder - 10) + 'a';
        } else {
            str[i++] = remainder + '0';
        }
        value = value / base;
    }
    
    // Добавляем знак минуса если нужно
    if (is_negative) {
        str[i++] = '-';
    }
    
    str[i] = '\0';
    
    // Разворачиваем строку
    reverse_string(str, i);
    
    return str;
}

// ===================== SIMPLE ATOI (для отладки) =====================

int atoi(const char* str) {
    int result = 0;
    int sign = 1;
    int i = 0;
    
    // Пропускаем пробелы
    while (str[i] == ' ' || str[i] == '\t' || str[i] == '\n') {
        i++;
    }
    
    // Определяем знак
    if (str[i] == '-') {
        sign = -1;
        i++;
    } else if (str[i] == '+') {
        i++;
    }
    
    // Конвертируем цифры
    while (str[i] >= '0' && str[i] <= '9') {
        result = result * 10 + (str[i] - '0');
        i++;
    }
    
    return result * sign;
}