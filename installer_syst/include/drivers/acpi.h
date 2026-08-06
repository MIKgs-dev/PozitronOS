#ifndef DRIVERS_ACPI_H
#define DRIVERS_ACPI_H

#include <stdint.h>
#include "kernel/types.h"
#include <uacpi/uacpi.h>
#include <uacpi/types.h>
#include <uacpi/kernel_api.h>

int acpi_init(void);

void acpi_poweroff(void);
void acpi_reboot(void);

#endif // DRIVERS_ACPI_H