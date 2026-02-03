#include "drivers/serial.h"
#include "kernel/ports.h"

#define PORT 0x3F8

void serial_init(void) {
    outb(PORT + 1, 0x00);    // Отключаем прерывания
    outb(PORT + 3, 0x80);    // Включаем DLAB
    outb(PORT + 0, 0x03);    // Устанавливаем скорость 38400 baud
    outb(PORT + 1, 0x00);
    outb(PORT + 3, 0x03);    // 8 бит, нет паритета, один стоп бит
    outb(PORT + 2, 0xC7);    // Включаем FIFO
    outb(PORT + 4, 0x0B);    // Включаем IRQ
}

int serial_received(void) {
    return inb(PORT + 5) & 1;
}

char serial_read(void) {
    while (serial_received() == 0);
    return inb(PORT);
}

int serial_is_transmit_empty(void) {
    return inb(PORT + 5) & 0x20;
}

void serial_write(char c) {
    while (serial_is_transmit_empty() == 0);
    outb(PORT, c);
}

void serial_write_char(char c) {
    while (!serial_is_transmit_empty());
    outb(PORT, c);
}

void serial_puts(const char* str) {
    while (*str) {
        serial_write(*str++);
    }
}

void serial_puts_num(uint32_t num) {
    char buf[12];
    int i = 0;
    
    if (num == 0) {
        serial_write('0');
        return;
    }
    
    while (num > 0 && i < 11) {
        buf[i++] = '0' + (num % 10);
        num /= 10;
    }
    
    while (i > 0) {
        serial_write(buf[--i]);
    }
}

void serial_puts_num_hex(uint32_t num) {
    char hex[] = "0123456789ABCDEF";
    
    // Если число 0
    if (num == 0) {
        serial_write('0');
        return;
    }
    
    // Ищем первую ненулевую цифру
    char buffer[9];
    int pos = 0;
    
    for (int i = 28; i >= 0; i -= 4) {
        uint8_t nibble = (num >> i) & 0xF;
        if (nibble != 0 || pos > 0) {
            buffer[pos++] = hex[nibble];
        }
    }
    
    // Выводим
    for (int i = 0; i < pos; i++) {
        serial_write(buffer[i]);
    }
}