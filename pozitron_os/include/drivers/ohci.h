#ifndef OHCI_H
#define OHCI_H

#include <stdint.h>

// OHCI регистры
#define OHCI_HCREVISION   0x00
#define OHCI_HCCONTROL    0x04
#define OHCI_HCCOMMANDSTATUS 0x08
#define OHCI_HCINTERRUPTSTATUS 0x0C
#define OHCI_HCINTERRUPTENABLE 0x10
#define OHCI_HCINTERRUPTDISABLE 0x14
#define OHCI_HCHCCA       0x18
#define OHCI_HCPERIODCURRENTED 0x1C
#define OHCI_HCCONTROLHEADED 0x20
#define OHCI_HCCONTROLCURRENTED 0x24
#define OHCI_HCBULKHEADED 0x28
#define OHCI_HCBULKCURRENTED 0x2C
#define OHCI_HCDONEHEAD   0x30
#define OHCI_HCFMINTERVAL 0x34
#define OHCI_HCFMREMAINING 0x38
#define OHCI_HCFMNUMBER   0x3C
#define OHCI_HCPERIODICSTART 0x40
#define OHCI_HCLSTHRESHOLD 0x44
#define OHCI_HCRHDESCRIPTORA 0x48
#define OHCI_HCRHDESCRIPTORB 0x4C
#define OHCI_HCRHSTATUS   0x50
#define OHCI_HCRHPORTSTATUS1 0x54
#define OHCI_HCRHPORTSTATUS2 0x58

// Функции OHCI (уникальные имена!)
void ohci_init(uint32_t base);
void ohci_poll(void);
uint8_t ohci_detect_devices(void);
uint8_t ohci_is_present(void);

#endif // OHCI_H