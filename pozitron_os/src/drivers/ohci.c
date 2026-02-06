#include "drivers/ohci.h"
#include "drivers/serial.h"
#include "drivers/ports.h"
#include "drivers/usb.h"
#include "kernel/memory.h"
#include <stddef.h>
#include "lib/string.h"

static uint32_t ohci_base = 0;
static uint8_t ohci_initialized = 0;
static uint8_t ohci_ports = 0;

// Структуры OHCI
typedef struct {
    uint32_t flags;
    uint32_t td_buffer_end;
    uint32_t next_td;
    uint32_t buffer_start;
    uint32_t reserved[4];
} __attribute__((packed)) ohci_td_t;

typedef struct {
    uint32_t flags;
    uint32_t tail_td;
    uint32_t head_td;
    uint32_t next_qh;
} __attribute__((packed)) ohci_qh_t;

typedef struct {
    uint32_t interrupt_table[32];
    uint32_t frame_number;
    uint32_t done_head;
    uint8_t reserved[116];
    uint32_t done_queue[32];
} __attribute__((packed)) ohci_hcca_t;

static ohci_hcca_t* hcca = NULL;
static ohci_td_t* control_td = NULL;
static ohci_qh_t* control_qh = NULL;
static uint8_t* setup_buffer = NULL;
static uint8_t* data_buffer = NULL;

// Задержка с использованием pause
static void ohci_delay_ms(uint32_t ms) {
    for (volatile uint32_t i = 0; i < ms * 1000; i++) {
        asm volatile ("pause");
    }
}

static void ohci_delay_us(uint32_t us) {
    for (volatile uint32_t i = 0; i < us * 10; i++) {
        asm volatile ("pause");
    }
}

static uint32_t ohci_read_reg(uint32_t reg) {
    return inl(ohci_base + reg);
}

static void ohci_write_reg(uint32_t reg, uint32_t value) {
    outl(ohci_base + reg, value);
}

// Инициализация структур OHCI с использованием аллокатора
static int ohci_init_structures(void) {
    // Выделяем память для HCCA с выравниванием
    hcca = (ohci_hcca_t*)kmalloc(sizeof(ohci_hcca_t));
    if (!hcca) {
        serial_puts("[OHCI] ERROR: Failed to allocate HCCA\n");
        return 0;
    }
    
    control_td = (ohci_td_t*)kmalloc(sizeof(ohci_td_t) * 4);
    if (!control_td) {
        kfree(hcca);
        serial_puts("[OHCI] ERROR: Failed to allocate TDs\n");
        return 0;
    }
    
    control_qh = (ohci_qh_t*)kmalloc(sizeof(ohci_qh_t));
    if (!control_qh) {
        kfree(hcca);
        kfree(control_td);
        serial_puts("[OHCI] ERROR: Failed to allocate QH\n");
        return 0;
    }
    
    setup_buffer = (uint8_t*)kmalloc(8);
    if (!setup_buffer) {
        kfree(hcca);
        kfree(control_td);
        kfree(control_qh);
        serial_puts("[OHCI] ERROR: Failed to allocate setup buffer\n");
        return 0;
    }
    
    data_buffer = (uint8_t*)kmalloc(USB_MAX_PACKET_SIZE);
    if (!data_buffer) {
        kfree(hcca);
        kfree(control_td);
        kfree(control_qh);
        kfree(setup_buffer);
        serial_puts("[OHCI] ERROR: Failed to allocate data buffer\n");
        return 0;
    }
    
    // Инициализируем HCCA
    memset(hcca, 0, sizeof(ohci_hcca_t));
    
    // Инициализируем Control QH
    memset(control_qh, 0, sizeof(ohci_qh_t));
    control_qh->next_qh = 0x00000001; // Терминируем
    
    serial_puts("[OHCI] Structures initialized\n");
    return 1;
}

// Освобождение структур
static void ohci_free_structures(void) {
    if (hcca) kfree(hcca);
    if (control_td) kfree(control_td);
    if (control_qh) kfree(control_qh);
    if (setup_buffer) kfree(setup_buffer);
    if (data_buffer) kfree(data_buffer);
    
    hcca = NULL;
    control_td = NULL;
    control_qh = NULL;
    setup_buffer = NULL;
    data_buffer = NULL;
}

// Создание Setup TD
static ohci_td_t* create_setup_td(ohci_td_t* next_td, uint8_t* setup_data,
                                 uint8_t device_addr, uint8_t endpoint,
                                 uint8_t max_packet_size) {
    if (!control_td) return NULL;
    
    ohci_td_t* td = &control_td[0];
    memset(td, 0, sizeof(ohci_td_t));
    
    // Flags
    td->flags = (1 << 18); // Дирекция: setup
    td->flags |= (0 << 19); // Задержка
    td->flags |= (3 << 21); // 3 ошибки
    td->flags |= (0 << 24); // Condition Code = Not Accessed
    td->flags |= (0 << 26); // Data Toggle = 0
    
    // Buffer End
    td->td_buffer_end = (uint32_t)setup_buffer + 7;
    
    // Next TD
    td->next_td = next_td ? (uint32_t)next_td : 0x00000001;
    
    // Buffer Start
    td->buffer_start = (uint32_t)setup_buffer;
    
    // Копируем setup данные
    if (setup_data) {
        memcpy(setup_buffer, setup_data, 8);
    }
    
    return td;
}

// Создание Data TD
static ohci_td_t* create_data_td(ohci_td_t* next_td, uint8_t* data, uint16_t length,
                                uint8_t direction, uint8_t device_addr, uint8_t endpoint,
                                uint8_t max_packet_size, uint8_t data_toggle) {
    if (!control_td) return NULL;
    
    ohci_td_t* td = &control_td[1];
    memset(td, 0, sizeof(ohci_td_t));
    
    // Flags
    td->flags = (direction << 18); // IN или OUT
    td->flags |= (0 << 19); // Задержка
    td->flags |= (3 << 21); // 3 ошибки
    td->flags |= (0 << 24); // Condition Code = Not Accessed
    td->flags |= (data_toggle << 26); // Data Toggle
    
    // Buffer End
    td->td_buffer_end = (uint32_t)data_buffer + length - 1;
    
    // Next TD
    td->next_td = next_td ? (uint32_t)next_td : 0x00000001;
    
    // Buffer Start
    td->buffer_start = (uint32_t)data_buffer;
    
    // Копируем данные для OUT
    if (direction == 0 && data && length > 0) {
        memcpy(data_buffer, data, length);
    }
    
    return td;
}

// Создание Status TD
static ohci_td_t* create_status_td(ohci_td_t* next_td, uint8_t direction,
                                  uint8_t device_addr, uint8_t endpoint) {
    if (!control_td) return NULL;
    
    ohci_td_t* td = &control_td[2];
    memset(td, 0, sizeof(ohci_td_t));
    
    // Flags
    td->flags = (direction << 18); // IN или OUT
    td->flags |= (0 << 19); // Задержка
    td->flags |= (3 << 21); // 3 ошибки
    td->flags |= (0 << 24); // Condition Code = Not Accessed
    td->flags |= (1 << 26); // Data Toggle = 1
    
    // Buffer End
    td->td_buffer_end = 0;
    
    // Next TD
    td->next_td = next_td ? (uint32_t)next_td : 0x00000001;
    
    // Buffer Start
    td->buffer_start = 0;
    
    return td;
}

// Ожидание завершения TD
static int wait_for_td_completion(ohci_td_t* td, uint32_t timeout_ms) {
    uint32_t start_time = 0;
    
    while (start_time < timeout_ms * 1000) {
        uint32_t condition_code = (td->flags >> 24) & 0x0F;
        
        if (condition_code != 0) {
            // TD обработан
            if (condition_code == 1) {
                return 0; // No Error
            } else {
                serial_puts("[OHCI] TD error code: ");
                serial_puts_num(condition_code);
                serial_puts("\n");
                return -1;
            }
        }
        
        start_time++;
        ohci_delay_us(10);
    }
    
    serial_puts("[OHCI] TD timeout\n");
    return -1;
}

// Контрольная транзакция OHCI
int ohci_control_transfer(uint8_t controller_idx,
                         usb_device_t* dev,
                         uint8_t bmRequestType,
                         uint8_t bRequest,
                         uint16_t wValue,
                         uint16_t wIndex,
                         uint16_t wLength,
                         void* data) {
    if (!dev || !ohci_initialized) {
        serial_puts("[OHCI] ERROR: Controller not initialized\n");
        return -1;
    }
    
    if (!control_td || !control_qh || !hcca) {
        serial_puts("[OHCI] ERROR: Structures not initialized\n");
        return -1;
    }
    
    // Создаем setup пакет
    usb_setup_packet_t setup = {
        .bmRequestType = bmRequestType,
        .bRequest = bRequest,
        .wValue = wValue,
        .wIndex = wIndex,
        .wLength = wLength
    };
    
    serial_puts("[OHCI] Control transfer: addr=");
    serial_puts_num(dev->address);
    serial_puts(" req=0x");
    serial_puts_num_hex(bRequest);
    serial_puts("\n");
    
    // Определяем направление
    uint8_t data_direction;
    uint8_t status_direction;
    
    if (wLength > 0 && (bmRequestType & 0x80)) {
        // IN transfer
        data_direction = 1; // IN
        status_direction = 0; // OUT
    } else if (wLength > 0) {
        // OUT transfer
        data_direction = 0; // OUT
        status_direction = 1; // IN
    } else {
        // No data stage
        data_direction = 0;
        status_direction = 1; // IN
    }
    
    // Создаем TD цепочку
    ohci_td_t* status_td = create_status_td(NULL, status_direction,
                                           dev->address, 0);
    if (!status_td) return -1;
    
    ohci_td_t* data_td = NULL;
    if (wLength > 0) {
        data_td = create_data_td(status_td, data, wLength,
                                data_direction, dev->address, 0,
                                dev->max_packet_size, dev->interfaces[0].endpoints[0].toggle);
        if (!data_td) return -1;
    }
    
    ohci_td_t* setup_td = create_setup_td(data_td ? data_td : status_td,
                                         (uint8_t*)&setup, dev->address, 0,
                                         dev->max_packet_size);
    if (!setup_td) return -1;
    
    // Обновляем очередь
    control_qh->head_td = (uint32_t)setup_td;
    control_qh->tail_td = (uint32_t)status_td;
    
    // Запускаем передачу
    ohci_write_reg(OHCI_HCCONTROLHEADED, (uint32_t)control_qh);
    
    // Ждем завершения
    int result = 0;
    
    // Setup stage
    if (wait_for_td_completion(setup_td, 100) < 0) {
        serial_puts("[OHCI] Setup stage failed\n");
        result = -1;
        goto cleanup;
    }
    
    // Data stage
    if (data_td && wLength > 0) {
        if (wait_for_td_completion(data_td, 100) < 0) {
            serial_puts("[OHCI] Data stage failed\n");
            result = -1;
            goto cleanup;
        }
        
        // Если это IN transfer, копируем данные
        if ((bmRequestType & 0x80) && data) {
            memcpy(data, data_buffer, wLength);
        }
        
        // Переключаем data toggle
        dev->interfaces[0].endpoints[0].toggle ^= 1;
    }
    
    // Status stage
    if (wait_for_td_completion(status_td, 100) < 0) {
        serial_puts("[OHCI] Status stage failed\n");
        result = -1;
        goto cleanup;
    }
    
    result = wLength;
    
cleanup:
    // Очищаем очередь
    control_qh->head_td = 0;
    control_qh->tail_td = 0;
    
    return result;
}

// Прерывающая транзакция OHCI
int ohci_interrupt_transfer(uint8_t controller_idx,
                           usb_device_t* dev,
                           uint8_t endpoint,
                           void* buffer,
                           uint16_t length,
                           uint32_t timeout_ms) {
    if (!dev || !ohci_initialized || !buffer) {
        serial_puts("[OHCI] ERROR: Invalid parameters for interrupt transfer\n");
        return -1;
    }
    
    serial_puts("[OHCI] Interrupt transfer: endpoint=0x");
    serial_puts_num_hex(endpoint);
    serial_puts("\n");
    
    // Находим endpoint
    uint8_t direction = endpoint & 0x80;
    usb_endpoint_t* ep = NULL;
    
    for (uint8_t i = 0; i < dev->num_interfaces; i++) {
        for (uint8_t j = 0; j < dev->interfaces[i].num_endpoints; j++) {
            if (dev->interfaces[i].endpoints[j].address == endpoint) {
                ep = &dev->interfaces[i].endpoints[j];
                break;
            }
        }
        if (ep) break;
    }
    
    if (!ep) {
        serial_puts("[OHCI] ERROR: Endpoint not found\n");
        return -1;
    }
    
    // Используем временные буферы
    ohci_td_t* td = (ohci_td_t*)kmalloc(sizeof(ohci_td_t));
    ohci_qh_t* qh = (ohci_qh_t*)kmalloc(sizeof(ohci_qh_t));
    uint8_t* temp_buffer = (uint8_t*)kmalloc(length);
    
    if (!td || !qh || !temp_buffer) {
        if (td) kfree(td);
        if (qh) kfree(qh);
        if (temp_buffer) kfree(temp_buffer);
        serial_puts("[OHCI] ERROR: Out of memory for transfer\n");
        return -1;
    }
    
    memset(td, 0, sizeof(ohci_td_t));
    memset(qh, 0, sizeof(ohci_qh_t));
    
    // Flags
    td->flags = ((direction == USB_ENDPOINT_IN ? 1 : 0) << 18);
    td->flags |= (0 << 19); // Задержка
    td->flags |= (3 << 21); // 3 ошибки
    td->flags |= (0 << 24); // Condition Code = Not Accessed
    td->flags |= (ep->toggle << 26); // Data Toggle
    
    // Buffer End
    td->td_buffer_end = (uint32_t)temp_buffer + length - 1;
    
    // Next TD
    td->next_td = 0x00000001; // Терминируем
    
    // Buffer Start
    td->buffer_start = (uint32_t)temp_buffer;
    
    // Копируем данные для OUT
    if (direction == USB_ENDPOINT_OUT && length > 0) {
        memcpy(temp_buffer, buffer, length);
    }
    
    // Инициализируем QH
    qh->head_td = (uint32_t)td;
    qh->tail_td = (uint32_t)td;
    qh->next_qh = 0x00000001;
    
    // Запускаем передачу
    ohci_write_reg(OHCI_HCCONTROLHEADED, (uint32_t)qh);
    
    // Ждем завершения
    uint32_t start_time = 0;
    int result = -1;
    
    while (start_time < timeout_ms * 1000) {
        uint32_t condition_code = (td->flags >> 24) & 0x0F;
        
        if (condition_code != 0) {
            if (condition_code == 1) {
                // Успех
                result = length;
                
                // Копируем данные для IN
                if (direction == USB_ENDPOINT_IN && length > 0) {
                    memcpy(buffer, temp_buffer, length);
                }
                
                // Переключаем data toggle
                ep->toggle ^= 1;
            } else {
                serial_puts("[OHCI] Interrupt TD error\n");
            }
            break;
        }
        
        start_time++;
        ohci_delay_us(10);
    }
    
    // Освобождаем временные буферы
    kfree(td);
    kfree(qh);
    kfree(temp_buffer);
    
    if (result < 0) {
        serial_puts("[OHCI] Interrupt transfer timeout\n");
    }
    
    return result;
}

// Инициализация OHCI контроллера
void ohci_init(uint32_t base) {
    serial_puts("[OHCI] Initializing at 0x");
    serial_puts_num_hex(base);
    serial_puts("\n");
    
    ohci_base = base;
    
    // 0. Проверим доступность контроллера
    if (base == 0 || base == 0xFFFFFFFF) {
        serial_puts("[OHCI] ERROR: Invalid base address\n");
        return;
    }
    
    uint32_t test = ohci_read_reg(OHCI_HCREVISION);
    serial_puts("[OHCI] Revision: 0x");
    serial_puts_num_hex(test);
    serial_puts("\n");
    
    if (test == 0xFFFFFFFF || test == 0) {
        serial_puts("[OHCI] ERROR: Controller not accessible!\n");
        return;
    }
    
    // 1. Инициализируем структуры
    if (!ohci_init_structures()) {
        serial_puts("[OHCI] ERROR: Failed to init structures\n");
        return;
    }
    
    // 2. Останавливаем контроллер
    serial_puts("[OHCI] Stopping controller...\n");
    ohci_write_reg(OHCI_HCCONTROL, 0);
    ohci_delay_ms(10);
    
    // 3. Сброс контроллера
    serial_puts("[OHCI] Resetting controller...\n");
    uint32_t control = ohci_read_reg(OHCI_HCCONTROL);
    control |= (1 << 0); // HostControllerReset бит
    ohci_write_reg(OHCI_HCCONTROL, control);
    
    ohci_delay_ms(50);
    
    // 4. Ждем сброса
    serial_puts("[OHCI] Waiting for reset...\n");
    uint32_t timeout = 10000;
    
    while (timeout-- && (ohci_read_reg(OHCI_HCCONTROL) & (1 << 0))) {
        ohci_delay_us(100);
    }
    
    if (timeout == 0) {
        serial_puts("[OHCI] ERROR: Reset timeout! Skipping OHCI.\n");
        ohci_free_structures();
        return;
    }
    
    serial_puts("[OHCI] Reset complete\n");
    
    // 5. Настраиваем HCCA
    ohci_write_reg(OHCI_HCHCCA, (uint32_t)hcca);
    
    // 6. Читаем дескриптор root hub
    uint32_t rh_desc_a = ohci_read_reg(OHCI_HCRHDESCRIPTORA);
    ohci_ports = (rh_desc_a >> 0) & 0xFF;
    
    serial_puts("[OHCI] Root Hub ports: ");
    serial_puts_num(ohci_ports);
    serial_puts("\n");
    
    if (ohci_ports == 0 || ohci_ports > 15) {
        serial_puts("[OHCI] WARNING: Invalid port count\n");
        ohci_ports = 2;
    }
    
    // 7. Включаем питание портов
    serial_puts("[OHCI] Powering ports...\n");
    for (uint8_t port = 1; port <= ohci_ports; port++) {
        uint32_t port_reg;
        
        if (port <= 1) {
            port_reg = OHCI_HCRHPORTSTATUS1;
        } else {
            port_reg = OHCI_HCRHPORTSTATUS2;
        }
        
        uint32_t port_status = ohci_read_reg(port_reg);
        
        // Включаем питание (бит 8)
        port_status |= (1 << 8);
        ohci_write_reg(port_reg, port_status);
        
        ohci_delay_ms(20);
        
        serial_puts("[OHCI] Port ");
        serial_puts_num(port);
        serial_puts(" powered\n");
    }
    
    // 8. Запускаем контроллер
    serial_puts("[OHCI] Starting controller...\n");
    control = ohci_read_reg(OHCI_HCCONTROL);
    control |= (1 << 6); // Operational бит
    control |= (1 << 5); // Control List Enabled
    control |= (1 << 4); // Bulk List Enabled
    ohci_write_reg(OHCI_HCCONTROL, control);
    
    ohci_delay_ms(10);
    
    // 9. Проверяем статус
    uint32_t status = ohci_read_reg(OHCI_HCCONTROL);
    if (status & (1 << 6)) {
        ohci_initialized = 1;
        serial_puts("[OHCI] Initialization SUCCESSFUL\n");
    } else {
        serial_puts("[OHCI] ERROR: Controller not operational\n");
        ohci_free_structures();
    }
}

// Обнаружение устройств OHCI
uint8_t ohci_detect_devices(void) {
    if (!ohci_initialized) {
        serial_puts("[OHCI] Cannot detect: controller not initialized\n");
        return 0;
    }
    
    serial_puts("[OHCI] Scanning for devices...\n");
    
    // Читаем дескриптор root hub
    uint32_t rh_desc_a = ohci_read_reg(OHCI_HCRHDESCRIPTORA);
    uint8_t num_ports = (rh_desc_a >> 0) & 0xFF;
    
    if (num_ports == 0) {
        serial_puts("[OHCI] No ports available\n");
        return 0;
    }
    
    uint8_t device_count = 0;
    
    for (uint8_t port = 1; port <= num_ports; port++) {
        uint32_t port_reg;
        
        if (port <= 1) {
            port_reg = OHCI_HCRHPORTSTATUS1;
        } else {
            port_reg = OHCI_HCRHPORTSTATUS2;
        }
        
        uint32_t port_status = ohci_read_reg(port_reg);
        
        serial_puts("[OHCI] Port ");
        serial_puts_num(port);
        serial_puts(" status: 0x");
        serial_puts_num_hex(port_status);
        
        // Проверяем бит Connected Status Change (бит 0)
        if (port_status & (1 << 0)) {
            serial_puts(" [DEVICE CONNECTED]");
            
            // Очищаем флаг подключения
            port_status |= (1 << 0); // Запись 1 очищает бит
            ohci_write_reg(port_reg, port_status);
            
            // Проверяем Current Connect Status (бит 1)
            if (port_status & (1 << 1)) {
                serial_puts(" [STILL CONNECTED]");
                
                // Включаем порт (бит 2)
                port_status |= (1 << 2); // PortEnable
                ohci_write_reg(port_reg, port_status);
                ohci_delay_ms(10);
                
                // Добавляем устройство (предполагаем Full Speed для OHCI)
                _usb_add_device(port - 1, USB_SPEED_FULL, 1, "OHCI Device");
                device_count++;
            }
        } else {
            serial_puts(" [NO DEVICE]");
        }
        
        serial_puts("\n");
    }
    
    serial_puts("[OHCI] Found ");
    serial_puts_num(device_count);
    serial_puts(" device(s)\n");
    
    return device_count;
}

// Опрос OHCI
void ohci_poll(void) {
    if (!ohci_initialized) return;
    // Заглушка пока
}

// Проверка наличия OHCI
uint8_t ohci_is_present(void) {
    return ohci_initialized;
}