#ifndef PORT_SPI_H
#define PORT_SPI_H

#include <stdint.h>

// Эмуляция SPI через параллельный порт LPT1
#define LPT1_DATA   0x378
#define LPT1_STATUS 0x379
#define LPT1_CONTROL 0x37A

// Биты для управления
#define SPI_SCK_BIT  0x01  // D0
#define SPI_MOSI_BIT 0x02  // D1
#define SPI_MISO_BIT 0x40  // Status bit 6
#define SPI_CS_BIT   0x04  // D2

static inline void spi_pins_init(void) {
    // Устанавливаем направление: Data порт на вывод
    uint8_t ctrl = inb(LPT1_CONTROL);
    ctrl &= ~0x20;  // Биты 5:0 = вывод
    outb(LPT1_CONTROL, ctrl);
    
    // CS неактивен
    uint8_t data = inb(LPT1_DATA);
    data |= SPI_CS_BIT;
    outb(LPT1_DATA, data);
}

static inline void sd_select(void) {
    uint8_t data = inb(LPT1_DATA);
    data &= ~SPI_CS_BIT;
    outb(LPT1_DATA, data);
    io_wait();
}

static inline void sd_deselect(void) {
    uint8_t data = inb(LPT1_DATA);
    data |= SPI_CS_BIT;
    outb(LPT1_DATA, data);
    io_wait();
    
    // Такты для завершения
    for (int i = 0; i < 10; i++) {
        data &= ~SPI_SCK_BIT;
        outb(LPT1_DATA, data);
        io_wait();
        data |= SPI_SCK_BIT;
        outb(LPT1_DATA, data);
        io_wait();
    }
}

static uint8_t spi_transfer_byte(uint8_t out) {
    uint8_t in = 0;
    uint8_t data = inb(LPT1_DATA);
    
    for (int i = 0; i < 8; i++) {
        // Clock low
        data &= ~SPI_SCK_BIT;
        
        // MOSI
        if (out & 0x80) {
            data |= SPI_MOSI_BIT;
        } else {
            data &= ~SPI_MOSI_BIT;
        }
        outb(LPT1_DATA, data);
        io_wait();
        
        // Clock high
        data |= SPI_SCK_BIT;
        outb(LPT1_DATA, data);
        io_wait();
        
        // Read MISO
        uint8_t status = inb(LPT1_STATUS);
        in <<= 1;
        if (status & SPI_MISO_BIT) {
            in |= 1;
        }
        
        out <<= 1;
    }
    
    // Clock low
    data &= ~SPI_SCK_BIT;
    outb(LPT1_DATA, data);
    
    return in;
}