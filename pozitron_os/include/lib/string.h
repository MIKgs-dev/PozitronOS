#ifndef _STRING_H
#define _STRING_H

#include <stddef.h>

// Memory functions
void* memset(void* ptr, int value, size_t num);
void* memcpy(void* dest, const void* src, size_t num);
void* memmove(void* dest, const void* src, size_t num);
int memcmp(const void* ptr1, const void* ptr2, size_t num);
void* memchr(const void* ptr, int value, size_t num);

// String functions
size_t strlen(const char* str);
char* strcpy(char* dest, const char* src);
char* strncpy(char* dest, const char* src, size_t num);
char* strcat(char* dest, const char* src);
char* strncat(char* dest, const char* src, size_t num);
int strcmp(const char* str1, const char* str2);
int strncmp(const char* str1, const char* str2, size_t num);
char* strchr(const char* str, int ch);
char* strrchr(const char* str, int ch);
size_t strspn(const char* str1, const char* str2);
size_t strcspn(const char* str1, const char* str2);
char* strpbrk(const char* str1, const char* str2);

// Additional useful functions
void* memrev(void* ptr, size_t num);
int memcasecmp(const void* ptr1, const void* ptr2, size_t num);
char* itoa(int value, char* str, int base);
int atoi(const char* str);

#endif // _STRING_H