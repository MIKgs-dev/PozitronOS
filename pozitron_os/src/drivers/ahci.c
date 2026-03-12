#include "drivers/ahci.h"
#include "kernel/memory.h"
#include "kernel/ports.h"
#include "drivers/pci.h"
#include "drivers/serial.h"
#include "drivers/pic.h"
#include "core/isr.h"
#include "lib/string.h"

#define AHCI_MAX_PORTS       32
#define AHCI_MAX_SLOTS       32
#define AHCI_SECTOR_SIZE     512
#define AHCI_MAX_CMD_SLOTS    32

#define RX_FIS_D2H_REG       0x40
#define RX_FIS_PIO_SETUP     0x58
#define RX_FIS_DMA_ACTIVATE  0x60
#define RX_FIS_SDB           0xA0
#define RX_FIS_UNKNOWN       0xC0

#define ATA_CMD_IDENTIFY             0xEC
#define ATA_CMD_READ_DMA              0xC8
#define ATA_CMD_WRITE_DMA             0xCA
#define ATA_CMD_READ_DMA_EXT          0x25
#define ATA_CMD_WRITE_DMA_EXT         0x35
#define ATA_CMD_READ_FPDMA_QUEUED     0x60
#define ATA_CMD_WRITE_FPDMA_QUEUED    0x61
#define ATA_CMD_FLUSH_CACHE           0xE7
#define ATA_CMD_FLUSH_CACHE_EXT       0xEA
#define ATA_CMD_STANDBY_IMMEDIATE     0xE0
#define ATA_CMD_IDLE_IMMEDIATE        0xE1
#define ATA_CMD_CHECK_POWER           0xE5
#define ATA_CMD_SLEEP                  0xE6

#define ATA_FLAG_LBA                   0x40
#define ATA_FLAG_LBA48                 0x80

typedef struct {
    uint16_t    flags;
    uint16_t    prd_length;
    uint32_t    bytecount;
    uint64_t    ctba;
} __attribute__((packed)) ahci_cmd_list_t;

#define AHCI_CMD_WRITE      0x40
#define AHCI_CMD_ATAPI      0x20
#define AHCI_CMD_PREFETCH   0x10
#define AHCI_CMD_RESET      0x08
#define AHCI_CMD_BIST       0x04
#define AHCI_CMD_CLEAR      0x02

typedef struct {
    uint64_t    dba;
    uint32_t    reserved;
    uint32_t    dbc;
} __attribute__((packed)) ahci_prd_t;

#define AHCI_PRD_INT        0x80000000

typedef struct {
    uint8_t     cfis[64];
    uint8_t     acmd[32];
    uint8_t     reserved[32];
    ahci_prd_t  prd[];
} __attribute__((packed)) ahci_cmd_table_t;

typedef struct {
    uint8_t     type;
    uint8_t     flags;
    uint8_t     command;
    uint8_t     features_low;
    uint8_t     lba_low;
    uint8_t     lba_mid;
    uint8_t     lba_high;
    uint8_t     device;
    uint8_t     lba_low_exp;
    uint8_t     lba_mid_exp;
    uint8_t     lba_high_exp;
    uint8_t     features_high;
    uint8_t     count_low;
    uint8_t     count_high;
    uint8_t     reserved[4];
} __attribute__((packed)) fis_h2d_t;

#define FIS_TYPE_H2D        0x27

typedef struct {
    uint8_t     type;
    uint8_t     flags;
    uint8_t     command;
    uint8_t     features_low;
    uint8_t     lba_low;
    uint8_t     lba_mid;
    uint8_t     lba_high;
    uint8_t     device;
    uint8_t     lba_low_exp;
    uint8_t     lba_mid_exp;
    uint8_t     lba_high_exp;
    uint8_t     features_high;
    uint8_t     count_low;
    uint8_t     count_high;
    uint8_t     tag : 5;
    uint8_t     reserved2 : 3;
    uint8_t     reserved[3];
} __attribute__((packed)) fis_ncq_t;

typedef struct {
    uint8_t     type;
    uint8_t     interrupt;
    uint8_t     status;
    uint8_t     error;
    uint8_t     lba_low;
    uint8_t     lba_mid;
    uint8_t     lba_high;
    uint8_t     device;
    uint8_t     lba_low_exp;
    uint8_t     lba_mid_exp;
    uint8_t     lba_high_exp;
    uint8_t     reserved;
    uint8_t     count_low;
    uint8_t     count_high;
    uint8_t     reserved2[2];
} __attribute__((packed)) fis_d2h_t;

#define FIS_TYPE_D2H        0x34

typedef struct {
    uint8_t     type;
    uint8_t     flags;
    uint8_t     tag;
    uint8_t     reserved1;
    uint32_t    data[5];
} __attribute__((packed)) fis_sdb_t;

#define FIS_TYPE_SDB        0xA1

typedef struct {
    uint64_t    sectors;
    uint32_t    max_slots;
    uint8_t     ncq;
    uint8_t     pmp;
    uint8_t     sata_rev;
    uint8_t     present;
    uint8_t     atapi;
    uint8_t     offline;
    uint32_t    intr_mask;
    volatile uint32_t* mmio;
} ahci_port_t;

typedef struct {
    void        *clb_virt;
    uint32_t    clb_phys;
    void        *fb_virt;
    uint32_t    fb_phys;
    void        *ct_virt[AHCI_MAX_SLOTS];
    uint32_t    ct_phys[AHCI_MAX_SLOTS];
    uint32_t    slot_active[AHCI_MAX_SLOTS];
    uint32_t    slot_ncq[AHCI_MAX_SLOTS];
    uint32_t    slot_tag[AHCI_MAX_SLOTS];
    uint8_t     running[AHCI_MAX_SLOTS];
} ahci_port_priv_t;

static ahci_port_t      ahci_ports[AHCI_MAX_PORTS];
static ahci_port_priv_t port_priv[AHCI_MAX_PORTS];
static uint32_t         ahci_base;
static uint32_t         ahci_cap;
static uint32_t         ahci_cap2;
static uint32_t         ahci_pi;
static uint8_t          ahci_irq;
int                     ahci_port_count;

#define ahci_read(reg)          (*(volatile uint32_t*)(ahci_base + (reg)))
#define ahci_write(reg, val)    (*(volatile uint32_t*)(ahci_base + (reg)) = (val))
#define port_read(p, reg)       (*(volatile uint32_t*)(ahci_base + 0x100 + (p)*0x80 + (reg)))
#define port_write(p, reg, val) (*(volatile uint32_t*)(ahci_base + 0x100 + (p)*0x80 + (reg)) = (val))

static inline void serial_puts_hex32(uint32_t val) {
    char hex[] = "0123456789ABCDEF";
    serial_write(hex[(val >> 28) & 0xF]);
    serial_write(hex[(val >> 24) & 0xF]);
    serial_write(hex[(val >> 20) & 0xF]);
    serial_write(hex[(val >> 16) & 0xF]);
    serial_write(hex[(val >> 12) & 0xF]);
    serial_write(hex[(val >> 8) & 0xF]);
    serial_write(hex[(val >> 4) & 0xF]);
    serial_write(hex[val & 0xF]);
}

static inline void serial_puts_hex64(uint64_t val) {
    serial_puts_hex32(val >> 32);
    serial_puts_hex32(val & 0xFFFFFFFF);
}

static void ahci_delay(int ms) {
    for (volatile int i = 0; i < ms * 100000; i++) {
        asm volatile("nop");
    }
}

static int ahci_find_controller(void) {
    pci_device_t dev;
    dev = pci_find_class(0x01, 0x06, 0x01);
    if (dev.bus == 0xFF) return -1;
    
    serial_puts("[AHCI] Found at ");
    serial_puts_num(dev.bus); serial_puts(":");
    serial_puts_num(dev.device); serial_puts(".");
    serial_puts_num(dev.func); serial_puts("\n");
    
    pci_enable_bus_master(dev.bus, dev.device, dev.func);
    pci_enable_memory_space(dev.bus, dev.device, dev.func);
    
    uint32_t bar5 = pci_read32(dev.bus, dev.device, dev.func, 0x24) & ~0xF;
    if (!bar5) return -1;
    
    ahci_base = bar5;
    ahci_irq = pci_read8(dev.bus, dev.device, dev.func, 0x3C);
    
    serial_puts("[AHCI] MMIO: 0x"); serial_puts_hex32(bar5); serial_puts("\n");
    serial_puts("[AHCI] IRQ: "); serial_puts_num(ahci_irq); serial_puts("\n");
    return 0;
}

static int ahci_reset(void) {
    ahci_cap = ahci_read(0x00);
    ahci_cap2 = ahci_read(0x24);
    
    serial_puts("[AHCI] CAP: 0x"); serial_puts_hex32(ahci_cap); serial_puts("\n");
    serial_puts("[AHCI] CAP2:0x"); serial_puts_hex32(ahci_cap2); serial_puts("\n");
    
    ahci_write(0x04, ahci_read(0x04) | (1 << 31));
    ahci_write(0x04, ahci_read(0x04) | 1);
    
    int timeout = 1000;
    while (timeout--) {
        ahci_delay(1);
        if (!(ahci_read(0x04) & 1)) break;
    }
    
    if (!timeout) {
        serial_puts("[AHCI] Reset timeout\n");
        return -1;
    }
    
    ahci_write(0x04, ahci_read(0x04) | (1 << 31));
    return 0;
}

static int ahci_port_stop(int port) {
    uint32_t cmd = port_read(port, 0x18);
    if (cmd & 1) {
        cmd &= ~1;
        port_write(port, 0x18, cmd);
    }
    
    int timeout = 500;
    while (timeout--) {
        ahci_delay(1);
        if (!(port_read(port, 0x18) & 0x8000)) break;
    }
    if (!timeout) return -1;
    
    cmd = port_read(port, 0x18);
    if (cmd & 0x10) {
        cmd &= ~0x10;
        port_write(port, 0x18, cmd);
    }
    
    timeout = 500;
    while (timeout--) {
        ahci_delay(1);
        if (!(port_read(port, 0x18) & 0x4000)) break;
    }
    if (!timeout) return -1;
    
    return 0;
}

static void ahci_port_start(int port) {
    uint32_t cmd = port_read(port, 0x18);
    cmd |= 0x11;
    port_write(port, 0x18, cmd);
}

static int ahci_identify_device(int port) {
    if (!ahci_ports[port].present) return -1;
    
    void *buffer = kmalloc_dma(512);
    if (!buffer) return -1;
    memset(buffer, 0, 512);
    
    int slot = 0;
    while (port_priv[port].slot_active[slot] && slot < AHCI_MAX_SLOTS) slot++;
    if (slot >= AHCI_MAX_SLOTS) {
        kfree_dma(buffer);
        return -1;
    }
    
    port_priv[port].slot_active[slot] = 1;
    
    ahci_cmd_list_t *cl = (ahci_cmd_list_t*)port_priv[port].clb_virt + slot;
    ahci_cmd_table_t *ct = (ahci_cmd_table_t*)port_priv[port].ct_virt[slot];
    fis_h2d_t *fis = (fis_h2d_t*)ct->cfis;
    
    memset(cl, 0, sizeof(ahci_cmd_list_t));
    memset(ct, 0, sizeof(ahci_cmd_table_t));
    
    fis->type = FIS_TYPE_H2D;
    fis->flags = 0x80;
    fis->command = ATA_CMD_IDENTIFY;
    
    ct->prd[0].dba = get_phys_addr(buffer);
    ct->prd[0].dbc = 511 | AHCI_PRD_INT;
    
    cl->prd_length = 1;
    cl->ctba = port_priv[port].ct_phys[slot];
    
    int timeout = 1000;
    while (port_read(port, 0x20) & (0x80 | 0x08)) {
        ahci_delay(1);
        if (!--timeout) {
            port_priv[port].slot_active[slot] = 0;
            kfree_dma(buffer);
            return -1;
        }
    }
    
    port_write(port, 0x38, 1 << slot);
    
    timeout = 5000;
    while (timeout--) {
        ahci_delay(1);
        if (!(port_read(port, 0x38) & (1 << slot))) break;
    }
    
    if (!timeout) {
        port_priv[port].slot_active[slot] = 0;
        kfree_dma(buffer);
        return -1;
    }
    
    uint32_t tfd = port_read(port, 0x20);
    if (tfd & 1) {
        port_priv[port].slot_active[slot] = 0;
        kfree_dma(buffer);
        return -1;
    }
    
    uint16_t *id = (uint16_t*)buffer;
    
    if (id[83] & (1 << 10)) {
        ahci_ports[port].sectors = ((uint64_t)id[100] << 0) |
                                    ((uint64_t)id[101] << 16) |
                                    ((uint64_t)id[102] << 32) |
                                    ((uint64_t)id[103] << 48);
    } else {
        ahci_ports[port].sectors = (uint64_t)id[60] | ((uint64_t)id[61] << 16);
    }
    
    if (id[76] & (1 << 8)) {
        ahci_ports[port].ncq = 1;
    }
    
    if (id[75] & (1 << 3)) {
        ahci_ports[port].pmp = 1;
    }
    
    uint32_t ssts = port_read(port, 0x28);
    ahci_ports[port].sata_rev = (ssts >> 4) & 0xF;
    
    kfree_dma(buffer);
    port_priv[port].slot_active[slot] = 0;
    return 0;
}

static int ahci_port_init(int port) {
    port_write(port, 0x2C, 0x301);
    ahci_delay(10);
    port_write(port, 0x2C, 0x300);
    
    int timeout = 1000;
    while (timeout--) {
        ahci_delay(1);
        uint32_t ssts = port_read(port, 0x28);
        if ((ssts & 0x0F) == 0x03) break;
    }
    
    if ((port_read(port, 0x28) & 0x0F) != 0x03) {
        return 0;
    }
    
    uint32_t sig = port_read(port, 0x24);
    
    ahci_ports[port].present = 1;
    ahci_ports[port].mmio = (volatile uint32_t*)(ahci_base + 0x100 + port * 0x80);
    
    switch (sig) {
        case 0x00000101:
            ahci_ports[port].atapi = 0;
            serial_puts("[AHCI] Port "); serial_puts_num(port); serial_puts(": ATA\n");
            break;
        case 0xEB140101:
            ahci_ports[port].atapi = 1;
            serial_puts("[AHCI] Port "); serial_puts_num(port); serial_puts(": ATAPI\n");
            break;
        default:
            serial_puts("[AHCI] Port "); serial_puts_num(port); serial_puts(": Unknown\n");
            return 0;
    }
    
    uint32_t max_slots = (ahci_cap >> 8) & 0x1F;
    ahci_ports[port].max_slots = max_slots + 1;
    
    uint32_t clb_size = 1024 * AHCI_MAX_SLOTS;
    port_priv[port].clb_virt = kmalloc_aligned(clb_size, 1024);
    if (!port_priv[port].clb_virt) return -1;
    memset(port_priv[port].clb_virt, 0, clb_size);
    port_priv[port].clb_phys = get_phys_addr(port_priv[port].clb_virt);
    
    port_priv[port].fb_virt = kmalloc_aligned(4096, 256);
    if (!port_priv[port].fb_virt) {
        kfree_aligned(port_priv[port].clb_virt);
        return -1;
    }
    memset(port_priv[port].fb_virt, 0, 4096);
    port_priv[port].fb_phys = get_phys_addr(port_priv[port].fb_virt);
    
    for (int i = 0; i < AHCI_MAX_SLOTS; i++) {
        port_priv[port].ct_virt[i] = kmalloc_aligned(256, 128);
        if (!port_priv[port].ct_virt[i]) {
            for (int j = 0; j < i; j++) {
                kfree_aligned(port_priv[port].ct_virt[j]);
            }
            kfree_aligned(port_priv[port].fb_virt);
            kfree_aligned(port_priv[port].clb_virt);
            return -1;
        }
        memset(port_priv[port].ct_virt[i], 0, 256);
        port_priv[port].ct_phys[i] = get_phys_addr(port_priv[port].ct_virt[i]);
        port_priv[port].slot_active[i] = 0;
    }
    
    if (ahci_port_stop(port) != 0) {
        return -1;
    }
    
    port_write(port, 0x00, port_priv[port].clb_phys);
    port_write(port, 0x04, 0);
    port_write(port, 0x08, port_priv[port].fb_phys);
    port_write(port, 0x0C, 0);
    
    port_write(port, 0x14, 0xFFFFFFFF);
    ahci_ports[port].intr_mask = 0xFFFFFFFF;
    
    ahci_port_start(port);
    
    timeout = 1000;
    while (timeout--) {
        uint32_t tfd = port_read(port, 0x20);
        if (!(tfd & (0x80 | 0x08))) break;
        ahci_delay(1);
    }
    
    ahci_identify_device(port);
    
    ahci_port_count++;
    return 1;
}

static int ahci_exec_cmd(int port, uint8_t cmd, uint64_t lba, 
                          uint32_t count, void *buffer, int write, int ncq) {
    if (!ahci_ports[port].present || ahci_ports[port].atapi) return -1;
    if (count == 0) return 0;
    if (count > 65535) count = 65535;
    
    int slot = 0;
    while (port_priv[port].slot_active[slot] && slot < AHCI_MAX_SLOTS) slot++;
    if (slot >= AHCI_MAX_SLOTS) return -1;
    
    port_priv[port].slot_active[slot] = 1;
    
    void *aligned = NULL;
    uint32_t phys = 0;
    
    if (buffer) {
        if (((uint32_t)buffer) & 0x1FF) {
            aligned = kmalloc_dma(count * 512);
            if (!aligned) {
                port_priv[port].slot_active[slot] = 0;
                return -1;
            }
            if (write) memcpy(aligned, buffer, count * 512);
            phys = get_phys_addr(aligned);
        } else {
            phys = get_phys_addr(buffer);
        }
    }
    
    ahci_cmd_list_t *cl = (ahci_cmd_list_t*)port_priv[port].clb_virt + slot;
    ahci_cmd_table_t *ct = (ahci_cmd_table_t*)port_priv[port].ct_virt[slot];
    
    memset(cl, 0, sizeof(ahci_cmd_list_t));
    memset(ct, 0, sizeof(ahci_cmd_table_t));
    
    if (ncq) {
        fis_ncq_t *fis_ncq = (fis_ncq_t*)ct->cfis;
        fis_ncq->type = FIS_TYPE_H2D;
        fis_ncq->flags = 0x80;
        fis_ncq->command = cmd;
        fis_ncq->device = 0x40;
        fis_ncq->tag = slot & 0x1F;
        
        fis_ncq->lba_low = lba & 0xFF;
        fis_ncq->lba_mid = (lba >> 8) & 0xFF;
        fis_ncq->lba_high = (lba >> 16) & 0xFF;
        fis_ncq->lba_low_exp = (lba >> 24) & 0xFF;
        fis_ncq->lba_mid_exp = (lba >> 32) & 0xFF;
        fis_ncq->lba_high_exp = (lba >> 40) & 0xFF;
        fis_ncq->count_low = count & 0xFF;
        fis_ncq->count_high = (count >> 8) & 0xFF;
        
        port_priv[port].slot_ncq[slot] = 1;
        port_priv[port].slot_tag[slot] = slot & 0x1F;
    } else {
        fis_h2d_t *fis = (fis_h2d_t*)ct->cfis;
        fis->type = FIS_TYPE_H2D;
        fis->flags = 0x80;
        fis->command = cmd;
        fis->device = 0x40;
        
        if (cmd == ATA_CMD_READ_DMA_EXT || cmd == ATA_CMD_WRITE_DMA_EXT) {
            fis->lba_low = lba & 0xFF;
            fis->lba_mid = (lba >> 8) & 0xFF;
            fis->lba_high = (lba >> 16) & 0xFF;
            fis->lba_low_exp = (lba >> 24) & 0xFF;
            fis->lba_mid_exp = (lba >> 32) & 0xFF;
            fis->lba_high_exp = (lba >> 40) & 0xFF;
            fis->count_low = count & 0xFF;
            fis->count_high = (count >> 8) & 0xFF;
        } else {
            fis->lba_low = lba & 0xFF;
            fis->lba_mid = (lba >> 8) & 0xFF;
            fis->lba_high = (lba >> 16) & 0xFF;
            fis->device |= ((lba >> 24) & 0x0F);
            fis->count_low = count & 0xFF;
            fis->count_high = 0;
        }
        port_priv[port].slot_ncq[slot] = 0;
    }
    
    if (phys) {
        ct->prd[0].dba = phys;
        ct->prd[0].dbc = (count * 512 - 1) | AHCI_PRD_INT;
        cl->prd_length = 1;
    }
    
    cl->flags = (write ? AHCI_CMD_WRITE : 0);
    cl->ctba = port_priv[port].ct_phys[slot];
    
    int timeout = 1000;
    while (port_read(port, 0x20) & (0x80 | 0x08)) {
        ahci_delay(1);
        if (!--timeout) {
            if (aligned) kfree_dma(aligned);
            port_priv[port].slot_active[slot] = 0;
            return -1;
        }
    }
    
    if (ncq) {
        port_write(port, 0x34, 1 << slot);
    }
    port_write(port, 0x38, 1 << slot);
    
    timeout = 5000;
    while (timeout--) {
        ahci_delay(1);
        if (!(port_read(port, 0x38) & (1 << slot))) break;
        if (ncq && !(port_read(port, 0x34) & (1 << slot))) break;
    }
    
    if (!timeout) {
        if (aligned) kfree_dma(aligned);
        port_priv[port].slot_active[slot] = 0;
        return -1;
    }
    
    uint32_t tfd = port_read(port, 0x20);
    if (tfd & 1) {
        uint32_t serr = port_read(port, 0x30);
        if (serr) port_write(port, 0x30, serr);
        if (aligned) kfree_dma(aligned);
        port_priv[port].slot_active[slot] = 0;
        return -1;
    }
    
    if (aligned && !write) {
        memcpy(buffer, aligned, count * 512);
    }
    
    if (aligned) kfree_dma(aligned);
    port_priv[port].slot_active[slot] = 0;
    return count * 512;
}

void ahci_irq_handler(registers_t *r) {
    (void)r;
    
    uint32_t is = ahci_read(0x08);
    if (!is) return;
    
    for (int port = 0; port < AHCI_MAX_PORTS; port++) {
        if (is & (1 << port) && ahci_ports[port].present) {
            uint32_t pis = port_read(port, 0x10);
            if (pis) {
                if (pis & 0x04) {
                }
                if (pis & 0x40) {
                }
                if (pis & 0x02) {
                }
                port_write(port, 0x10, pis);
            }
        }
    }
    
    ahci_write(0x08, is);
    pic_send_eoi(ahci_irq);
}

int ahci_init(void) {
    serial_puts("[AHCI] Starting...\n");
    
    memset(ahci_ports, 0, sizeof(ahci_ports));
    memset(port_priv, 0, sizeof(port_priv));
    ahci_port_count = 0;
    
    if (ahci_find_controller() != 0) {
        serial_puts("[AHCI] No controller found\n");
        return -1;
    }
    
    if (ahci_reset() != 0) return -1;
    
    ahci_pi = ahci_read(0x0C);
    serial_puts("[AHCI] PI: 0x"); serial_puts_hex32(ahci_pi); serial_puts("\n");
    
    ahci_write(0x04, ahci_read(0x04) | 2);
    
    irq_install_handler(ahci_irq, ahci_irq_handler);
    
    for (int i = 0; i < AHCI_MAX_PORTS; i++) {
        if (ahci_pi & (1 << i)) {
            ahci_port_init(i);
        }
    }
    
    serial_puts("[AHCI] Done, "); serial_puts_num(ahci_port_count); serial_puts(" devices\n");
    return 0;
}

int ahci_read_sectors(int port, uint64_t lba, uint32_t count, void *buffer) {
    if (count > 1 && ahci_ports[port].ncq && 
        (ahci_cap & (1 << 30))) {
        return ahci_exec_cmd(port, ATA_CMD_READ_FPDMA_QUEUED, lba, count, buffer, 0, 1);
    } else if (lba < 0xFFFFFFF && count <= 256) {
        return ahci_exec_cmd(port, ATA_CMD_READ_DMA, lba, count, buffer, 0, 0);
    } else {
        return ahci_exec_cmd(port, ATA_CMD_READ_DMA_EXT, lba, count, buffer, 0, 0);
    }
}

int ahci_write_sectors(int port, uint64_t lba, uint32_t count, void *buffer) {
    if (count > 1 && ahci_ports[port].ncq &&
        (ahci_cap & (1 << 30))) {
        return ahci_exec_cmd(port, ATA_CMD_WRITE_FPDMA_QUEUED, lba, count, buffer, 1, 1);
    } else if (lba < 0xFFFFFFF && count <= 256) {
        return ahci_exec_cmd(port, ATA_CMD_WRITE_DMA, lba, count, buffer, 1, 0);
    } else {
        return ahci_exec_cmd(port, ATA_CMD_WRITE_DMA_EXT, lba, count, buffer, 1, 0);
    }
}

int ahci_flush_cache(int port) {
    if (ahci_ports[port].sectors > 0xFFFFFFF) {
        return ahci_exec_cmd(port, ATA_CMD_FLUSH_CACHE_EXT, 0, 0, NULL, 0, 0);
    } else {
        return ahci_exec_cmd(port, ATA_CMD_FLUSH_CACHE, 0, 0, NULL, 0, 0);
    }
}

void ahci_probe_ports(void) {
    serial_puts("\n=== AHCI DEVICES ===\n");
    for (int i = 0; i < AHCI_MAX_PORTS; i++) {
        if (ahci_ports[i].present) {
            serial_puts("Port "); serial_puts_num(i);
            serial_puts(": "); serial_puts(ahci_ports[i].atapi ? "ATAPI" : "ATA");
            serial_puts(" - "); serial_puts_hex64(ahci_ports[i].sectors);
            serial_puts(" sectors, SATA Gen");
            serial_puts_num(ahci_ports[i].sata_rev);
            if (ahci_ports[i].ncq) serial_puts(" NCQ");
            if (ahci_ports[i].pmp) serial_puts(" PMP");
            serial_puts("\n");
        }
    }
    serial_puts("====================\n");
}