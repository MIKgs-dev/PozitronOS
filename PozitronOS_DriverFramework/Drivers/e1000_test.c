// e1000_test.c
#include <kernel/drivermgr.h>

#define E1000_EERD_OFFSET   0x14
#define E1000_EERD_START    0x00000001
#define E1000_EERD_DONE     0x00000002
#define E1000_EEPROM_MAC    0x0000

typedef struct {
    uint32_t mmio_base;
    uint8_t mac[6];
    uint8_t irq;
} e1000_test_priv_t;

static void str_copy(char* dst, const char* src, int max_len) {
    int i;
    for (i = 0; i < max_len - 1 && src[i] != 0; i++) dst[i] = src[i];
    dst[i] = 0;
}

static int e1000_test_init(driver_module_t* mod, const driver_api_t* api) {
    api->log("[E1000] Init OK");
    return 0;
}

static int e1000_test_probe(driver_module_t* mod, hw_device_t* hw_dev) {
    const driver_api_t* api = mod->api;
    
    if (!hw_dev || hw_dev->bus != BUS_PCI) return -1;
    
    uint32_t bar0 = api->pci_read(hw_dev->pci.bus, hw_dev->pci.device, 
                                   hw_dev->pci.function, 0x10, 4);
    
    uint32_t mmio_base = bar0 & 0xFFFFFFF0;
    
    if (mmio_base == 0 || mmio_base == 0xFFFFFFF0) {
        return -1;
    }
    
    hw_dev->driver_data = (void*)mmio_base;
    return 0;
}

static int e1000_test_attach(driver_module_t* mod, hw_device_t* hw_dev, driver_connector_t* conn) {
    const driver_api_t* api = mod->api;
    int success = 0;
    int attempt;
    int i, j;
    
    e1000_test_priv_t* priv = (e1000_test_priv_t*)api->kmalloc(sizeof(e1000_test_priv_t));
    if (!priv) return -1;
    
    uint8_t* p = (uint8_t*)priv;
    for (i = 0; i < sizeof(e1000_test_priv_t); i++) p[i] = 0;
    
    priv->mmio_base = (uint32_t)hw_dev->driver_data;
    volatile uint32_t* mmio = (volatile uint32_t*)priv->mmio_base;
    
    api->log("[E1000] MMIO at 0x%x", priv->mmio_base);
    
    for (attempt = 0; attempt < 2; attempt++) {
        uint8_t temp_mac[6];
        int valid = 1;
        
        // Пробуем EERD
        for (i = 0; i < 3 && valid; i++) {
            uint32_t eerd_cmd = ((uint32_t)((E1000_EEPROM_MAC + i) << 8)) | E1000_EERD_START;
            mmio[E1000_EERD_OFFSET / 4] = eerd_cmd;
            
            uint32_t eerd_data;
            int timeout = 1000000;
            do {
                eerd_data = mmio[E1000_EERD_OFFSET / 4];
                if (--timeout == 0) {
                    valid = 0;
                    break;
                }
            } while (!(eerd_data & E1000_EERD_DONE));
            
            if (valid) {
                uint16_t word = (uint16_t)(eerd_data >> 16);
                temp_mac[i * 2] = (uint8_t)(word & 0xFF);
                temp_mac[i * 2 + 1] = (uint8_t)(word >> 8);
            }
        }
        
        if (valid) {
            int all_ff = 1, all_00 = 1;
            for (j = 0; j < 6; j++) {
                if (temp_mac[j] != 0xFF) all_ff = 0;
                if (temp_mac[j] != 0x00) all_00 = 0;
            }
            
            if (!all_ff && !all_00) {
                for (j = 0; j < 6; j++) priv->mac[j] = temp_mac[j];
                success = 1;
                break;
            }
        }
        
        // Пробуем RAL/RAH
        uint32_t ral = mmio[0x5400 / 4];
        uint32_t rah = mmio[0x5404 / 4];
        
        temp_mac[0] = (uint8_t)(ral & 0xFF);
        temp_mac[1] = (uint8_t)((ral >> 8) & 0xFF);
        temp_mac[2] = (uint8_t)((ral >> 16) & 0xFF);
        temp_mac[3] = (uint8_t)((ral >> 24) & 0xFF);
        temp_mac[4] = (uint8_t)(rah & 0xFF);
        temp_mac[5] = (uint8_t)((rah >> 8) & 0xFF);
        
        int all_ff = 1, all_00 = 1;
        for (j = 0; j < 6; j++) {
            if (temp_mac[j] != 0xFF) all_ff = 0;
            if (temp_mac[j] != 0x00) all_00 = 0;
        }
        
        if (!all_ff && !all_00) {
            for (j = 0; j < 6; j++) priv->mac[j] = temp_mac[j];
            success = 1;
            break;
        }
    }
    
    if (!success) {
        api->error("[E1000] Failed to read MAC");
        api->kfree(priv);
        return -1;
    }
    
    // Форматируем MAC
    char mac_str[32];
    char hex[] = "0123456789ABCDEF";
    int pos = 0;
    
    for (i = 0; i < 6; i++) {
        mac_str[pos++] = hex[(priv->mac[i] >> 4) & 0xF];
        mac_str[pos++] = hex[priv->mac[i] & 0xF];
        if (i < 5) mac_str[pos++] = ':';
    }
    mac_str[pos] = 0;
    
    api->log("[E1000] MAC: %s", mac_str);
    api->log("[E1000] Attached successfully!");
    
    conn->type = DRV_TYPE_NETWORK;
    conn->name = "e1000_test";
    conn->device = hw_dev;
    conn->private_data = priv;
    conn->net_send = 0;
    conn->net_recv = 0;
    conn->net_set_mac = 0;
    
    mod->private_data = priv;
    return 0;
}

static int e1000_test_detach(driver_module_t* mod) {
    if (mod->private_data) {
        mod->api->kfree(mod->private_data);
        mod->private_data = 0;
    }
    return 0;
}

static int e1000_test_unload(driver_module_t* mod) {
    if (mod->active) e1000_test_detach(mod);
    return 0;
}

__attribute__((section(".drv_meta"), used))
static const struct {
    uint32_t magic;
    uint32_t version;
    uint32_t type;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    char name[32];
    char version_str[16];
    char author[32];
} drv_meta = {
    .magic = 0x4452564C,
    .version = 1,
    .type = DRV_TYPE_NETWORK,
    .vendor_id = 0x8086,
    .device_id = 0x100E,
    .class_code = 0x02,
    .subclass = 0x00,
    .name = "e1000_test",
    .version_str = "1.0",
    .author = "PozitronOS Team",
};

__attribute__((section(".drv_module"), used))
static driver_module_t driver_module = {
    .magic = DRV_MAGIC,
    .api_version_major = DRV_API_VERSION_MAJOR,
    .api_version_minor = DRV_API_VERSION_MINOR,
    .api_version_patch = DRV_API_VERSION_PATCH,
    .name = "",
    .version = "",
    .author = "",
    .type = DRV_TYPE_NETWORK,
    .vendor_id = 0x8086,
    .device_id = 0x100E,
    .class_code = 0x02,
    .subclass = 0x00,
    .match_any_vendor = 0,
    .match_any_device = 0,
    .state = DRV_STATE_LOADED,
    .refcount = 0,
    .loaded = 1,
    .active = 0,
    .private_data = 0,
    .init = e1000_test_init,
    .probe = e1000_test_probe,
    .attach = e1000_test_attach,
    .detach = e1000_test_detach,
    .unload = e1000_test_unload,
};

driver_module_t* driver_entry(const driver_api_t* api) {
    str_copy(driver_module.name, "e1000_test", 64);
    str_copy(driver_module.version, "1.0", 32);
    str_copy(driver_module.author, "PozitronOS Team", 64);
    driver_module.api = api;
    return &driver_module;
}