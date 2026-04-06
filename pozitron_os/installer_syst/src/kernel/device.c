#include <kernel/device.h>
#include <kernel/memory.h>
#include <lib/string.h>
#include <drivers/serial.h>

static uint32_t next_device_id = 1;
device_t *device_root = NULL;

void device_init(void) {
    device_root = NULL;
    serial_puts("[DEVICE] Subsystem initialized\n");
}

device_t* device_create(const char *name) {
    device_t *dev = (device_t*)kmalloc(sizeof(device_t));
    if (!dev) return NULL;
    
    memset(dev, 0, sizeof(device_t));
    
    dev->id = next_device_id++;
    
    if (name) {
        strncpy(dev->name, name, 31);
        dev->name[31] = '\0';
    } else {
        strcpy(dev->name, "unknown");
    }
    
    mutex_init(&dev->lock);
    
    dev->next_sibling = device_root;
    device_root = dev;
    
    serial_puts("[DEVICE] Created: ");
    serial_puts(dev->name);
    serial_puts(" (ID: ");
    serial_puts_num(dev->id);
    serial_puts(")\n");
    
    return dev;
}

void device_destroy(device_t *dev) {
    if (!dev) return;
    
    if (device_root == dev) {
        device_root = dev->next_sibling;
    } else {
        device_t *prev = device_root;
        while (prev && prev->next_sibling != dev) {
            prev = prev->next_sibling;
        }
        if (prev) {
            prev->next_sibling = dev->next_sibling;
        }
    }
    
    serial_puts("[DEVICE] Destroyed: ");
    serial_puts(dev->name);
    serial_puts("\n");
    
    kfree(dev);
}

void device_add_child(device_t *parent, device_t *child) {
    if (!parent || !child) return;
    
    child->parent = parent;
    child->next_sibling = parent->first_child;
    parent->first_child = child;
    
    serial_puts("[DEVICE] Added child ");
    serial_puts(child->name);
    serial_puts(" to ");
    serial_puts(parent->name);
    serial_puts("\n");
}

device_t* device_find_by_pci(uint8_t bus, uint8_t dev, uint8_t func) {
    device_t *curr = device_root;
    while (curr) {
        if (curr->bus == bus && curr->dev == dev && curr->func == func) {
            return curr;
        }
        curr = curr->next_sibling;
    }
    return NULL;
}