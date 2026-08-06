#ifndef POZITRON_DEVICE_H
#define POZITRON_DEVICE_H

#include <stdint.h>
#include <kernel/mutex.h>

typedef struct device {
    uint32_t id;
    char name[32];
    
    void *softc;
    
    uint32_t bar0;
    uint32_t bar0_len;
    uint32_t bar1;
    uint32_t bar1_len;
    uint32_t bar2;
    uint32_t bar2_len;
    uint8_t irq;
    uint8_t bus;
    uint8_t dev;
    uint8_t func;

    struct device *parent;
    struct device *first_child;
    struct device *next_sibling;
    
    mutex_t lock;
} device_t;

extern device_t *device_root;

void device_init(void);
device_t* device_create(const char *name);
void device_destroy(device_t *dev);
void device_add_child(device_t *parent, device_t *child);
device_t* device_find_by_pci(uint8_t bus, uint8_t dev, uint8_t func);

#define device_get_softc(dev) ((dev)->softc)
#define device_set_softc(dev, ptr) ((dev)->softc = (ptr))
#define device_get_name(dev) ((dev)->name)

#endif