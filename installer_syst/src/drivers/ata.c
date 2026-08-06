#include "drivers/ata.h"
#include "drivers/serial.h"
#include "drivers/pci.h"
#include "kernel/ports.h"
#include "kernel/memory.h"
#include "drivers/timer.h"
#include "lib/string.h"
#include "kernel/timer_utils.h"

#define IDE_TIMEOUT  60000
#define CDROM_CDB_SIZE 12
#define MAX_MULTI_SECTORS 128

static ata_device_t ata_devices[4];
static int ata_device_count = 0;
static uint8_t ata_initialized = 0;
static uint32_t spinup_end = 0;

static struct ata_channel* ata_channels[4];
static int ata_channel_count = 0;

static int await_ide(uint8_t mask, uint8_t flags, uint16_t base, uint16_t timeout_ms) {
    uint32_t end = timer_calc_ms(timeout_ms);
    for (;;) {
        uint8_t status = inb(base + ATA_CB_STAT);
        if (status == 0xFF) return -1;
        if ((status & mask) == flags) return status;
        if (timer_check_ms(end)) return -1;
        yield();
    }
}

static int await_not_bsy(uint16_t base) {
    return await_ide(ATA_CB_STAT_BSY, 0, base, IDE_TIMEOUT);
}

static int await_rdy(uint16_t base) {
    return await_ide(ATA_CB_STAT_RDY, ATA_CB_STAT_RDY, base, IDE_TIMEOUT);
}

static int pause_await_not_bsy(uint16_t iobase1, uint16_t iobase2) {
    inb(iobase2 + ATA_CB_ASTAT);
    return await_not_bsy(iobase1);
}

static int ndelay_await_not_bsy(uint16_t iobase1) {
    ndelay(400);
    return await_not_bsy(iobase1);
}

static void ata_reset_port(uint16_t iobase2) {
    outb(iobase2 + ATA_CB_DC, ATA_CB_DC_HD15 | ATA_CB_DC_NIEN | ATA_CB_DC_SRST);
    udelay(10);
    outb(iobase2 + ATA_CB_DC, ATA_CB_DC_HD15 | ATA_CB_DC_NIEN);
    mdelay(10);
}

static void ata_reset(ata_device_t* adrive) {
    struct ata_channel* chan = (struct ata_channel*)adrive->chan;
    uint8_t slave = adrive->slave;
    uint16_t iobase1 = chan->iobase1;
    uint16_t iobase2 = chan->iobase2;

    ata_reset_port(iobase2);

    int status = await_not_bsy(iobase1);
    if (status < 0) return;
    
    if (slave) {
        uint32_t end = timer_calc_ms(IDE_TIMEOUT);
        for (;;) {
            outb(iobase1 + ATA_CB_DH, ATA_CB_DH_DEV1);
            status = ndelay_await_not_bsy(iobase1);
            if (status < 0) return;
            if (inb(iobase1 + ATA_CB_DH) == ATA_CB_DH_DEV1) break;
            if (timer_check_ms(end)) return;
        }
    } else {
        outb(iobase1 + ATA_CB_DH, ATA_CB_DH_DEV0);
        status = await_not_bsy(iobase1);
        if (status < 0) return;
    }
}

struct ata_pio_cmd {
    uint8_t feature;
    uint8_t sector_count;
    uint8_t lba_low;
    uint8_t lba_mid;
    uint8_t lba_high;
    uint8_t device;
    uint8_t command;
    uint8_t feature2;
    uint8_t sector_count2;
    uint8_t lba_low2;
    uint8_t lba_mid2;
    uint8_t lba_high2;
};

static int send_cmd(ata_device_t* adrive, struct ata_pio_cmd* cmd) {
    struct ata_channel* chan = (struct ata_channel*)adrive->chan;
    uint8_t slave = adrive->slave;
    uint16_t iobase1 = chan->iobase1;

    int status = await_not_bsy(iobase1);
    if (status < 0) return status;
    
    uint8_t newdh = ((cmd->device & ~ATA_CB_DH_DEV1) |
                     (slave ? ATA_CB_DH_DEV1 : ATA_CB_DH_DEV0));
    outb(iobase1 + ATA_CB_DH, newdh);
    
    status = await_not_bsy(iobase1);
    if (status < 0) return status;
    
    status = await_rdy(iobase1);
    if (status < 0) return status;

    if ((cmd->command & ~0x11) == ATA_CMD_READ_SECTORS_EXT) {
        outb(iobase1 + ATA_CB_FR, cmd->feature2);
        outb(iobase1 + ATA_CB_SC, cmd->sector_count2);
        outb(iobase1 + ATA_CB_SN, cmd->lba_low2);
        outb(iobase1 + ATA_CB_CL, cmd->lba_mid2);
        outb(iobase1 + ATA_CB_CH, cmd->lba_high2);
    }
    outb(iobase1 + ATA_CB_FR, cmd->feature);
    outb(iobase1 + ATA_CB_SC, cmd->sector_count);
    outb(iobase1 + ATA_CB_SN, cmd->lba_low);
    outb(iobase1 + ATA_CB_CL, cmd->lba_mid);
    outb(iobase1 + ATA_CB_CH, cmd->lba_high);
    outb(iobase1 + ATA_CB_CMD, cmd->command);

    return 0;
}

static int ata_wait_data(uint16_t iobase1) {
    int status = ndelay_await_not_bsy(iobase1);
    if (status < 0) return status;

    if (status & ATA_CB_STAT_ERR) return -4;
    if (!(status & ATA_CB_STAT_DRQ)) return -5;
    return 0;
}

// МУЛЬТИСЕКТОРНАЯ ПЕРЕДАЧА
static int ata_pio_transfer_multi(ata_device_t* adrive, int iswrite, 
                                   uint32_t sector_count, void* buffer) {
    struct ata_channel* chan = (struct ata_channel*)adrive->chan;
    uint16_t iobase1 = chan->iobase1;
    uint16_t iobase2 = chan->iobase2;
    void* buf = buffer;
    uint32_t remaining = sector_count;
    int status;
    
    while (remaining > 0) {
        status = await_not_bsy(iobase1);
        if (status < 0) return status;
        
        if (!(status & ATA_CB_STAT_DRQ)) {
            serial_puts("[ATA] PIO: no DRQ, status=0x");
            serial_puts_num_hex(status);
            serial_puts("\n");
            return -5;
        }
        
        // Читаем/пишем блоками по 256 слов (512 байт) за раз
        if (iswrite) {
            for (uint32_t i = 0; i < 256; i++) {
                outw(iobase1 + ATA_CB_DATA, ((uint16_t*)buf)[i]);
            }
        } else {
            for (uint32_t i = 0; i < 256; i++) {
                ((uint16_t*)buf)[i] = inw(iobase1 + ATA_CB_DATA);
            }
        }
        
        buf += 512;
        remaining--;
        
        // После каждого сектора проверяем ошибки, но не ждем BSY
        status = inb(iobase1 + ATA_CB_STAT);
        if (status & ATA_CB_STAT_ERR) {
            serial_puts("[ATA] Error during transfer, status=0x");
            serial_puts_num_hex(status);
            serial_puts("\n");
            return -4;
        }
    }
    
    // Финальное ожидание готовности
    status = await_not_bsy(iobase1);
    if (status < 0) return status;
    
    return 0;
}

static int ata_readwrite_multi(ata_device_t* adrive, uint64_t lba, 
                                uint32_t count, void* buffer, int iswrite) {
    struct ata_pio_cmd cmd;
    memset(&cmd, 0, sizeof(cmd));
    
    uint32_t use_lba48 = (count > 256 || lba + count > (1ULL << 28));
    uint32_t sectors_per_cmd = (count > MAX_MULTI_SECTORS) ? MAX_MULTI_SECTORS : count;
    
    // Используем READ/WRITE MULTIPLE если возможно
    uint8_t read_cmd = use_lba48 ? 0x29 : 0xC4;  // READ MULTIPLE EXT / READ MULTIPLE
    uint8_t write_cmd = use_lba48 ? 0x39 : 0xC5; // WRITE MULTIPLE EXT / WRITE MULTIPLE
    
    if (use_lba48) {
        cmd.sector_count2 = sectors_per_cmd >> 8;
        cmd.lba_low2 = (lba >> 24) & 0xFF;
        cmd.lba_mid2 = (lba >> 32) & 0xFF;
        cmd.lba_high2 = (lba >> 40) & 0xFF;
        cmd.command = iswrite ? write_cmd : read_cmd;
        cmd.sector_count = sectors_per_cmd & 0xFF;
        cmd.lba_low = lba & 0xFF;
        cmd.lba_mid = (lba >> 8) & 0xFF;
        cmd.lba_high = (lba >> 16) & 0xFF;
        cmd.device = ((lba >> 24) & 0x0F) | ATA_CB_DH_LBA;
        cmd.feature = 0;
        cmd.feature2 = 0;
    } else {
        cmd.command = iswrite ? write_cmd : read_cmd;
        cmd.sector_count = sectors_per_cmd;
        cmd.lba_low = lba & 0xFF;
        cmd.lba_mid = (lba >> 8) & 0xFF;
        cmd.lba_high = (lba >> 16) & 0xFF;
        cmd.device = ((lba >> 24) & 0x0F) | ATA_CB_DH_LBA;
        cmd.feature = 0;
    }
    
    struct ata_channel* chan = (struct ata_channel*)adrive->chan;
    uint16_t iobase2 = chan->iobase2;
    
    outb(iobase2 + ATA_CB_DC, ATA_CB_DC_HD15 | ATA_CB_DC_NIEN);
    
    int ret = send_cmd(adrive, &cmd);
    if (ret == 0) {
        ret = ata_wait_data(chan->iobase1);
        if (ret == 0) {
            ret = ata_pio_transfer_multi(adrive, iswrite, sectors_per_cmd, buffer);
        }
    }
    
    outb(iobase2 + ATA_CB_DC, ATA_CB_DC_HD15);
    return ret;
}

static int ata_readwrite(ata_device_t* adrive, uint64_t lba, 
                         uint32_t count, void* buffer, int iswrite) {
    if (count == 0) return 0;
    
    uint32_t remaining = count;
    uint32_t offset = 0;
    uint8_t* buf = (uint8_t*)buffer;
    
    while (remaining > 0) {
        uint32_t chunk = (remaining > MAX_MULTI_SECTORS) ? MAX_MULTI_SECTORS : remaining;
        int ret = ata_readwrite_multi(adrive, lba + offset, chunk, buf + offset * 512, iswrite);
        if (ret != 0) return ret;
        remaining -= chunk;
        offset += chunk;
        
        // Даем диску немного отдыха между большими блоками
        if (offset % (MAX_MULTI_SECTORS * 10) == 0) {
            yield();
        }
    }
    
    return 0;
}

int ata_flush_cache(ata_device_t* dev) {
    if (!dev || !dev->present) return -1;
    
    struct ata_channel* chan = (struct ata_channel*)dev->chan;
    uint16_t iobase1 = chan->iobase1;
    uint16_t iobase2 = chan->iobase2;
    
    int status = await_not_bsy(iobase1);
    if (status < 0) return status;
    
    struct ata_pio_cmd cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.command = ATA_CMD_FLUSH_CACHE_EXT;
    
    outb(iobase2 + ATA_CB_DC, ATA_CB_DC_HD15 | ATA_CB_DC_NIEN);
    
    int ret = send_cmd(dev, &cmd);
    if (ret < 0) {
        outb(iobase2 + ATA_CB_DC, ATA_CB_DC_HD15);
        return ret;
    }
    
    status = await_not_bsy(iobase1);
    outb(iobase2 + ATA_CB_DC, ATA_CB_DC_HD15);
    
    return status < 0 ? status : 0;
}

int atapi_cmd_data(ata_device_t* adrive, void* cdbcmd, uint16_t blocksize,
                   uint32_t count, void* buffer) {
    struct ata_channel* chan = (struct ata_channel*)adrive->chan;
    uint16_t iobase1 = chan->iobase1;
    uint16_t iobase2 = chan->iobase2;

    struct ata_pio_cmd cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.lba_mid = blocksize & 0xFF;
    cmd.lba_high = (blocksize >> 8) & 0xFF;
    cmd.command = ATA_CMD_PACKET;

    outb(iobase2 + ATA_CB_DC, ATA_CB_DC_HD15 | ATA_CB_DC_NIEN);

    int ret = send_cmd(adrive, &cmd);
    if (ret == 0) {
        ret = ata_wait_data(iobase1);
        if (ret == 0) {
            uint16_t* cdb = (uint16_t*)cdbcmd;
            for (int i = 0; i < CDROM_CDB_SIZE / 2; i++) {
                outw(iobase1 + ATA_CB_DATA, cdb[i]);
            }
            
            int status = pause_await_not_bsy(iobase1, iobase2);
            if (status >= 0 && blocksize && (status & ATA_CB_STAT_DRQ)) {
                ret = ata_pio_transfer_multi(adrive, 0, count, buffer);
            } else if (status >= 0 && !(status & ATA_CB_STAT_ERR)) {
                ret = 0;
            } else {
                ret = -1;
            }
        }
    }

    outb(iobase2 + ATA_CB_DC, ATA_CB_DC_HD15);
    return ret;
}

static int send_ata_identity(ata_device_t* adrive, uint16_t* buffer, int command) {
    memset(buffer, 0, 512);
    
    struct ata_channel* chan = (struct ata_channel*)adrive->chan;
    uint16_t iobase2 = chan->iobase2;
    
    int status = await_not_bsy(chan->iobase1);
    if (status < 0) return status;
    
    struct ata_pio_cmd cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.command = command;

    outb(iobase2 + ATA_CB_DC, ATA_CB_DC_HD15 | ATA_CB_DC_NIEN);
    
    int ret = send_cmd(adrive, &cmd);
    if (ret == 0) {
        ret = ata_wait_data(chan->iobase1);
        if (ret == 0) {
            for (int i = 0; i < 256; i++) {
                buffer[i] = inw(chan->iobase1 + ATA_CB_DATA);
            }
        }
    }
    
    outb(iobase2 + ATA_CB_DC, ATA_CB_DC_HD15);
    return ret;
}

static void ata_fix_string(uint8_t* str, int len) {
    for (int i = 0; i < len; i += 2) {
        uint8_t tmp = str[i];
        str[i] = str[i + 1];
        str[i + 1] = tmp;
    }
    for (int i = len - 1; i >= 0 && str[i] == ' '; i--) str[i] = '\0';
}

static void init_atadrive(ata_device_t* adrive, struct ata_channel* chan, 
                          uint8_t slave, uint16_t* buffer, int is_atapi) {
    adrive->chan = chan;
    adrive->slave = slave;
    adrive->present = 1;
    adrive->atapi = is_atapi;
    adrive->type = is_atapi ? ATA_DEV_ATAPI : ATA_DEV_ATA;
    adrive->sector_size = 512;
    
    memcpy(adrive->model, (uint8_t*)&buffer[27], 40);
    adrive->model[40] = '\0';
    ata_fix_string((uint8_t*)adrive->model, 40);
    
    memcpy(adrive->serial, (uint8_t*)&buffer[10], 20);
    adrive->serial[20] = '\0';
    ata_fix_string((uint8_t*)adrive->serial, 20);
    
    memcpy(adrive->firmware, (uint8_t*)&buffer[23], 8);
    adrive->firmware[8] = '\0';
    ata_fix_string((uint8_t*)adrive->firmware, 8);
    
    if (buffer[83] & (1 << 10)) {
        adrive->lba48_supported = 1;
        adrive->sectors = *(uint64_t*)&buffer[100];
    } else {
        adrive->lba48_supported = 0;
        adrive->sectors = *(uint32_t*)&buffer[60];
    }
    
    adrive->dma_supported = (buffer[63] & 0xFF00) ? 1 : 0;
}

static void ata_detect_channel(struct ata_channel* chan) {
    ata_device_t dummy;
    memset(&dummy, 0, sizeof(dummy));
    dummy.chan = chan;
    
    int didreset = 0;
    
    for (int slave = 0; slave <= 1; slave++) {
        uint16_t iobase1 = chan->iobase1;
        uint16_t iobase2 = chan->iobase2;
        
        uint8_t newdh = slave ? ATA_CB_DH_DEV1 : ATA_CB_DH_DEV0;
        outb(iobase1 + ATA_CB_DH, newdh);
        ndelay(400);
        
        outb(iobase1 + ATA_CB_SC, 0x55);
        outb(iobase1 + ATA_CB_SN, 0xAA);
        uint8_t sc = inb(iobase1 + ATA_CB_SC);
        uint8_t sn = inb(iobase1 + ATA_CB_SN);
        uint8_t dh = inb(iobase1 + ATA_CB_DH);
        
        if (sc != 0x55 || sn != 0xAA || dh != newdh) continue;
        
        dummy.slave = slave;
        
        if (!didreset) {
            ata_reset(&dummy);
            didreset = 1;
            await_not_bsy(iobase1);
        }
        
        uint16_t buffer[256];
        
        int ret = send_ata_identity(&dummy, buffer, ATA_CMD_IDENTIFY_PACKET_DEVICE);
        if (ret == 0 && ata_device_count < 4) {
            ata_device_t* adrive = &ata_devices[ata_device_count];
            init_atadrive(adrive, chan, slave, buffer, 1);
            ata_device_count++;
            serial_puts("[ATA] Found ATAPI: ");
            serial_puts(adrive->model);
            serial_puts("\n");
            continue;
        }
        
        ret = send_ata_identity(&dummy, buffer, ATA_CMD_IDENTIFY_DEVICE);
        if (ret == 0 && ata_device_count < 4) {
            ata_device_t* adrive = &ata_devices[ata_device_count];
            init_atadrive(adrive, chan, slave, buffer, 0);
            ata_device_count++;
            serial_puts("[ATA] Found ATA: ");
            serial_puts(adrive->model);
            serial_puts(" (");
            serial_puts_num_ulong(adrive->sectors / 2 / 1024);
            serial_puts(" MB)\n");
            
            ata_reset_port(iobase2);
            await_not_bsy(iobase1);
            continue;
        }
    }
}

static int powerup_await_non_bsy(uint16_t base) {
    uint8_t status;
    uint32_t end = timer_calc_ms(IDE_TIMEOUT);
    
    for (;;) {
        status = inb(base + ATA_CB_STAT);
        if (status == 0xFF) return -1;
        if (!(status & ATA_CB_STAT_BSY)) break;
        if (timer_check_ms(end)) return -1;
        yield();
    }
    return status;
}

static void ata_init_channel(struct ata_pci_device* pci, int irq,
                             uint32_t port1, uint32_t port2, uint32_t master) {
    static int chanid = 0;
    
    struct ata_channel* chan = (struct ata_channel*)kmalloc(sizeof(struct ata_channel));
    if (!chan) {
        serial_puts("[ATA] Failed to allocate channel\n");
        return;
    }
    
    chan->chanid = chanid++;
    chan->irq = irq;
    chan->pci_bdf = pci ? (pci->bus << 8) | (pci->device << 3) | pci->function : -1;
    chan->pci_dev = pci;
    chan->iobase1 = port1;
    chan->iobase2 = port2;
    chan->iomaster = master;
    
    serial_puts("[ATA] Channel ");
    serial_puts_num(chan->chanid);
    serial_puts(": IO=0x");
    serial_puts_num_hex(port1);
    serial_puts(" CTRL=0x");
    serial_puts_num_hex(port2);
    serial_puts(" IRQ=");
    serial_puts_num(irq);
    serial_puts("\n");
    
    ata_reset_port(port2);
    
    int status = powerup_await_non_bsy(port1);
    if (status < 0) {
        serial_puts("[ATA] No devices on channel\n");
        kfree(chan);
        return;
    }
    
    if (ata_channel_count < 4) {
        ata_channels[ata_channel_count++] = chan;
    }
    
    ata_detect_channel(chan);
}

static void ata_scan_pci(void) {
    for (int bus = 0; bus < 256; bus++) {
        for (int dev = 0; dev < 32; dev++) {
            for (int func = 0; func < 8; func++) {
                uint16_t vendor = pci_read16(bus, dev, func, 0);
                if (vendor == 0xFFFF) continue;
                
                uint8_t class = pci_read8(bus, dev, func, 0x0B);
                uint8_t subclass = pci_read8(bus, dev, func, 0x0A);
                
                if (class == 0x01 && subclass == 0x01) {
                    uint8_t prog_if = pci_read8(bus, dev, func, 0x09);
                    
                    serial_puts("[ATA] Found PCI IDE at ");
                    serial_puts_num(bus);
                    serial_puts(":");
                    serial_puts_num(dev);
                    serial_puts(".");
                    serial_puts_num(func);
                    serial_puts("\n");
                    
                    pci_enable_bus_master(bus, dev, func);
                    pci_enable_io_space(bus, dev, func);
                    
                    uint8_t pciirq = pci_read8(bus, dev, func, 0x3C);
                    int master = 0;
                    
                    if (prog_if & 0x80) {
                        uint32_t bar4 = pci_read32(bus, dev, func, 0x20);
                        if (bar4 & 1) {
                            master = bar4 & 0xFFFC;
                        }
                    }
                    
                    uint32_t port1, port2, irq1, port3, port4, irq2;
                    
                    if (prog_if & 1) {
                        port1 = pci_read32(bus, dev, func, 0x10) & 0xFFFC;
                        port2 = pci_read32(bus, dev, func, 0x14) & 0xFFFC;
                        irq1 = pciirq;
                    } else {
                        port1 = PORT_ATA1_CMD_BASE;
                        port2 = PORT_ATA1_CTRL_BASE;
                        irq1 = 14;
                    }
                    
                    if (prog_if & 4) {
                        port3 = pci_read32(bus, dev, func, 0x18) & 0xFFFC;
                        port4 = pci_read32(bus, dev, func, 0x1C) & 0xFFFC;
                        irq2 = pciirq;
                    } else {
                        port3 = PORT_ATA2_CMD_BASE;
                        port4 = PORT_ATA2_CTRL_BASE;
                        irq2 = 15;
                    }
                    
                    struct ata_pci_device pci_dev;
                    pci_dev.bus = bus;
                    pci_dev.device = dev;
                    pci_dev.function = func;
                    
                    ata_init_channel(&pci_dev, irq1, port1, port2, master);
                    ata_init_channel(&pci_dev, irq2, port3, port4, master ? master + 8 : 0);
                    
                    return;
                }
            }
        }
    }
    
    serial_puts("[ATA] No PCI IDE found, trying legacy ports\n");
    ata_init_channel(NULL, 14, PORT_ATA1_CMD_BASE, PORT_ATA1_CTRL_BASE, 0);
    ata_init_channel(NULL, 15, PORT_ATA2_CMD_BASE, PORT_ATA2_CTRL_BASE, 0);
}

void ata_init(void) {
    if (ata_initialized) return;
    
    serial_puts("[ATA] Initializing ATA driver...\n");
    
    ata_device_count = 0;
    memset(ata_devices, 0, sizeof(ata_devices));
    memset(ata_channels, 0, sizeof(ata_channels));
    ata_channel_count = 0;
    spinup_end = timer_calc_ms(IDE_TIMEOUT);
    
    int ata_controller_found = 0;
    for (int bus = 0; bus < 256; bus++) {
        for (int dev = 0; dev < 32; dev++) {
            for (int func = 0; func < 8; func++) {
                uint16_t vendor = pci_read16(bus, dev, func, 0);
                if (vendor == 0xFFFF) continue;

                uint8_t class = pci_read8(bus, dev, func, 0x0B);
                uint8_t subclass = pci_read8(bus, dev, func, 0x0A);
                
                if (class == 0x01 && subclass == 0x01) {
                    ata_controller_found = 1;
                    break;
                }
            }
            if (ata_controller_found) break;
        }
        if (ata_controller_found) break;
    }
    
    if (!ata_controller_found) {
        serial_puts("[ATA] No IDE controller found, skipping\n");
        ata_initialized = 1;
        return;
    }
    
    ata_scan_pci();
    
    if (ata_device_count == 0) {
        for (int i = 0; i < ata_channel_count; i++) {
            if (ata_channels[i]) {
                kfree(ata_channels[i]);
                ata_channels[i] = NULL;
            }
        }
        ata_channel_count = 0;
        serial_puts("[ATA] No ATA/ATAPI devices found\n");
    } else {
        serial_puts("[ATA] Multi-sector mode enabled for ATA disks\n");
    }
    
    ata_initialized = 1;
    serial_puts("[ATA] Initialization complete, found ");
    serial_puts_num(ata_device_count);
    serial_puts(" devices\n");
}

int ata_read_sectors(ata_device_t* dev, uint64_t lba, uint32_t count, void* buffer) {
    if (!dev || !dev->present) return -1;
    if (count == 0) return 0;
    return ata_readwrite(dev, lba, count, buffer, 0);
}

int ata_write_sectors(ata_device_t* dev, uint64_t lba, uint32_t count, void* buffer) {
    if (!dev || !dev->present) return -1;
    if (count == 0) return 0;
    return ata_readwrite(dev, lba, count, buffer, 1);
}

ata_device_t* ata_get_device(int index) {
    if (index >= ata_device_count) return NULL;
    return &ata_devices[index];
}

int ata_get_device_count(void) {
    return ata_device_count;
}

void ata_dump_info(void) {
    serial_puts("\n=== ATA DEVICES ===\n");
    for (int i = 0; i < ata_device_count; i++) {
        ata_device_t* dev = &ata_devices[i];
        serial_puts("Device ");
        serial_puts_num(i);
        serial_puts(": ");
        serial_puts(dev->model);
        serial_puts("\n  Type: ");
        serial_puts(dev->atapi ? "ATAPI" : "ATA");
        serial_puts("\n  Size: ");
        serial_puts_num_ulong(dev->sectors);
        serial_puts(" sectors (");
        serial_puts_num_ulong(dev->sectors / 2 / 1024);
        serial_puts(" MB)\n");
        if (dev->serial[0]) {
            serial_puts("  Serial: ");
            serial_puts(dev->serial);
            serial_puts("\n");
        }
        serial_puts("  Firmware: ");
        serial_puts(dev->firmware);
        serial_puts("\n");
    }
    serial_puts("==================\n");
}