#ifndef POZITRON_AHCI_H
#define POZITRON_AHCI_H

#include <stdint.h>
#include "../core/isr.h"

/* Глобальные регистры */
#define AHCI_CAP         0x00
#define AHCI_GHC         0x04
#define AHCI_IS          0x08
#define AHCI_PI          0x0C
#define AHCI_VS          0x10
#define AHCI_CAP2        0x24

/* Регистры порта (смещение = 0x100 + порт*0x80) */
#define AHCI_PX_CLB      0x00
#define AHCI_PX_CLBU     0x04
#define AHCI_PX_FB       0x08
#define AHCI_PX_FBU      0x0C
#define AHCI_PX_IS       0x10
#define AHCI_PX_IE       0x14
#define AHCI_PX_CMD      0x18
#define AHCI_PX_TFD      0x20
#define AHCI_PX_SIG      0x24
#define AHCI_PX_SSTS     0x28
#define AHCI_PX_SCTL     0x2C
#define AHCI_PX_SERR     0x30
#define AHCI_PX_SACT     0x34
#define AHCI_PX_CI       0x38

/* Биты команд */
#define ATA_CMD_READ_DMA          0xC8
#define ATA_CMD_WRITE_DMA         0xCA
#define ATA_CMD_READ_DMA_EXT      0x25
#define ATA_CMD_WRITE_DMA_EXT     0x35
#define ATA_CMD_READ_FPDMA_QUEUED 0x60
#define ATA_CMD_WRITE_FPDMA_QUEUED 0x61
#define ATA_CMD_FLUSH_CACHE       0xE7
#define ATA_CMD_FLUSH_CACHE_EXT   0xEA
#define ATA_CMD_IDENTIFY          0xEC

/* Функции */
int  ahci_init(void);
int  ahci_read_sectors(int port, uint64_t lba, uint32_t count, void *buffer);
int  ahci_write_sectors(int port, uint64_t lba, uint32_t count, void *buffer);
int  ahci_flush_cache(int port);
void ahci_probe_ports(void);
void ahci_irq_handler(registers_t *r);

/* Глобальные переменные */
extern int ahci_port_count;

#endif