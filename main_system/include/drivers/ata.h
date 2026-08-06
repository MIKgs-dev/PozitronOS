#ifndef __ATA_H
#define __ATA_H

#include <stdint.h>

// ATA IO ports
#define PORT_ATA1_CMD_BASE     0x01F0
#define PORT_ATA1_CTRL_BASE    0x03F4
#define PORT_ATA2_CMD_BASE     0x0170
#define PORT_ATA2_CTRL_BASE    0x0374

// ATA регистры
#define ATA_CB_DATA  0
#define ATA_CB_ERR   1
#define ATA_CB_FR    1
#define ATA_CB_SC    2
#define ATA_CB_SN    3
#define ATA_CB_CL    4
#define ATA_CB_CH    5
#define ATA_CB_DH    6
#define ATA_CB_STAT  7
#define ATA_CB_CMD   7

#define ATA_CB_ASTAT 2
#define ATA_CB_DC    2
#define ATA_CB_DA    3

// Device bits
#define ATA_CB_DH_DEV0 0xA0
#define ATA_CB_DH_DEV1 0xB0
#define ATA_CB_DH_LBA  0x40

// Status bits
#define ATA_CB_STAT_BSY  0x80
#define ATA_CB_STAT_RDY  0x40
#define ATA_CB_STAT_DF   0x20
#define ATA_CB_STAT_DRQ  0x08
#define ATA_CB_STAT_ERR  0x01

// Control bits
#define ATA_CB_DC_HD15   0x08
#define ATA_CB_DC_SRST   0x04
#define ATA_CB_DC_NIEN   0x02

// ATA commands
#define ATA_CMD_READ_SECTORS             0x20
#define ATA_CMD_READ_SECTORS_EXT         0x24
#define ATA_CMD_READ_DMA_EXT             0x25
#define ATA_CMD_WRITE_SECTORS            0x30
#define ATA_CMD_WRITE_SECTORS_EXT        0x34
#define ATA_CMD_WRITE_DMA_EXT            0x35
#define ATA_CMD_PACKET                   0xA0
#define ATA_CMD_IDENTIFY_PACKET_DEVICE   0xA1
#define ATA_CMD_READ_DMA                 0xC8
#define ATA_CMD_WRITE_DMA                0xCA
#define ATA_CMD_FLUSH_CACHE              0xE7
#define ATA_CMD_FLUSH_CACHE_EXT          0xEA
#define ATA_CMD_IDENTIFY_DEVICE          0xEC

// Типы устройств
#define ATA_DEV_NONE    0
#define ATA_DEV_ATA     1
#define ATA_DEV_ATAPI   2
#define ATA_DEV_SATA    3

// Структура PCI устройства (минимальная для ATA)
struct ata_pci_device {
    uint8_t bus;
    uint8_t device;
    uint8_t function;
};

// Структура ATA устройства
typedef struct ata_device {
    void* chan;              // struct ata_channel*
    uint8_t slave;
    uint8_t present;
    uint8_t atapi;
    uint8_t type;
    
    uint64_t sectors;
    uint32_t sector_size;
    char model[41];
    char serial[21];
    char firmware[9];
    
    uint8_t lba48_supported;
    uint8_t dma_supported;
} ata_device_t;

// Структура канала (полная, без forward declaration проблем)
struct ata_channel {
    uint16_t iobase1;
    uint16_t iobase2;
    uint16_t iomaster;
    uint8_t irq;
    uint8_t chanid;
    uint32_t pci_bdf;
    struct ata_pci_device* pci_dev;
};

// Функции
void ata_init(void);
int ata_read_sectors(ata_device_t* dev, uint64_t lba, uint32_t count, void* buffer);
int ata_write_sectors(ata_device_t* dev, uint64_t lba, uint32_t count, void* buffer);
int ata_flush_cache(ata_device_t* dev);
ata_device_t* ata_get_device(int index);
int ata_get_device_count(void);
void ata_dump_info(void);

#endif