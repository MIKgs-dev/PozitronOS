#ifndef EHCI_H
#define EHCI_H

#include <stdint.h>

// EHCI регистры
#define EHCI_CAPLENGTH      0x00
#define EHCI_HCIVERSION     0x02
#define EHCI_HCSPARAMS      0x04
#define EHCI_HCCPARAMS      0x08
#define EHCI_USBCMD         0x00
#define EHCI_USBSTS         0x04
#define EHCI_USBINTR        0x08
#define EHCI_FRINDEX        0x0C
#define EHCI_PORTSC         0x44

// Команды
#define EHCI_CMD_RUN        0x00000001
#define EHCI_CMD_RESET      0x00000002
#define EHCI_CMD_ASYNC_EN   0x00000020
#define EHCI_CMD_PERIODIC_EN 0x00000040

// Статус
#define EHCI_STS_HALTED     0x00001000
#define EHCI_STS_HOST_ERROR 0x00000010

// Порты
#define EHCI_PORT_CONNECT   0x00000001
#define EHCI_PORT_ENABLE    0x00000002
#define EHCI_PORT_RESET     0x00000100
#define EHCI_PORT_POWER     0x00001000

// Функции EHCI
void ehci_init(uint32_t cap_base, uint32_t op_base);
void ehci_poll(void);
uint8_t ehci_detect_devices(void);
uint8_t ehci_is_present(void);

#endif // EHCI_H