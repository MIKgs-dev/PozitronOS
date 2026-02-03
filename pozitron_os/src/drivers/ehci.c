#include "drivers/ehci.h"
#include "drivers/serial.h"
#include "drivers/ports.h"
#include "drivers/usb.h"
#include "drivers/timer.h"
#include <stddef.h>

// Глобальные переменные EHCI
static uint32_t ehci_cap_base = 0;
static uint32_t ehci_op_base = 0;
static uint8_t ehci_initialized = 0;
static uint8_t ehci_ports = 0;

// Структуры данных EHCI
typedef struct {
    uint32_t next_qtd;
    uint32_t alt_next_qtd;
    uint32_t token;
    uint32_t buffer[5];
    uint32_t ext_buffer[5];
} __attribute__((packed)) ehci_qtd_t;

typedef struct {
    uint32_t horiz_link;
    uint32_t charac;
    uint32_t caps;
    uint32_t curr_qtd;
    uint32_t next_qtd;
    uint32_t alt_next_qtd;
    uint32_t token;
    uint32_t buffer[5];
    uint32_t ext_buffer[5];
    uint32_t reserved[3];
} __attribute__((packed)) ehci_qh_t;

// Статические буферы
static ehci_qtd_t* control_qtd = NULL;
static ehci_qh_t* control_qh = NULL;
static ehci_qh_t* async_qh = NULL;
static uint8_t* setup_buffer = NULL;
static uint8_t* data_buffer = NULL;

// СВОИ РЕАЛИЗАЦИИ mem* функций для EHCI
static void ehci_memset(void* ptr, uint8_t value, uint32_t size) {
    uint8_t* p = (uint8_t*)ptr;
    for (uint32_t i = 0; i < size; i++) {
        p[i] = value;
    }
}

static void ehci_memcpy(void* dst, const void* src, uint32_t size) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    for (uint32_t i = 0; i < size; i++) {
        d[i] = s[i];
    }
}

// Вспомогательные функции
static uint32_t ehci_read_cap_reg(uint32_t reg) {
    return inl(ehci_cap_base + reg);
}

static uint32_t ehci_read_op_reg(uint32_t reg) {
    return inl(ehci_op_base + reg);
}

static void ehci_write_op_reg(uint32_t reg, uint32_t value) {
    outl(ehci_op_base + reg, value);
}

static void ehci_delay_ms(uint32_t milliseconds) {
    timer_sleep_ms(milliseconds);
}

// Аллокация памяти для EHCI
static void* ehci_alloc_align(uint32_t size, uint32_t align) {
    static uint8_t mem_pool[16384];
    static uint32_t mem_offset = 0;
    
    uint32_t aligned_offset = (mem_offset + align - 1) & ~(align - 1);
    
    if (aligned_offset + size > sizeof(mem_pool)) {
        serial_puts("[EHCI] ERROR: Memory pool exhausted\n");
        return NULL;
    }
    
    void* ptr = &mem_pool[aligned_offset];
    mem_offset = aligned_offset + size;
    
    return ptr;
}

// Инициализация структур EHCI
static int ehci_init_structures(void) {
    // QH должен быть выровнен по 32 байта
    control_qh = (ehci_qh_t*)ehci_alloc_align(sizeof(ehci_qh_t), 32);
    async_qh = (ehci_qh_t*)ehci_alloc_align(sizeof(ehci_qh_t), 32);
    control_qtd = (ehci_qtd_t*)ehci_alloc_align(sizeof(ehci_qtd_t) * 4, 32);
    setup_buffer = (uint8_t*)ehci_alloc_align(8, 4);
    data_buffer = (uint8_t*)ehci_alloc_align(USB_MAX_PACKET_SIZE, 4);
    
    if (!control_qh || !async_qh || !control_qtd || !setup_buffer || !data_buffer) {
        serial_puts("[EHCI] ERROR: Failed to allocate structures\n");
        return 0;
    }
    
    // Инициализируем Control QH
    ehci_memset(control_qh, 0, sizeof(ehci_qh_t));
    control_qh->horiz_link = 0x00000001; // Терминируем
    control_qh->charac = (1 << 15) | (1 << 12); // Head of Reclamation List, Control endpoint
    
    // Инициализируем Async QH
    ehci_memset(async_qh, 0, sizeof(ehci_qh_t));
    async_qh->horiz_link = 0x00000001;
    async_qh->charac = (1 << 15); // Head of Reclamation List
    
    serial_puts("[EHCI] Structures initialized\n");
    return 1;
}

// Создание Setup QTD
static ehci_qtd_t* create_setup_qtd(ehci_qtd_t* next_qtd, uint8_t* setup_data,
                                   uint8_t device_addr, uint8_t endpoint,
                                   uint8_t max_packet_size) {
    if (!control_qtd) return NULL;
    
    ehci_qtd_t* qtd = &control_qtd[0];
    ehci_memset(qtd, 0, sizeof(ehci_qtd_t));
    
    // Next QTD
    qtd->next_qtd = next_qtd ? (uint32_t)next_qtd : 0x00000001;
    
    // Alt Next QTD
    qtd->alt_next_qtd = 0x00000001;
    
    // Token
    qtd->token = (0 << 0); // Total Bytes = 0 для SETUP
    qtd->token |= (0 << 8); // IOC = 0
    qtd->token |= (1 << 9); // C_PAGE = 1
    qtd->token |= (0 << 10); // ERR_CNT = 0
    qtd->token |= (0 << 12); // PID = SETUP
    qtd->token |= (0 << 14); // Status = Active
    qtd->token |= (0 << 16); // Data Toggle = 0
    qtd->token |= (max_packet_size << 16); // Max Packet Length
    
    // Buffer Pointer
    qtd->buffer[0] = (uint32_t)setup_buffer;
    
    // Копируем setup данные
    if (setup_data) {
        ehci_memcpy(setup_buffer, setup_data, 8);
    }
    
    return qtd;
}

// Создание Data QTD
static ehci_qtd_t* create_data_qtd(ehci_qtd_t* next_qtd, uint8_t* data, uint16_t length,
                                  uint8_t pid, uint8_t device_addr, uint8_t endpoint,
                                  uint8_t max_packet_size, uint8_t data_toggle) {
    if (!control_qtd) return NULL;
    
    ehci_qtd_t* qtd = &control_qtd[1];
    ehci_memset(qtd, 0, sizeof(ehci_qtd_t));
    
    // Next QTD
    qtd->next_qtd = next_qtd ? (uint32_t)next_qtd : 0x00000001;
    
    // Alt Next QTD
    qtd->alt_next_qtd = 0x00000001;
    
    // Token
    qtd->token = ((length - 1) << 0); // Total Bytes
    qtd->token |= (0 << 8); // IOC = 0
    qtd->token |= (1 << 9); // C_PAGE = 1
    qtd->token |= (0 << 10); // ERR_CNT = 0
    qtd->token |= (pid << 12); // PID
    qtd->token |= (0 << 14); // Status = Active
    qtd->token |= (data_toggle << 16); // Data Toggle
    qtd->token |= (max_packet_size << 16); // Max Packet Length
    
    // Buffer Pointer
    qtd->buffer[0] = (uint32_t)data_buffer;
    
    // Копируем данные для OUT
    if (pid == 1 && data && length > 0) { // OUT PID = 1
        ehci_memcpy(data_buffer, data, length);
    }
    
    return qtd;
}

// Создание Status QTD
static ehci_qtd_t* create_status_qtd(ehci_qtd_t* next_qtd, uint8_t pid,
                                    uint8_t device_addr, uint8_t endpoint) {
    if (!control_qtd) return NULL;
    
    ehci_qtd_t* qtd = &control_qtd[2];
    ehci_memset(qtd, 0, sizeof(ehci_qtd_t));
    
    // Next QTD
    qtd->next_qtd = next_qtd ? (uint32_t)next_qtd : 0x00000001;
    
    // Alt Next QTD
    qtd->alt_next_qtd = 0x00000001;
    
    // Token
    qtd->token = (0 << 0); // Total Bytes = 1
    qtd->token |= (0 << 8); // IOC = 0
    qtd->token |= (0 << 9); // C_PAGE = 0
    qtd->token |= (0 << 10); // ERR_CNT = 0
    qtd->token |= (pid << 12); // PID
    qtd->token |= (0 << 14); // Status = Active
    qtd->token |= (1 << 16); // Data Toggle = 1
    
    // Buffer Pointer
    qtd->buffer[0] = 0;
    
    return qtd;
}

// Ожидание завершения QTD
static int wait_for_qtd_completion(ehci_qtd_t* qtd, uint32_t timeout_ms) {
    uint32_t start_ticks = timer_get_ticks();
    
    while (timer_get_ticks() - start_ticks < timeout_ms) {
        uint32_t status = (qtd->token >> 14) & 0x03;
        
        if (status != 0) {
            // QTD обработан
            if (status == 1) {
                return 0; // Inactive (успех)
            } else {
                serial_puts("[EHCI] QTD error status: ");
                serial_puts_num(status);
                serial_puts("\n");
                return -1;
            }
        }
        ehci_delay_ms(1);
    }
    
    serial_puts("[EHCI] QTD timeout\n");
    return -1;
}

// Контрольная транзакция EHCI
int ehci_control_transfer(uint8_t controller_idx,
                         usb_device_t* dev,
                         uint8_t bmRequestType,
                         uint8_t bRequest,
                         uint16_t wValue,
                         uint16_t wIndex,
                         uint16_t wLength,
                         void* data) {
    if (!dev || !ehci_initialized) {
        serial_puts("[EHCI] ERROR: Controller not initialized\n");
        return -1;
    }
    
    if (!control_qh || !control_qtd) {
        serial_puts("[EHCI] ERROR: Structures not initialized\n");
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
    
    serial_puts("[EHCI] Control transfer: addr=");
    serial_puts_num(dev->address);
    serial_puts(" req=0x");
    serial_puts_num_hex(bRequest);
    serial_puts("\n");
    
    // Определяем направление
    uint8_t data_pid;
    uint8_t status_pid;
    
    if (wLength > 0 && (bmRequestType & 0x80)) {
        // IN transfer
        data_pid = 2; // IN PID = 2
        status_pid = 1; // OUT PID = 1
    } else if (wLength > 0) {
        // OUT transfer
        data_pid = 1; // OUT PID = 1
        status_pid = 2; // IN PID = 2
    } else {
        // No data stage
        data_pid = 0;
        status_pid = 2; // IN PID = 2
    }
    
    // Создаем QTD цепочку
    ehci_qtd_t* status_qtd = create_status_qtd(NULL, status_pid,
                                              dev->address, 0);
    if (!status_qtd) return -1;
    
    ehci_qtd_t* data_qtd = NULL;
    if (wLength > 0) {
        data_qtd = create_data_qtd(status_qtd, data, wLength,
                                  data_pid, dev->address, 0,
                                  dev->max_packet_size, dev->interfaces[0].endpoints[0].toggle);
        if (!data_qtd) return -1;
    }
    
    ehci_qtd_t* setup_qtd = create_setup_qtd(data_qtd ? data_qtd : status_qtd,
                                            (uint8_t*)&setup, dev->address, 0,
                                            dev->max_packet_size);
    if (!setup_qtd) return -1;
    
    // Настраиваем QH
    control_qh->curr_qtd = (uint32_t)setup_qtd;
    control_qh->next_qtd = (uint32_t)setup_qtd;
    control_qh->alt_next_qtd = (uint32_t)status_qtd;
    
    // Настраиваем Async Schedule
    async_qh->horiz_link = (uint32_t)control_qh | 0x00000002; // QH тип
    
    // Запускаем Async Schedule
    ehci_write_op_reg(0x20, (uint32_t)async_qh); // ASYNCLISTADDR
    
    // Ждем завершения
    int result = 0;
    
    // Setup stage
    if (wait_for_qtd_completion(setup_qtd, 100) < 0) {
        serial_puts("[EHCI] Setup stage failed\n");
        result = -1;
        goto cleanup;
    }
    
    // Data stage
    if (data_qtd && wLength > 0) {
        if (wait_for_qtd_completion(data_qtd, 100) < 0) {
            serial_puts("[EHCI] Data stage failed\n");
            result = -1;
            goto cleanup;
        }
        
        // Если это IN transfer, копируем данные
        if ((bmRequestType & 0x80) && data) {
            ehci_memcpy(data, data_buffer, wLength);
        }
        
        // Переключаем data toggle
        dev->interfaces[0].endpoints[0].toggle ^= 1;
    }
    
    // Status stage
    if (wait_for_qtd_completion(status_qtd, 100) < 0) {
        serial_puts("[EHCI] Status stage failed\n");
        result = -1;
        goto cleanup;
    }
    
    result = wLength;
    
cleanup:
    // Останавливаем Async Schedule
    ehci_write_op_reg(0x20, 0);
    
    // Сбрасываем QH
    control_qh->curr_qtd = 0;
    control_qh->next_qtd = 0;
    control_qh->alt_next_qtd = 0;
    
    return result;
}

// Прерывающая транзакция EHCI
int ehci_interrupt_transfer(uint8_t controller_idx,
                           usb_device_t* dev,
                           uint8_t endpoint,
                           void* buffer,
                           uint16_t length,
                           uint32_t timeout_ms) {
    if (!dev || !ehci_initialized || !buffer) {
        serial_puts("[EHCI] ERROR: Invalid parameters for interrupt transfer\n");
        return -1;
    }
    
    serial_puts("[EHCI] Interrupt transfer: endpoint=0x");
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
        serial_puts("[EHCI] ERROR: Endpoint not found\n");
        return -1;
    }
    
    // Создаем QTD
    ehci_qtd_t qtd;
    ehci_memset(&qtd, 0, sizeof(ehci_qtd_t));
    
    // Next QTD
    qtd.next_qtd = 0x00000001;
    
    // Alt Next QTD
    qtd.alt_next_qtd = 0x00000001;
    
    // Token
    qtd.token = ((length - 1) << 0); // Total Bytes
    qtd.token |= (1 << 8); // IOC = 1 для прерываний
    qtd.token |= (1 << 9); // C_PAGE = 1
    qtd.token |= (0 << 10); // ERR_CNT = 0
    qtd.token |= ((direction == USB_ENDPOINT_IN ? 2 : 1) << 12); // PID
    qtd.token |= (0 << 14); // Status = Active
    qtd.token |= (ep->toggle << 16); // Data Toggle
    qtd.token |= (ep->max_packet_size << 16); // Max Packet Length
    
    // Buffer Pointer
    qtd.buffer[0] = (uint32_t)data_buffer;
    
    // Копируем данные для OUT
    if (direction == USB_ENDPOINT_OUT && length > 0) {
        ehci_memcpy(data_buffer, buffer, length);
    }
    
    // Создаем временный QH
    ehci_qh_t qh;
    ehci_memset(&qh, 0, sizeof(ehci_qh_t));
    qh.horiz_link = 0x00000001;
    qh.curr_qtd = (uint32_t)&qtd;
    qh.next_qtd = (uint32_t)&qtd;
    qh.alt_next_qtd = (uint32_t)&qtd;
    
    // Запускаем передачу
    async_qh->horiz_link = (uint32_t)&qh | 0x00000002;
    ehci_write_op_reg(0x20, (uint32_t)async_qh);
    
    // Ждем завершения
    uint32_t start_ticks = timer_get_ticks();
    int result = -1;
    
    while (timer_get_ticks() - start_ticks < timeout_ms) {
        uint32_t status = (qtd.token >> 14) & 0x03;
        
        if (status != 0) {
            if (status == 1) {
                // Успех
                result = length;
                
                // Копируем данные для IN
                if (direction == USB_ENDPOINT_IN && length > 0) {
                    ehci_memcpy(buffer, data_buffer, length);
                }
                
                // Переключаем data toggle
                ep->toggle ^= 1;
            } else {
                serial_puts("[EHCI] Interrupt QTD error\n");
            }
            break;
        }
        ehci_delay_ms(1);
    }
    
    // Останавливаем передачу
    ehci_write_op_reg(0x20, 0);
    
    if (result < 0) {
        serial_puts("[EHCI] Interrupt transfer timeout\n");
    }
    
    return result;
}

// Инициализация EHCI контроллера
void ehci_init(uint32_t cap_base, uint32_t op_base) {
    serial_puts("[EHCI] Initializing: CAP=0x");
    serial_puts_num_hex(cap_base);
    serial_puts(", OP=0x");
    serial_puts_num_hex(op_base);
    serial_puts("\n");
    
    ehci_cap_base = cap_base;
    ehci_op_base = op_base;
    
    // 1. Инициализируем структуры
    if (!ehci_init_structures()) {
        serial_puts("[EHCI] ERROR: Failed to init structures\n");
        return;
    }
    
    // 2. Получаем количество портов
    uint32_t hcsparams = ehci_read_cap_reg(EHCI_HCSPARAMS);
    ehci_ports = (hcsparams >> 0) & 0x0F;
    
    serial_puts("[EHCI] Ports: ");
    serial_puts_num(ehci_ports);
    serial_puts("\n");
    
    // 3. Останавливаем контроллер
    ehci_write_op_reg(EHCI_USBCMD, 0);
    ehci_delay_ms(10);
    
    // 4. Ждем остановки
    uint32_t timeout = 1000;
    while (timeout-- && !(ehci_read_op_reg(EHCI_USBSTS) & EHCI_STS_HALTED)) {
        ehci_delay_ms(1);
    }
    
    if (timeout == 0) {
        serial_puts("[EHCI] WARNING: Could not stop controller\n");
        return;
    }
    
    // 5. Сброс контроллера
    ehci_write_op_reg(EHCI_USBCMD, EHCI_CMD_RESET);
    ehci_delay_ms(50);
    
    // Ждем сброса
    timeout = 1000;
    while (timeout-- && (ehci_read_op_reg(EHCI_USBCMD) & EHCI_CMD_RESET)) {
        ehci_delay_ms(1);
    }
    
    if (timeout == 0) {
        serial_puts("[EHCI] ERROR: Reset timeout\n");
        return;
    }
    
    // 6. Включаем порты
    for (uint8_t port = 0; port < ehci_ports; port++) {
        uint32_t portsc = ehci_read_op_reg(EHCI_PORTSC + (port * 4));
        
        // Включаем питание
        if (!(portsc & EHCI_PORT_POWER)) {
            portsc |= EHCI_PORT_POWER;
            ehci_write_op_reg(EHCI_PORTSC + (port * 4), portsc);
            ehci_delay_ms(20);
        }
    }
    
    // 7. Включаем Async Schedule
    ehci_write_op_reg(EHCI_USBCMD, EHCI_CMD_RUN | EHCI_CMD_ASYNC_EN);
    ehci_delay_ms(10);
    
    // 8. Проверяем статус
    uint32_t status = ehci_read_op_reg(EHCI_USBSTS);
    if (status & EHCI_STS_HALTED) {
        serial_puts("[EHCI] ERROR: Controller halted\n");
        return;
    }
    
    ehci_initialized = 1;
    serial_puts("[EHCI] Initialization successful\n");
}

// Обнаружение устройств EHCI
uint8_t ehci_detect_devices(void) {
    if (!ehci_initialized) {
        serial_puts("[EHCI] Cannot detect: controller not initialized\n");
        return 0;
    }
    
    serial_puts("[EHCI] Detecting devices...\n");
    
    uint8_t device_count = 0;
    
    for (uint8_t port = 0; port < ehci_ports; port++) {
        uint32_t portsc = ehci_read_op_reg(EHCI_PORTSC + (port * 4));
        
        serial_puts("[EHCI] Port ");
        serial_puts_num(port);
        serial_puts(": 0x");
        serial_puts_num_hex(portsc);
        
        if (portsc & EHCI_PORT_CONNECT) {
            serial_puts(" [CONNECTED]");
            
            // Определяем скорость
            uint32_t speed = (portsc >> 26) & 0x03;
            usb_speed_t usb_speed = USB_SPEED_FULL;
            
            switch(speed) {
                case 0: usb_speed = USB_SPEED_HIGH; break;
                case 1: usb_speed = USB_SPEED_FULL; break;
                case 2: usb_speed = USB_SPEED_LOW; break;
                default: usb_speed = USB_SPEED_FULL; break;
            }
            
            // Добавляем устройство (controller_idx = 2 для EHCI)
            _usb_add_device(port, usb_speed, 2, "EHCI Device");
            device_count++;
            
            // Сброс порта если не сброшен
            if (!(portsc & EHCI_PORT_RESET)) {
                portsc |= EHCI_PORT_RESET;
                ehci_write_op_reg(EHCI_PORTSC + (port * 4), portsc);
                ehci_delay_ms(50);
                
                portsc &= ~EHCI_PORT_RESET;
                ehci_write_op_reg(EHCI_PORTSC + (port * 4), portsc);
                ehci_delay_ms(20);
            }
        } else {
            serial_puts(" [DISCONNECTED]");
        }
        
        serial_puts("\n");
    }
    
    serial_puts("[EHCI] Found ");
    serial_puts_num(device_count);
    serial_puts(" device(s)\n");
    
    return device_count;
}

// Опрос EHCI
void ehci_poll(void) {
    if (!ehci_initialized) return;
    
    // Проверяем состояние портов
    for (uint8_t port = 0; port < ehci_ports; port++) {
        uint32_t portsc = ehci_read_op_reg(EHCI_PORTSC + (port * 4));
        // Можно добавить логику обнаружения изменений
        (void)portsc;
    }
}

// Проверка наличия EHCI
uint8_t ehci_is_present(void) {
    return ehci_initialized;
}