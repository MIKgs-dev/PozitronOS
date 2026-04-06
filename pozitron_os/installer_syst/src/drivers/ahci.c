#include "drivers/ahci.h"
#include "drivers/serial.h"
#include "drivers/pci.h"
#include "drivers/timer.h"
#include "kernel/ports.h"
#include "kernel/memory.h"
#include "lib/string.h"
#include "kernel/timer_utils.h"

#ifndef readl
#define readl(addr) (*(volatile uint32_t*)(addr))
#define writel(addr, val) (*(volatile uint32_t*)(addr) = (val))
#endif

#define AHCI_TIMEOUT_NORMAL   30000
#define AHCI_TIMEOUT_FLUSH    60000
#define AHCI_RESET_TIMEOUT    5000
#define AHCI_LINK_TIMEOUT     100

static struct ahci_port ahci_ports[32];
static int ahci_port_count = 0;
static uint8_t ahci_initialized = 0;
static uint32_t ahci_iobase = 0;
static uint32_t ahci_caps = 0;
static uint32_t ahci_ports_impl = 0;

static uint32_t ahci_readl(uint32_t reg) {
    return readl((void*)(uintptr_t)(ahci_iobase + reg));
}

static void ahci_writel(uint32_t reg, uint32_t val) {
    writel((void*)(uintptr_t)(ahci_iobase + reg), val);
}

static uint32_t ahci_port_readl(uint32_t pnr, uint32_t reg) {
    uint32_t port_reg = 0x100 + pnr * 0x80 + reg;
    return ahci_readl(port_reg);
}

static void ahci_port_writel(uint32_t pnr, uint32_t reg, uint32_t val) {
    uint32_t port_reg = 0x100 + pnr * 0x80 + reg;
    ahci_writel(port_reg, val);
}

static int ahci_wait_ready(struct ahci_port* port, uint32_t timeout_ms) {
    uint32_t pnr = port->pnr;
    uint32_t end = timer_calc_ms(timeout_ms);
    
    while (1) {
        uint32_t tf = ahci_port_readl(pnr, PORT_TFDATA);
        uint32_t status = tf & 0xFF;
        
        if (!(status & ATA_CB_STAT_BSY) && (status & ATA_CB_STAT_RDY)) {
            return 0;
        }
        
        if (timer_check_ms(end)) {
            serial_puts("[AHCI] Timeout waiting for disk ready\n");
            return -1;
        }
        yield();
    }
}

static int ahci_wait_clear_busy(struct ahci_port* port, uint32_t timeout_ms) {
    uint32_t pnr = port->pnr;
    uint32_t end = timer_calc_ms(timeout_ms);
    
    while (1) {
        uint32_t tf = ahci_port_readl(pnr, PORT_TFDATA);
        uint32_t status = tf & 0xFF;
        
        if (!(status & ATA_CB_STAT_BSY)) {
            return 0;
        }
        
        if (timer_check_ms(end)) {
            serial_puts("[AHCI] Timeout waiting for BSY clear\n");
            return -1;
        }
        yield();
    }
}

static int ahci_wait_cmd_complete(struct ahci_port* port, uint32_t timeout_ms) {
    uint32_t pnr = port->pnr;
    uint32_t end = timer_calc_ms(timeout_ms);
    
    while (1) {
        uint32_t ci = ahci_port_readl(pnr, PORT_CMD_ISSUE);
        if (!(ci & 1)) {
            return 0;
        }
        
        if (timer_check_ms(end)) {
            serial_puts("[AHCI] Command timeout\n");
            return -1;
        }
        yield();
    }
}

static void sata_prep_readwrite(struct sata_cmd_fis* fis, uint64_t lba, 
                                uint32_t count, int iswrite) {
    uint8_t command;
    memset(fis, 0, sizeof(*fis));

    if (count >= (1 << 8) || lba + count >= (1ULL << 28)) {
        fis->sector_count2 = count >> 8;
        fis->lba_low2 = (lba >> 24) & 0xFF;
        fis->lba_mid2 = (lba >> 32) & 0xFF;
        fis->lba_high2 = (lba >> 40) & 0xFF;
        lba &= 0xFFFFFF;
        command = iswrite ? ATA_CMD_WRITE_DMA_EXT : ATA_CMD_READ_DMA_EXT;
    } else {
        command = iswrite ? ATA_CMD_WRITE_DMA : ATA_CMD_READ_DMA;
    }
    fis->feature = 1;
    fis->command = command;
    fis->sector_count = count;
    fis->lba_low = lba & 0xFF;
    fis->lba_mid = (lba >> 8) & 0xFF;
    fis->lba_high = (lba >> 16) & 0xFF;
    fis->device = ((lba >> 24) & 0x0F) | ATA_CB_DH_LBA;
}

static int ahci_command(struct ahci_port* port, int iswrite, int isatapi,
                        void* buffer, uint32_t bsize, int is_flush) {
    uint32_t val, status, flags, intbits;
    struct ahci_cmd* cmd = port->cmd;
    struct ahci_fis* fis = port->fis;
    struct ahci_list* list = port->list;
    uint32_t pnr = port->pnr;
    uint32_t timeout = is_flush ? AHCI_TIMEOUT_FLUSH : AHCI_TIMEOUT_NORMAL;

    if (ahci_wait_ready(port, timeout) != 0) {
        serial_puts("[AHCI] Disk not ready before command\n");
        return -1;
    }

    cmd->fis.reg = 0x27;
    cmd->fis.pmp_type = 1 << 7;
    if (buffer && bsize > 0) {
        cmd->prdt[0].base = (uint32_t)buffer;
        cmd->prdt[0].baseu = 0;
        cmd->prdt[0].flags = bsize - 1;
    }

    flags = (1 << 16) | (iswrite ? (1 << 6) : 0) | (isatapi ? (1 << 5) : 0) | (5 << 0);
    list[0].flags = flags;
    list[0].bytes = 0;
    list[0].base = (uint32_t)cmd;
    list[0].baseu = 0;

    intbits = ahci_port_readl(pnr, PORT_IRQ_STAT);
    if (intbits)
        ahci_port_writel(pnr, PORT_IRQ_STAT, intbits);
    
    ahci_port_writel(pnr, PORT_SCR_ACT, 1);
    ahci_port_writel(pnr, PORT_CMD_ISSUE, 1);

    if (ahci_wait_cmd_complete(port, timeout) != 0) {
        serial_puts("[AHCI] Command did not complete\n");
        return -1;
    }

    if (!is_flush) {
        if (ahci_wait_clear_busy(port, timeout) != 0) {
            serial_puts("[AHCI] BSY did not clear\n");
            return -1;
        }
    }
    
    if (ahci_wait_ready(port, timeout) != 0) {
        serial_puts("[AHCI] Disk not ready after command\n");
        return -1;
    }

    status = fis->rfis[2];
    
    int success = (0x00 == (status & (ATA_CB_STAT_BSY | ATA_CB_STAT_DF | ATA_CB_STAT_ERR)) &&
                   ATA_CB_STAT_RDY == (status & ATA_CB_STAT_RDY));
    
    if (!success) {
        serial_puts("[AHCI] Command error, status=0x");
        serial_puts_num_hex(status);
        serial_puts("\n");
        
        val = ahci_port_readl(pnr, PORT_CMD);
        ahci_port_writel(pnr, PORT_CMD, val & ~PORT_CMD_START);
        
        uint32_t end = timer_calc_ms(AHCI_RESET_TIMEOUT);
        while (1) {
            val = ahci_port_readl(pnr, PORT_CMD);
            if ((val & PORT_CMD_LIST_ON) == 0) break;
            if (timer_check_ms(end)) break;
            yield();
        }
        
        val = ahci_port_readl(pnr, PORT_SCR_ERR);
        ahci_port_writel(pnr, PORT_SCR_ERR, val);
        val = ahci_port_readl(pnr, PORT_IRQ_STAT);
        ahci_port_writel(pnr, PORT_IRQ_STAT, val);
        
        val = ahci_port_readl(pnr, PORT_CMD);
        ahci_port_writel(pnr, PORT_CMD, val | PORT_CMD_START);
        return -1;
    }
    
    return 0;
}

static int ahci_flush_command(struct ahci_port* port) {
    struct sata_cmd_fis fis;
    memset(&fis, 0, sizeof(fis));
    fis.command = ATA_CMD_FLUSH_CACHE_EXT;
    fis.feature = 0;
    
    memcpy(&port->cmd->fis, &fis, sizeof(fis));
    port->list[0].flags = (1 << 16) | (5 << 0);
    port->list[0].base = (uint32_t)(uintptr_t)port->cmd;
    
    return ahci_command(port, 0, 0, NULL, 0, 1);
}

static void ahci_port_reset(uint32_t pnr) {
    uint32_t val;
    uint32_t end = timer_calc_ms(AHCI_RESET_TIMEOUT);
    for (;;) {
        val = ahci_port_readl(pnr, PORT_CMD);
        if (!(val & (PORT_CMD_FIS_RX | PORT_CMD_START | PORT_CMD_LIST_ON | PORT_CMD_FIS_ON)))
            break;
        val &= ~(PORT_CMD_FIS_RX | PORT_CMD_START);
        ahci_port_writel(pnr, PORT_CMD, val);
        if (timer_check_ms(end)) break;
        yield();
    }
    ahci_port_writel(pnr, PORT_IRQ_MASK, 0);
    val = ahci_port_readl(pnr, PORT_IRQ_STAT);
    if (val)
        ahci_port_writel(pnr, PORT_IRQ_STAT, val);
}

static void ata_fix_string(uint8_t* str, int len) {
    for (int i = 0; i < len; i += 2) {
        uint8_t tmp = str[i];
        str[i] = str[i + 1];
        str[i + 1] = tmp;
    }
    for (int i = len - 1; i >= 0 && str[i] == ' '; i--) str[i] = '\0';
}

static void ahci_free_port_resources(struct ahci_port* port) {
    if (!port) return;
    
    if (port->cmd) {
        kfree(port->cmd);
        port->cmd = NULL;
    }
    if (port->list) {
        kfree(port->list);
        port->list = NULL;
    }
    if (port->fis) {
        kfree(port->fis);
        port->fis = NULL;
    }
}

static int ahci_port_setup(uint32_t pnr) {
    uint32_t cmd, stat, tf;
    uint16_t buffer[256];
    struct ahci_port* port = &ahci_ports[ahci_port_count];
    
    port->pnr = pnr;
    port->present = 0;
    port->ctrl = NULL;
    port->cmd = NULL;
    port->list = NULL;
    port->fis = NULL;
    
    serial_puts("[AHCI]   Setting up port ");
    serial_puts_num(pnr);
    serial_puts("\n");
    
    cmd = ahci_port_readl(pnr, PORT_CMD);
    cmd |= PORT_CMD_FIS_RX;
    ahci_port_writel(pnr, PORT_CMD, cmd);
    
    cmd |= PORT_CMD_SPIN_UP;
    ahci_port_writel(pnr, PORT_CMD, cmd);
    
    uint32_t end = timer_calc_ms(AHCI_LINK_TIMEOUT);
    for (;;) {
        stat = ahci_port_readl(pnr, PORT_SCR_STAT);
        if ((stat & 0x07) == 0x03) break;
        if (timer_check_ms(end)) {
            serial_puts("[AHCI]   Link timeout\n");
            return -1;
        }
        yield();
    }
    
    uint32_t err = ahci_port_readl(pnr, PORT_SCR_ERR);
    if (err)
        ahci_port_writel(pnr, PORT_SCR_ERR, err);
    
    end = timer_calc_ms(AHCI_TIMEOUT_NORMAL);
    for (;;) {
        tf = ahci_port_readl(pnr, PORT_TFDATA);
        if (!(tf & (ATA_CB_STAT_BSY | ATA_CB_STAT_DRQ))) break;
        if (timer_check_ms(end)) {
            serial_puts("[AHCI]   Device ready timeout\n");
            return -1;
        }
        yield();
    }
    
    cmd |= PORT_CMD_START;
    ahci_port_writel(pnr, PORT_CMD, cmd);
    
    struct sata_cmd_fis fis;
    memset(&fis, 0, sizeof(fis));
    fis.command = ATA_CMD_IDENTIFY_PACKET_DEVICE;
    
    struct ahci_cmd* ahci_cmd = (struct ahci_cmd*)kmalloc_aligned(256, 256);
    struct ahci_list* ahci_list = (struct ahci_list*)kmalloc_aligned(1024, 1024);
    struct ahci_fis* ahci_fis = (struct ahci_fis*)kmalloc_aligned(256, 256);
    
    if (!ahci_cmd || !ahci_list || !ahci_fis) {
        if (ahci_cmd) kfree(ahci_cmd);
        if (ahci_list) kfree(ahci_list);
        if (ahci_fis) kfree(ahci_fis);
        serial_puts("[AHCI]   Memory allocation failed\n");
        return -1;
    }
    
    memset(ahci_cmd, 0, 256);
    memset(ahci_list, 0, 1024);
    memset(ahci_fis, 0, 256);
    
    port->cmd = ahci_cmd;
    port->list = ahci_list;
    port->fis = ahci_fis;
    
    ahci_port_writel(pnr, PORT_LST_ADDR, (uint32_t)(uintptr_t)ahci_list);
    ahci_port_writel(pnr, PORT_FIS_ADDR, (uint32_t)(uintptr_t)ahci_fis);
    
    memcpy(&ahci_cmd->fis, &fis, sizeof(fis));
    ahci_cmd->prdt[0].base = (uint32_t)(uintptr_t)buffer;
    ahci_cmd->prdt[0].flags = sizeof(buffer) - 1;
    
    ahci_list[0].flags = (1 << 16) | (5 << 0);
    ahci_list[0].base = (uint32_t)(uintptr_t)ahci_cmd;
    
    int rc = ahci_command(port, 0, 1, buffer, sizeof(buffer), 0);
    
    if (rc != 0) {
        fis.command = ATA_CMD_IDENTIFY_DEVICE;
        memcpy(&ahci_cmd->fis, &fis, sizeof(fis));
        rc = ahci_command(port, 0, 0, buffer, sizeof(buffer), 0);
        if (rc != 0) {
            ahci_free_port_resources(port);
            serial_puts("[AHCI]   Identify failed\n");
            return -1;
        }
        port->atapi = 0;
    } else {
        port->atapi = 1;
    }
    
    port->present = 1;
    port->sector_size = port->atapi ? CDROM_SECTOR_SIZE : DISK_SECTOR_SIZE;
    
    memcpy(port->model, (uint8_t*)&buffer[27], 40);
    port->model[40] = '\0';
    ata_fix_string((uint8_t*)port->model, 40);
    
    memcpy(port->serial, (uint8_t*)&buffer[10], 20);
    port->serial[20] = '\0';
    ata_fix_string((uint8_t*)port->serial, 20);
    
    memcpy(port->firmware, (uint8_t*)&buffer[23], 8);
    port->firmware[8] = '\0';
    ata_fix_string((uint8_t*)port->firmware, 8);
    
    if (!port->atapi) {
        if (buffer[83] & (1 << 10)) {
            port->lba48_supported = 1;
            port->sectors = *(uint64_t*)&buffer[100];
        } else {
            port->lba48_supported = 0;
            port->sectors = *(uint32_t*)&buffer[60];
        }
        serial_puts("[AHCI]   Found disk: ");
        serial_puts(port->model);
        serial_puts(" (");
        serial_puts_num_ulong(port->sectors / 2 / 1024);
        serial_puts(" MB)\n");
    } else {
        port->sectors = 0;
        serial_puts("[AHCI]   Found ATAPI: ");
        serial_puts(port->model);
        serial_puts("\n");
    }
    
    return 0;
}

static void ahci_cleanup_ports(void) {
    for (int i = 0; i < ahci_port_count; i++) {
        ahci_free_port_resources(&ahci_ports[i]);
        memset(&ahci_ports[i], 0, sizeof(struct ahci_port));
    }
    ahci_port_count = 0;
}

static void ahci_scan_pci(void) {
    for (int bus = 0; bus < 256; bus++) {
        for (int dev = 0; dev < 32; dev++) {
            for (int func = 0; func < 8; func++) {
                uint16_t vendor = pci_read16(bus, dev, func, 0);
                if (vendor == 0xFFFF) continue;
                
                uint8_t class = pci_read8(bus, dev, func, 0x0B);
                uint8_t subclass = pci_read8(bus, dev, func, 0x0A);
                uint8_t prog_if = pci_read8(bus, dev, func, 0x09);
                
                if (class == 0x01 && subclass == 0x06 && prog_if == 0x01) {
                    serial_puts("[AHCI] Found AHCI controller at ");
                    serial_puts_num(bus);
                    serial_puts(":");
                    serial_puts_num(dev);
                    serial_puts(".");
                    serial_puts_num(func);
                    serial_puts("\n");
                    
                    pci_enable_bus_master(bus, dev, func);
                    pci_enable_memory_space(bus, dev, func);
                    
                    ahci_iobase = pci_read32(bus, dev, func, 0x24) & 0xFFFFFFF0;
                    
                    serial_puts("[AHCI] MMIO base: 0x");
                    serial_puts_num_hex(ahci_iobase);
                    serial_puts("\n");
                    
                    ahci_writel(HOST_CTL, ahci_readl(HOST_CTL) | HOST_CTL_AHCI_EN);
                    
                    ahci_caps = ahci_readl(HOST_CAP);
                    ahci_ports_impl = ahci_readl(HOST_PORTS_IMPL);
                    
                    uint32_t max_ports = ahci_caps & 0x1F;
                    serial_puts("[AHCI] CAPS=");
                    serial_puts_num_hex(ahci_caps);
                    serial_puts(" PORTS_IMPL=0x");
                    serial_puts_num_hex(ahci_ports_impl);
                    serial_puts("\n");
                    
                    int ports_found = 0;
                    
                    for (uint32_t pnr = 0; pnr <= max_ports && ahci_port_count < 32; pnr++) {
                        if (!(ahci_ports_impl & (1 << pnr))) {
                            serial_puts("[AHCI] Port ");
                            serial_puts_num(pnr);
                            serial_puts(": not implemented\n");
                            continue;
                        }
                        
                        uint32_t ssts = ahci_port_readl(pnr, PORT_SCR_STAT);
                        uint32_t sstatus = ssts & 0x0F;
                        
                        serial_puts("[AHCI] Port ");
                        serial_puts_num(pnr);
                        serial_puts(": SStatus=");
                        serial_puts_num_hex(sstatus);
                        serial_puts(" (");
                        
                        switch(sstatus) {
                            case 0x00: serial_puts("no device"); break;
                            case 0x01: serial_puts("device present, not ready"); break;
                            case 0x03: serial_puts("device present, ready"); break;
                            default: serial_puts("unknown");
                        }
                        serial_puts(")\n");
                        
                        if (sstatus != 0x03) {
                            serial_puts("[AHCI] Port ");
                            serial_puts_num(pnr);
                            serial_puts(": no device, skipping\n");
                            continue;
                        }
                        
                        serial_puts("[AHCI] Port ");
                        serial_puts_num(pnr);
                        serial_puts(": device present, initializing...\n");
                        
                        ahci_port_reset(pnr);
                        if (ahci_port_setup(pnr) == 0) {
                            ahci_port_count++;
                            ports_found++;
                            serial_puts("[AHCI] Port ");
                            serial_puts_num(pnr);
                            serial_puts(": initialized successfully\n");
                        } else {
                            serial_puts("[AHCI] Port ");
                            serial_puts_num(pnr);
                            serial_puts(": initialization failed\n");
                        }
                    }
                    
                    if (ports_found == 0) {
                        serial_puts("[AHCI] No working ports found, cleaning up\n");
                        ahci_iobase = 0;
                    }
                    
                    return;
                }
            }
        }
    }
}

static int ahci_check_pci_present(void) {
    for (int bus = 0; bus < 256; bus++) {
        for (int dev = 0; dev < 32; dev++) {
            for (int func = 0; func < 8; func++) {
                uint16_t vendor = pci_read16(bus, dev, func, 0);
                if (vendor == 0xFFFF) continue;
                
                uint8_t class = pci_read8(bus, dev, func, 0x0B);
                uint8_t subclass = pci_read8(bus, dev, func, 0x0A);
                uint8_t prog_if = pci_read8(bus, dev, func, 0x09);
                
                if (class == 0x01 && subclass == 0x06 && prog_if == 0x01) {
                    return 1;
                }
            }
        }
    }
    return 0;
}

void ahci_init(void) {
    if (ahci_initialized) return;
    
    serial_puts("[AHCI] Initializing...\n");
    
    if (!ahci_check_pci_present()) {
        serial_puts("[AHCI] No AHCI controller found, skipping\n");
        ahci_initialized = 1;
        return;
    }
    
    ahci_port_count = 0;
    memset(ahci_ports, 0, sizeof(ahci_ports));
    
    ahci_scan_pci();
    
    ahci_initialized = 1;
    if (ahci_port_count > 0) {
        serial_puts("[AHCI] Initialization complete, found ");
        serial_puts_num(ahci_port_count);
        serial_puts(" ports\n");
    } else {
        serial_puts("[AHCI] No AHCI devices found\n");
        ahci_cleanup_ports();
    }
}

int ahci_read_sectors(int port, uint64_t lba, uint32_t count, void* buffer) {
    if (port < 0 || port >= ahci_port_count) return -1;
    struct ahci_port* p = &ahci_ports[port];
    if (!p->present || p->atapi) return -1;
    if (count == 0) return 0;
    
    struct sata_cmd_fis fis;
    sata_prep_readwrite(&fis, lba, count, 0);
    memcpy(&p->cmd->fis, &fis, sizeof(fis));
    
    p->cmd->prdt[0].base = (uint32_t)(uintptr_t)buffer;
    p->cmd->prdt[0].flags = count * p->sector_size - 1;
    
    p->list[0].flags = (1 << 16) | (5 << 0);
    p->list[0].base = (uint32_t)(uintptr_t)p->cmd;
    
    return ahci_command(p, 0, 0, buffer, count * p->sector_size, 0);
}

int ahci_write_sectors(int port, uint64_t lba, uint32_t count, void* buffer) {
    if (port < 0 || port >= ahci_port_count) return -1;
    struct ahci_port* p = &ahci_ports[port];
    if (!p->present || p->atapi) return -1;
    if (count == 0) return 0;
    
    struct sata_cmd_fis fis;
    sata_prep_readwrite(&fis, lba, count, 1);
    memcpy(&p->cmd->fis, &fis, sizeof(fis));
    
    p->cmd->prdt[0].base = (uint32_t)(uintptr_t)buffer;
    p->cmd->prdt[0].flags = count * p->sector_size - 1;
    
    p->list[0].flags = (1 << 16) | (5 << 0);
    p->list[0].base = (uint32_t)(uintptr_t)p->cmd;
    
    return ahci_command(p, 1, 0, buffer, count * p->sector_size, 0);
}

int ahci_flush_cache(int port) {
    if (port < 0 || port >= ahci_port_count) return -1;
    struct ahci_port* p = &ahci_ports[port];
    if (!p->present || p->atapi) return -1;
    return ahci_flush_command(p);
}

int ahci_get_port_count(void) {
    return ahci_port_count;
}

void ahci_dump_info(void) {
    serial_puts("\n=== AHCI DEVICES ===\n");
    for (int i = 0; i < ahci_port_count; i++) {
        struct ahci_port* p = &ahci_ports[i];
        serial_puts("Port ");
        serial_puts_num(i);
        serial_puts(": ");
        serial_puts(p->model);
        serial_puts("\n  Type: ");
        serial_puts(p->atapi ? "ATAPI" : "ATA");
        if (!p->atapi) {
            serial_puts("\n  Size: ");
            serial_puts_num_ulong(p->sectors);
            serial_puts(" sectors (");
            serial_puts_num_ulong(p->sectors / 2 / 1024);
            serial_puts(" MB)");
        }
        serial_puts("\n  Serial: ");
        serial_puts(p->serial);
        serial_puts("\n  Firmware: ");
        serial_puts(p->firmware);
        if (p->lba48_supported) serial_puts("\n  LBA48: Yes");
        serial_puts("\n");
    }
    serial_puts("==================\n");
}

struct ahci_port* ahci_get_port(int index) {
    if (index >= ahci_port_count) return NULL;
    return &ahci_ports[index];
}