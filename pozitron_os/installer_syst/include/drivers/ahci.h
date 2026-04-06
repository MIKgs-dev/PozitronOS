#ifndef __AHCI_H
#define __AHCI_H

#include <stdint.h>

// SATA Command FIS
struct sata_cmd_fis {
    uint8_t reg;
    uint8_t pmp_type;
    uint8_t command;
    uint8_t feature;
    uint8_t lba_low;
    uint8_t lba_mid;
    uint8_t lba_high;
    uint8_t device;
    uint8_t lba_low2;
    uint8_t lba_mid2;
    uint8_t lba_high2;
    uint8_t feature2;
    uint8_t sector_count;
    uint8_t sector_count2;
    uint8_t res_1;
    uint8_t control;
    uint8_t res_2[64 - 16];
};

// AHCI контроллер
struct ahci_ctrl {
    uint32_t iobase;
    uint32_t caps;
    uint32_t ports;
    uint8_t irq;
    uint32_t pci_bdf;
    void* pci_dev;
};

// AHCI команда
struct ahci_cmd {
    struct sata_cmd_fis fis;
    uint8_t atapi[0x20];
    uint8_t res[0x20];
    struct {
        uint32_t base;
        uint32_t baseu;
        uint32_t res;
        uint32_t flags;
    } prdt[];
};

// Command list entry
struct ahci_list {
    uint32_t flags;
    uint32_t bytes;
    uint32_t base;
    uint32_t baseu;
    uint32_t res[4];
};

// FIS receive area
struct ahci_fis {
    uint8_t dsfis[0x1c];
    uint8_t res_1[0x04];
    uint8_t psfis[0x14];
    uint8_t res_2[0x0c];
    uint8_t rfis[0x14];
    uint8_t res_3[0x04];
    uint8_t sdbfis[0x08];
    uint8_t ufis[0x40];
    uint8_t res_4[0x60];
};

// AHCI порт
struct ahci_port {
    struct ahci_ctrl* ctrl;
    struct ahci_list* list;
    struct ahci_fis* fis;
    struct ahci_cmd* cmd;
    uint32_t pnr;
    uint32_t atapi;
    uint32_t present;
    uint64_t sectors;
    uint32_t sector_size;
    char model[41];
    char serial[21];
    char firmware[9];
    uint8_t lba48_supported;
};

// Функции
void ahci_init(void);
int ahci_read_sectors(int port, uint64_t lba, uint32_t count, void* buffer);
int ahci_write_sectors(int port, uint64_t lba, uint32_t count, void* buffer);
int ahci_flush_cache(int port);
int ahci_get_port_count(void);
void ahci_dump_info(void);
struct ahci_port* ahci_get_port(int index);


// AHCI регистры
#define HOST_CAP                  0x00
#define HOST_CTL                  0x04
#define HOST_IRQ_STAT             0x08
#define HOST_PORTS_IMPL           0x0c
#define HOST_VERSION              0x10

#define HOST_CTL_RESET            (1 << 0)
#define HOST_CTL_IRQ_EN           (1 << 1)
#define HOST_CTL_AHCI_EN          (1 << 31)

#define PORT_LST_ADDR             0x00
#define PORT_LST_ADDR_HI          0x04
#define PORT_FIS_ADDR             0x08
#define PORT_FIS_ADDR_HI          0x0c
#define PORT_IRQ_STAT             0x10
#define PORT_IRQ_MASK             0x14
#define PORT_CMD                  0x18
#define PORT_TFDATA               0x20
#define PORT_SIG                  0x24
#define PORT_SCR_STAT             0x28
#define PORT_SCR_CTL              0x2c
#define PORT_SCR_ERR              0x30
#define PORT_SCR_ACT              0x34
#define PORT_CMD_ISSUE            0x38

#define PORT_IRQ_PHYRDY           (1 << 22)
#define PORT_IRQ_CONNECT          (1 << 6)
#define PORT_IRQ_SG_DONE          (1 << 5)
#define PORT_IRQ_D2H_REG_FIS      (1 << 0)

#define PORT_CMD_FIS_RX           (1 << 4)
#define PORT_CMD_START            (1 << 0)
#define PORT_CMD_SPIN_UP          (1 << 1)
#define PORT_CMD_LIST_ON          (1 << 15)
#define PORT_CMD_FIS_ON           (1 << 14)

#define ATA_CB_STAT_BSY           0x80
#define ATA_CB_STAT_RDY           0x40
#define ATA_CB_STAT_DF            0x20
#define ATA_CB_STAT_DRQ           0x08
#define ATA_CB_STAT_ERR           0x01
#define ATA_CB_DH_LBA             0x40

#define ATA_CMD_READ_DMA_EXT      0x25
#define ATA_CMD_WRITE_DMA_EXT     0x35
#define ATA_CMD_READ_DMA          0xC8
#define ATA_CMD_WRITE_DMA         0xCA
#define ATA_CMD_PACKET            0xA0
#define ATA_CMD_IDENTIFY_DEVICE   0xEC
#define ATA_CMD_IDENTIFY_PACKET_DEVICE 0xA1
#define ATA_CMD_FLUSH_CACHE       0xE7
#define ATA_CMD_FLUSH_CACHE_EXT   0xEA

#define CDROM_SECTOR_SIZE         2048
#define DISK_SECTOR_SIZE          512
#define CDROM_CDB_SIZE            12

#endif