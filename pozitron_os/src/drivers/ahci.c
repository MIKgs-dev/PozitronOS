#include "drivers/ahci.h"
#include "drivers/pci.h"
#include "drivers/serial.h"
#include "drivers/disk.h"      // для регистрации дисков
#include "kernel/memory.h"
#include "lib/string.h"
#include "drivers/timer.h"

// ============================================================================
// ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ
// ============================================================================
ahci_controller_t* ahci_ctrl = NULL;

// ============================================================================
// РЕГИСТРОВЫЙ ДОСТУП
// ============================================================================
static inline uint32_t ahci_read(ahci_controller_t* ctrl, uint32_t reg) {
    return *(volatile uint32_t*)(ctrl->base + reg);
}

static inline void ahci_write(ahci_controller_t* ctrl, uint32_t reg, uint32_t val) {
    *(volatile uint32_t*)(ctrl->base + reg) = val;
}

uint32_t ahci_port_read(ahci_controller_t* ctrl, int port, uint32_t reg) {
    return *(volatile uint32_t*)(ctrl->base + 0x100 + (port * 0x80) + reg);
}

void ahci_port_write(ahci_controller_t* ctrl, int port, uint32_t reg, uint32_t val) {
    *(volatile uint32_t*)(ctrl->base + 0x100 + (port * 0x80) + reg) = val;
}

// ============================================================================
// ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ
// ============================================================================
static void ahci_delay(uint32_t us) {
    for (volatile uint32_t i = 0; i < us * 20; i++);
}

static int ahci_wait_clear(ahci_controller_t* ctrl, int port, uint32_t reg, 
                           uint32_t mask, uint32_t timeout_us) {
    while (timeout_us--) {
        if (!(ahci_port_read(ctrl, port, reg) & mask))
            return 0;
        ahci_delay(1);
    }
    return -1;
}

static int ahci_wait_set(ahci_controller_t* ctrl, int port, uint32_t reg,
                         uint32_t mask, uint32_t timeout_us) {
    while (timeout_us--) {
        if (ahci_port_read(ctrl, port, reg) & mask)
            return 0;
        ahci_delay(1);
    }
    return -1;
}

// ============================================================================
// СБРОС И ИНИЦИАЛИЗАЦИЯ КОНТРОЛЛЕРА
// ============================================================================
static int ahci_reset_controller(ahci_controller_t* ctrl) {
    serial_puts("[AHCI] Resetting controller...\n");
    
    uint32_t ghc = ahci_read(ctrl, AHCI_GHC);
    ahci_write(ctrl, AHCI_GHC, ghc | AHCI_GHC_HR);
    
    int timeout = 1000000;
    while (timeout--) {
        if (!(ahci_read(ctrl, AHCI_GHC) & AHCI_GHC_HR)) {
            serial_puts("[AHCI] Controller reset OK\n");
            return 0;
        }
        ahci_delay(1);
    }
    
    serial_puts("[AHCI] ERROR: Controller reset timeout\n");
    return -1;
}

static int ahci_port_reset(ahci_controller_t* ctrl, int port) {
    serial_puts("[AHCI] Resetting port ");
    serial_puts_num(port);
    serial_puts("\n");
    
    uint32_t cmd = ahci_port_read(ctrl, port, AHCI_PXCMD);
    cmd &= ~(AHCI_PXCMD_ST | AHCI_PXCMD_FRE);
    ahci_port_write(ctrl, port, AHCI_PXCMD, cmd);
    
    if (ahci_wait_clear(ctrl, port, AHCI_PXCMD, AHCI_PXCMD_CR | AHCI_PXCMD_FR, 100000) < 0) {
        serial_puts("[AHCI] Port stop timeout\n");
        return -1;
    }
    
    uint32_t sctl = ahci_port_read(ctrl, port, AHCI_PXSCTL);
    sctl &= ~AHCI_PXSCTL_DET_MASK;
    sctl |= AHCI_PXSCTL_DET_INIT;
    ahci_port_write(ctrl, port, AHCI_PXSCTL, sctl);
    
    ahci_delay(10000);
    
    sctl &= ~AHCI_PXSCTL_DET_MASK;
    sctl |= AHCI_PXSCTL_DET_NONE;
    ahci_port_write(ctrl, port, AHCI_PXSCTL, sctl);
    
    if (ahci_wait_set(ctrl, port, AHCI_PXSSTS, AHCI_PXSSTS_DET_PRESENT, 1000000) < 0) {
        serial_puts("[AHCI] No device detected after reset\n");
        return -1;
    }
    
    serial_puts("[AHCI] Port reset OK\n");
    return 0;
}

// ============================================================================
// ИНИЦИАЛИЗАЦИЯ ПОРТА
// ============================================================================
static int ahci_port_init(ahci_controller_t* ctrl, int port) {
    serial_puts("[AHCI] Initializing port ");
    serial_puts_num(port);
    serial_puts("\n");
    
    ahci_port_t* pdata = &ctrl->port_data[port];
    
    uint32_t cmd = ahci_port_read(ctrl, port, AHCI_PXCMD);
    if (cmd & (AHCI_PXCMD_ST | AHCI_PXCMD_FRE)) {
        cmd &= ~(AHCI_PXCMD_ST | AHCI_PXCMD_FRE);
        ahci_port_write(ctrl, port, AHCI_PXCMD, cmd);
        
        if (ahci_wait_clear(ctrl, port, AHCI_PXCMD, AHCI_PXCMD_CR | AHCI_PXCMD_FR, 100000) < 0) {
            serial_puts("[AHCI] Port stop timeout\n");
            return -1;
        }
    }
    
    pdata->cmd_list = (ahci_cmd_entry_t*)kmalloc_aligned(1024, 1024);
    if (!pdata->cmd_list) {
        serial_puts("[AHCI] Failed to allocate command list\n");
        return -1;
    }
    memset(pdata->cmd_list, 0, 1024);
    pdata->cmd_list_phys = virt_to_phys(pdata->cmd_list);
    
    for (int i = 0; i < AHCI_MAX_CMDS; i++) {
        pdata->cmd_tables[i] = (ahci_cmd_table_t*)kmalloc_aligned(256, 128);
        if (!pdata->cmd_tables[i]) {
            serial_puts("[AHCI] Failed to allocate command table\n");
            for (int j = 0; j < i; j++) {
                kfree_aligned(pdata->cmd_tables[j]);
            }
            kfree_aligned(pdata->cmd_list);
            return -1;
        }
        memset(pdata->cmd_tables[i], 0, 256);
        pdata->cmd_tables_phys[i] = virt_to_phys(pdata->cmd_tables[i]);
    }
    
    ahci_port_write(ctrl, port, AHCI_PXCLB, pdata->cmd_list_phys);
    ahci_port_write(ctrl, port, AHCI_PXCLBU, 0);
    ahci_port_write(ctrl, port, AHCI_PXFB, 0);
    ahci_port_write(ctrl, port, AHCI_PXFBU, 0);
    
    cmd = ahci_port_read(ctrl, port, AHCI_PXCMD);
    cmd |= AHCI_PXCMD_FRE | AHCI_PXCMD_SUD | AHCI_PXCMD_POD;
    ahci_port_write(ctrl, port, AHCI_PXCMD, cmd);
    
    if (ahci_wait_set(ctrl, port, AHCI_PXCMD, AHCI_PXCMD_FR, 100000) < 0) {
        serial_puts("[AHCI] FR not set\n");
        return -1;
    }
    
    serial_puts("[AHCI] Port initialized\n");
    return 0;
}

// ============================================================================
// ОПРЕДЕЛЕНИЕ ТИПА УСТРОЙСТВА
// ============================================================================
static int ahci_probe_port(ahci_controller_t* ctrl, int port) {
    uint32_t ssts = ahci_port_read(ctrl, port, AHCI_PXSSTS);
    uint32_t det = ssts & AHCI_PXSSTS_DET_MASK;
    
    if (det != AHCI_PXSSTS_DET_PRESENT) {
        return 0;
    }
    
    serial_puts("[AHCI] Port ");
    serial_puts_num(port);
    serial_puts(": device present");
    
    uint32_t sig = ahci_port_read(ctrl, port, AHCI_PXSIG);
    if (sig == 0xEB140101) {
        serial_puts(" (ATAPI)\n");
        ctrl->port_data[port].atapi = 1;
    } else if (sig == 0x00000101) {
        serial_puts(" (SATA)\n");
        ctrl->port_data[port].atapi = 0;
    } else {
        serial_puts(" (unknown, sig=0x");
        serial_puts_num_hex(sig);
        serial_puts(")\n");
        return 0;
    }
    
    ctrl->port_data[port].present = 1;
    return 1;
}

// ============================================================================
// IDENTIFY (ОПРЕДЕЛЕНИЕ ПАРАМЕТРОВ ДИСКА)
// ============================================================================
typedef struct {
    uint16_t config;
    uint16_t cylinders;
    uint16_t reserved1;
    uint16_t heads;
    uint16_t bytes_per_track;
    uint16_t bytes_per_sector;
    uint16_t sectors_per_track;
    uint16_t vendor[3];
    char     serial[20];
    uint16_t buffer_type;
    uint16_t buffer_size;
    uint16_t ecc_size;
    char     firmware[8];
    char     model[40];
    uint16_t max_sectors_per_intr;
    uint16_t capabilities;
    uint16_t reserved2[2];
    uint16_t pio_mode;
    uint16_t dma_mode;
    uint16_t valid;
    uint16_t current_cylinders;
    uint16_t current_heads;
    uint16_t current_sectors;
    uint32_t current_capacity;
    uint16_t multi_sectors;
    uint32_t lba_capacity;
    uint16_t singleword_dma;
    uint16_t multiword_dma;
    uint16_t advanced_pio_modes;
    uint16_t min_mw_dma_cycle;
    uint16_t rec_mw_dma_cycle;
    uint16_t min_pio_cycle;
    uint16_t min_pio_iordy;
    uint16_t additional[58];
    uint32_t wwn;
    uint16_t additional2[32];
    uint32_t lba48_capacity;
} __attribute__((packed)) ata_identify_t;

static int ahci_identify_port(ahci_controller_t* ctrl, int port) {
    ahci_port_t* pdata = &ctrl->port_data[port];
    if (!pdata->present) return -1;

    ata_identify_t* ident = (ata_identify_t*)kmalloc(512);
    if (!ident) return -1;
    memset(ident, 0, 512);

    int slot = -1;
    uint32_t sact = ahci_port_read(ctrl, port, AHCI_PXSACT);
    uint32_t ci = ahci_port_read(ctrl, port, AHCI_PXCI);
    for (int i = 0; i < AHCI_MAX_CMDS; i++) {
        if (!(sact & (1 << i)) && !(ci & (1 << i))) {
            slot = i;
            break;
        }
    }
    if (slot == -1) {
        kfree(ident);
        return -1;
    }

    ahci_cmd_entry_t* cmd_entry = &pdata->cmd_list[slot];
    memset(cmd_entry, 0, sizeof(ahci_cmd_entry_t));
    cmd_entry->cfl = 5;
    cmd_entry->w = 0;
    cmd_entry->prdtl = 1;
    cmd_entry->ctba = pdata->cmd_tables_phys[slot];

    ahci_cmd_table_t* table = pdata->cmd_tables[slot];
    memset(table, 0, sizeof(ahci_cmd_table_t));
    table->prdt[0].dba = virt_to_phys(ident);
    table->prdt[0].dbc = (512 - 1) | 0x80000000;

    uint8_t* fis = table->cfis;
    fis[0] = 0x27;
    fis[1] = 0x80;
    fis[2] = 0xEC;  // IDENTIFY DEVICE

    ci |= (1 << slot);
    ahci_port_write(ctrl, port, AHCI_PXCI, ci);

    int timeout = 30000000;
    while (timeout--) {
        if (!(ahci_port_read(ctrl, port, AHCI_PXCI) & (1 << slot))) {
            uint32_t serr = ahci_port_read(ctrl, port, AHCI_PXSERR);
            if (serr) {
                serial_puts("[AHCI] SError during identify: 0x");
                serial_puts_num_hex(serr);
                serial_puts("\n");
                ahci_port_write(ctrl, port, AHCI_PXSERR, serr);
                kfree(ident);
                return -1;
            }

            // Смена порядка байтов (ATA данные big-endian)
            uint16_t* words = (uint16_t*)ident;
            for (int i = 0; i < 256; i++) {
                words[i] = __builtin_bswap16(words[i]);
            }

            // Извлекаем модель (слова 27-46)
            char model[41];
            for (int i = 0; i < 20; i++) {
                model[i*2] = (char)(words[27 + i] >> 8);
                model[i*2+1] = (char)(words[27 + i] & 0xFF);
            }
            model[40] = '\0';
            // Убираем пробелы в конце
            for (int i = 39; i >= 0 && model[i] == ' '; i--) model[i] = '\0';

            uint32_t sectors;
            if (words[83] & 0x40) { // LBA48 supported
                sectors = (uint32_t)ident->lba48_capacity;
            } else {
                sectors = ident->lba_capacity;
            }

            serial_puts("[AHCI] Port ");
            serial_puts_num(port);
            serial_puts(": ");
            serial_puts(model);
            serial_puts(", ");
            serial_puts_num(sectors);
            serial_puts(" sectors\n");

            disk_register(DISK_TYPE_AHCI, port, 0, sectors, model, 0);

            kfree(ident);
            return 0;
        }
        for (volatile int i = 0; i < 100; i++);
    }

    serial_puts("[AHCI] Identify timeout on port ");
    serial_puts_num(port);
    serial_puts("\n");
    kfree(ident);
    return -1;
}

// ============================================================================
// ОСНОВНАЯ ФУНКЦИЯ ЧТЕНИЯ
// ============================================================================
int ahci_read_sectors(int port, uint32_t lba, uint32_t count, void* buffer) {
    if (!ahci_ctrl) {
        serial_puts("[AHCI] ERROR: Controller not initialized\n");
        return -1;
    }
    
    if (port >= ahci_ctrl->num_ports) {
        serial_puts("[AHCI] ERROR: Invalid port\n");
        return -1;
    }
    
    if (!ahci_ctrl->port_data[port].present) {
        serial_puts("[AHCI] ERROR: No device on port\n");
        return -1;
    }
    
    if (ahci_ctrl->port_data[port].atapi) {
        serial_puts("[AHCI] ERROR: ATAPI not supported\n");
        return -1;
    }
    
    if (count == 0) return 0;
    
    ahci_port_t* pdata = &ahci_ctrl->port_data[port];
    
    uint32_t cmd = ahci_port_read(ahci_ctrl, port, AHCI_PXCMD);
    if (!(cmd & AHCI_PXCMD_FRE)) {
        serial_puts("[AHCI] Port not ready (FRE=0)\n");
        return -1;
    }
    
    int slot = -1;
    uint32_t sact = ahci_port_read(ahci_ctrl, port, AHCI_PXSACT);
    uint32_t ci = ahci_port_read(ahci_ctrl, port, AHCI_PXCI);
    
    for (int i = 0; i < AHCI_MAX_CMDS; i++) {
        if (!(sact & (1 << i)) && !(ci & (1 << i))) {
            slot = i;
            break;
        }
    }
    
    if (slot == -1) {
        serial_puts("[AHCI] No free command slot\n");
        return -1;
    }
    
    ahci_cmd_entry_t* cmd_entry = &pdata->cmd_list[slot];
    memset(cmd_entry, 0, sizeof(ahci_cmd_entry_t));
    
    cmd_entry->cfl = 5;
    cmd_entry->w = 0;
    cmd_entry->prdtl = 1;
    cmd_entry->ctba = pdata->cmd_tables_phys[slot];
    
    ahci_cmd_table_t* table = pdata->cmd_tables[slot];
    memset(table, 0, sizeof(ahci_cmd_table_t));
    
    table->prdt[0].dba = virt_to_phys(buffer);
    table->prdt[0].dbc = (count * 512 - 1) | 0x80000000;
    
    uint8_t* fis = table->cfis;
    fis[0] = 0x27;
    fis[1] = 0x80;
    fis[2] = 0x25;  // READ DMA EXT
    
    fis[4] = (uint8_t)(lba);
    fis[5] = (uint8_t)(lba >> 8);
    fis[6] = (uint8_t)(lba >> 16);
    fis[7] = 0x40;
    fis[8] = (uint8_t)(lba >> 24);
    fis[9] = (uint8_t)(lba >> 32);
    fis[10] = (uint8_t)(lba >> 40);
    
    fis[12] = (uint8_t)(count);
    fis[13] = (uint8_t)(count >> 8);
    
    ci |= (1 << slot);
    ahci_port_write(ahci_ctrl, port, AHCI_PXCI, ci);
    
    int timeout = 30000000;
    while (timeout--) {
        if (!(ahci_port_read(ahci_ctrl, port, AHCI_PXCI) & (1 << slot))) {
            uint32_t serr = ahci_port_read(ahci_ctrl, port, AHCI_PXSERR);
            if (serr) {
                serial_puts("[AHCI] SError after read: 0x");
                serial_puts_num_hex(serr);
                serial_puts("\n");
                ahci_port_write(ahci_ctrl, port, AHCI_PXSERR, serr);
                return -1;
            }
            return count;
        }
        for (volatile int i = 0; i < 100; i++);
    }
    
    serial_puts("[AHCI] Read timeout\n");
    return -1;
}

// ============================================================================
// ОСНОВНАЯ ФУНКЦИЯ ЗАПИСИ
// ============================================================================
int ahci_write_sectors(int port, uint32_t lba, uint32_t count, void* buffer) {
    if (!ahci_ctrl) {
        serial_puts("[AHCI] ERROR: Controller not initialized\n");
        return -1;
    }
    
    if (port >= ahci_ctrl->num_ports) {
        serial_puts("[AHCI] ERROR: Invalid port\n");
        return -1;
    }
    
    if (!ahci_ctrl->port_data[port].present) {
        serial_puts("[AHCI] ERROR: No device on port\n");
        return -1;
    }
    
    if (ahci_ctrl->port_data[port].atapi) {
        serial_puts("[AHCI] ERROR: ATAPI not supported\n");
        return -1;
    }
    
    if (count == 0) return 0;
    
    ahci_port_t* pdata = &ahci_ctrl->port_data[port];
    
    uint32_t cmd = ahci_port_read(ahci_ctrl, port, AHCI_PXCMD);
    if (!(cmd & AHCI_PXCMD_FRE)) {
        serial_puts("[AHCI] Port not ready (FRE=0)\n");
        return -1;
    }
    
    int slot = -1;
    uint32_t sact = ahci_port_read(ahci_ctrl, port, AHCI_PXSACT);
    uint32_t ci = ahci_port_read(ahci_ctrl, port, AHCI_PXCI);
    
    for (int i = 0; i < AHCI_MAX_CMDS; i++) {
        if (!(sact & (1 << i)) && !(ci & (1 << i))) {
            slot = i;
            break;
        }
    }
    
    if (slot == -1) {
        serial_puts("[AHCI] No free command slot\n");
        return -1;
    }
    
    ahci_cmd_entry_t* cmd_entry = &pdata->cmd_list[slot];
    memset(cmd_entry, 0, sizeof(ahci_cmd_entry_t));
    
    cmd_entry->cfl = 5;
    cmd_entry->w = 1;
    cmd_entry->prdtl = 1;
    cmd_entry->ctba = pdata->cmd_tables_phys[slot];
    
    ahci_cmd_table_t* table = pdata->cmd_tables[slot];
    memset(table, 0, sizeof(ahci_cmd_table_t));
    
    table->prdt[0].dba = virt_to_phys(buffer);
    table->prdt[0].dbc = (count * 512 - 1) | 0x80000000;
    
    uint8_t* fis = table->cfis;
    fis[0] = 0x27;
    fis[1] = 0x80;
    fis[2] = 0x35;  // WRITE DMA EXT
    
    fis[4] = (uint8_t)(lba);
    fis[5] = (uint8_t)(lba >> 8);
    fis[6] = (uint8_t)(lba >> 16);
    fis[7] = 0x40;
    fis[8] = (uint8_t)(lba >> 24);
    fis[9] = (uint8_t)(lba >> 32);
    fis[10] = (uint8_t)(lba >> 40);
    
    fis[12] = (uint8_t)(count);
    fis[13] = (uint8_t)(count >> 8);
    
    ci |= (1 << slot);
    ahci_port_write(ahci_ctrl, port, AHCI_PXCI, ci);
    
    int timeout = 30000000;
    while (timeout--) {
        if (!(ahci_port_read(ahci_ctrl, port, AHCI_PXCI) & (1 << slot))) {
            uint32_t serr = ahci_port_read(ahci_ctrl, port, AHCI_PXSERR);
            if (serr) {
                serial_puts("[AHCI] SError after write: 0x");
                serial_puts_num_hex(serr);
                serial_puts("\n");
                ahci_port_write(ahci_ctrl, port, AHCI_PXSERR, serr);
                return -1;
            }
            return count;
        }
        for (volatile int i = 0; i < 100; i++);
    }
    
    serial_puts("[AHCI] Write timeout\n");
    return -1;
}

// ============================================================================
// ИНИЦИАЛИЗАЦИЯ (ВХОДНАЯ ТОЧКА)
// ============================================================================
int ahci_init(void) {
    serial_puts("[AHCI] Looking for AHCI controller...\n");
    
    pci_device_t devices[8];
    int count = pci_find_all_class(0x01, 0x06, 0x01, devices, 8);
    
    if (count == 0) {
        serial_puts("[AHCI] No AHCI controller found\n");
        return -1;
    }
    
    serial_puts("[AHCI] Found controller at ");
    serial_puts_num(devices[0].bus);
    serial_puts(":");
    serial_puts_num(devices[0].device);
    serial_puts(".");
    serial_puts_num(devices[0].func);
    serial_puts("\n");
    
    ahci_ctrl = (ahci_controller_t*)kmalloc(sizeof(ahci_controller_t));
    if (!ahci_ctrl) {
        serial_puts("[AHCI] Failed to allocate controller structure\n");
        return -1;
    }
    memset(ahci_ctrl, 0, sizeof(ahci_controller_t));
    
    ahci_ctrl->pci_bus = devices[0].bus;
    ahci_ctrl->pci_dev = devices[0].device;
    ahci_ctrl->pci_func = devices[0].func;
    ahci_ctrl->irq = pci_read8(devices[0].bus, devices[0].device, devices[0].func, 0x3C);
    
    uint32_t bar5 = pci_read32(devices[0].bus, devices[0].device, devices[0].func, 0x24);
    ahci_ctrl->base = bar5 & 0xFFFFFFF0;
    
    serial_puts("[AHCI] ABAR: 0x");
    serial_puts_num_hex(ahci_ctrl->base);
    serial_puts(", IRQ: ");
    serial_puts_num(ahci_ctrl->irq);
    serial_puts("\n");
    
    uint16_t cmd = pci_read16(devices[0].bus, devices[0].device, devices[0].func, 0x04);
    cmd |= 0x0006;  // Bus Master + Memory Space
    pci_write16(devices[0].bus, devices[0].device, devices[0].func, 0x04, cmd);
    
    if (ahci_reset_controller(ahci_ctrl) < 0) {
        serial_puts("[AHCI] Controller reset failed\n");
        kfree(ahci_ctrl);
        ahci_ctrl = NULL;
        return -1;
    }
    
    ahci_ctrl->cap = ahci_read(ahci_ctrl, AHCI_CAP);
    ahci_ctrl->cap2 = ahci_read(ahci_ctrl, AHCI_CAP2);
    ahci_ctrl->ports_impl = ahci_read(ahci_ctrl, AHCI_PI);
    
    ahci_ctrl->num_ports = (ahci_ctrl->cap & 0x1F) + 1;
    if (ahci_ctrl->num_ports > AHCI_MAX_PORTS)
        ahci_ctrl->num_ports = AHCI_MAX_PORTS;
    
    serial_puts("[AHCI] CAP: 0x");
    serial_puts_num_hex(ahci_ctrl->cap);
    serial_puts(", Ports: ");
    serial_puts_num(ahci_ctrl->num_ports);
    serial_puts("\n");
    
    ahci_write(ahci_ctrl, AHCI_GHC, ahci_read(ahci_ctrl, AHCI_GHC) | AHCI_GHC_AE);
    
    // Инициализируем каждый порт
    for (int i = 0; i < ahci_ctrl->num_ports; i++) {
        if (!(ahci_ctrl->ports_impl & (1 << i))) {
            continue;
        }
        
        if (ahci_port_reset(ahci_ctrl, i) < 0) {
            continue;
        }
        
        if (ahci_port_init(ahci_ctrl, i) < 0) {
            continue;
        }
        
        if (ahci_probe_port(ahci_ctrl, i)) {
            if (!ahci_ctrl->port_data[i].atapi) {
                ahci_identify_port(ahci_ctrl, i);
            }
        }
    }
    
    ahci_write(ahci_ctrl, AHCI_GHC, ahci_read(ahci_ctrl, AHCI_GHC) | AHCI_GHC_IE);
    
    serial_puts("[AHCI] Initialized successfully\n");
    return 0;
}