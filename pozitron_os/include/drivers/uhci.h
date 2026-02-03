#ifndef UHCI_H
#define UHCI_H

#include <stdint.h>

// UHCI регистры
#define UHCI_CMD         0x00    // Command register
#define UHCI_STS         0x02    // Status register
#define UHCI_INTR        0x04    // Interrupt enable
#define UHCI_FRNUM       0x06    // Frame number
#define UHCI_FLBASEADD   0x08    // Frame list base address
#define UHCI_SOFMOD      0x0C    // Start of frame modify
#define UHCI_PORTSC1     0x10    // Port 1 status/control
#define UHCI_PORTSC2     0x12    // Port 2 status/control

// Команды
#define UHCI_CMD_RUN     0x0001
#define UHCI_CMD_HCRESET 0x0002
#define UHCI_CMD_GRESET  0x0004

// Статус
#define UHCI_STS_USBINT  0x0001
#define UHCI_STS_ERROR   0x0002
#define UHCI_STS_HCHALTED 0x0020

// Порты
#define UHCI_PORT_CONNECT   0x0001
#define UHCI_PORT_ENABLE    0x0002
#define UHCI_PORT_SUSPEND   0x0004
#define UHCI_PORT_RESET     0x0008
#define UHCI_PORT_LSDA      0x0020

// Функции UHCI (уникальные имена!)
void uhci_init(uint32_t base);
void uhci_poll(void);
uint8_t uhci_detect_devices(void);
uint8_t uhci_is_present(void);

#endif // UHCI_H