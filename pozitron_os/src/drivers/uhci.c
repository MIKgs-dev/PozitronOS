#include "drivers/uhci.h"
#include "drivers/serial.h"
#include "drivers/ports.h"
#include "drivers/usb.h"
#include "drivers/timer.h"
#include <stddef.h>

// Глобальные переменные UHCI
static uint32_t uhci_base = 0;
static uint8_t uhci_initialized = 0;
static uint8_t uhci_ports = 2;

// Структуры данных UHCI
typedef struct {
    uint32_t link_pointer;
    uint32_t status_control;
    uint32_t token;
    uint32_t buffer_pointer;
} __attribute__((packed)) uhci_td_t;

typedef struct {
    uint32_t link_pointer;
    uint32_t element_pointer;
} __attribute__((packed)) uhci_qh_t;

// Статические буферы для дескрипторов
static uhci_td_t* control_td = NULL;
static uhci_qh_t* control_qh = NULL;
static uint8_t* setup_buffer = NULL;
static uint8_t* data_buffer = NULL;

// СВОИ РЕАЛИЗАЦИИ mem* функций для UHCI
static void uhci_memset(void* ptr, uint8_t value, uint32_t size) {
    uint8_t* p = (uint8_t*)ptr;
    for (uint32_t i = 0; i < size; i++) {
        p[i] = value;
    }
}

static void uhci_memcpy(void* dst, const void* src, uint32_t size) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    for (uint32_t i = 0; i < size; i++) {
        d[i] = s[i];
    }
}

// Вспомогательные функции
static uint16_t uhci_read_reg(uint16_t reg) {
    return inw(uhci_base + reg);
}

static void uhci_write_reg(uint16_t reg, uint16_t value) {
    outw(uhci_base + reg, value);
}

// Задержка
static void uhci_delay_ms(uint32_t milliseconds) {
    timer_sleep_ms(milliseconds);
}

// Аллокация памяти для структур UHCI
static void* uhci_alloc_align(uint32_t size, uint32_t align) {
    // Простая аллокация - в реальной ОС нужно использовать memory_alloc
    static uint8_t mem_pool[8192];
    static uint32_t mem_offset = 0;
    
    uint32_t aligned_offset = (mem_offset + align - 1) & ~(align - 1);
    
    if (aligned_offset + size > sizeof(mem_pool)) {
        serial_puts("[UHCI] ERROR: Memory pool exhausted\n");
        return NULL;
    }
    
    void* ptr = &mem_pool[aligned_offset];
    mem_offset = aligned_offset + size;
    
    return ptr;
}

// Инициализация структур UHCI
static int uhci_init_structures(void) {
    // Аллоцируем память для структур
    control_td = (uhci_td_t*)uhci_alloc_align(sizeof(uhci_td_t) * 4, 16);
    control_qh = (uhci_qh_t*)uhci_alloc_align(sizeof(uhci_qh_t), 16);
    setup_buffer = (uint8_t*)uhci_alloc_align(8, 4);
    data_buffer = (uint8_t*)uhci_alloc_align(USB_MAX_PACKET_SIZE, 4);
    
    if (!control_td || !control_qh || !setup_buffer || !data_buffer) {
        serial_puts("[UHCI] ERROR: Failed to allocate structures\n");
        return 0;
    }
    
    // Инициализируем Queue Head
    uhci_memset(control_qh, 0, sizeof(uhci_qh_t));
    control_qh->link_pointer = 0x00000001; // Терминируем
    
    serial_puts("[UHCI] Structures initialized\n");
    return 1;
}

// Создание Setup TD
static uhci_td_t* create_setup_td(uint32_t next_td, uint8_t* setup_data, 
                                 uint8_t device_addr, uint8_t endpoint, 
                                 uint8_t max_packet_size) {
    if (!control_td) return NULL;
    
    uhci_td_t* td = &control_td[0];
    uhci_memset(td, 0, sizeof(uhci_td_t));
    
    // Link Pointer
    td->link_pointer = next_td | 0x00000002; // Указывает на следующий TD, Depth First
    
    // Status/Control
    td->status_control = (1 << 23); // Активный
    td->status_control |= (3 << 19); // 3 ошибки
    
    // Token
    td->token = (0x2D << 21); // SETUP pid
    td->token |= (endpoint << 15);
    td->token |= (device_addr << 8);
    td->token |= (7 << 0); // Длина setup пакета = 8 байт
    
    // Buffer Pointer
    td->buffer_pointer = (uint32_t)setup_buffer;
    
    // Копируем setup данные
    if (setup_data) {
        uhci_memcpy(setup_buffer, setup_data, 8);
    }
    
    return td;
}

// Создание Data TD
static uhci_td_t* create_data_td(uint32_t next_td, uint8_t* data, uint16_t length,
                                uint8_t pid, uint8_t device_addr, uint8_t endpoint,
                                uint8_t max_packet_size, uint8_t data_toggle) {
    if (!control_td) return NULL;
    
    uhci_td_t* td = &control_td[1];
    uhci_memset(td, 0, sizeof(uhci_td_t));
    
    // Link Pointer
    td->link_pointer = next_td | 0x00000002;
    
    // Status/Control
    td->status_control = (1 << 23); // Активный
    td->status_control |= (3 << 19); // 3 ошибки
    if (data_toggle) {
        td->status_control |= (1 << 18); // Data Toggle = 1
    }
    
    // Token
    td->token = (pid << 21);
    td->token |= (endpoint << 15);
    td->token |= (device_addr << 8);
    td->token |= ((length - 1) << 0); // Длина
    
    // Buffer Pointer
    td->buffer_pointer = (uint32_t)data_buffer;
    
    // Копируем данные
    if (data && length > 0) {
        uhci_memcpy(data_buffer, data, length);
    }
    
    return td;
}

// Создание Status TD
static uhci_td_t* create_status_td(uint32_t next_td, uint8_t pid,
                                  uint8_t device_addr, uint8_t endpoint) {
    if (!control_td) return NULL;
    
    uhci_td_t* td = &control_td[2];
    uhci_memset(td, 0, sizeof(uhci_td_t));
    
    // Link Pointer
    td->link_pointer = next_td | 0x00000002;
    
    // Status/Control
    td->status_control = (1 << 23); // Активный
    td->status_control |= (3 << 19); // 3 ошибки
    
    // Token
    td->token = (pid << 21);
    td->token |= (endpoint << 15);
    td->token |= (device_addr << 8);
    td->token |= (0 << 0); // Длина = 1
    
    // Buffer Pointer
    td->buffer_pointer = 0;
    
    return td;
}

// Ожидание завершения TD
static int wait_for_td_completion(uhci_td_t* td, uint32_t timeout_ms) {
    uint32_t start_ticks = timer_get_ticks();
    
    while (timer_get_ticks() - start_ticks < timeout_ms) {
        if (!(td->status_control & (1 << 23))) {
            // TD завершен
            if (td->status_control & (1 << 22)) {
                // Ошибка
                serial_puts("[UHCI] TD error: 0x");
                serial_puts_num_hex(td->status_control);
                serial_puts("\n");
                return -1;
            }
            return 0; // Успех
        }
        timer_sleep_ms(1);
    }
    
    serial_puts("[UHCI] TD timeout\n");
    return -1;
}

// Контрольная транзакция UHCI
int uhci_control_transfer(uint8_t controller_idx,
                         usb_device_t* dev,
                         uint8_t bmRequestType,
                         uint8_t bRequest,
                         uint16_t wValue,
                         uint16_t wIndex,
                         uint16_t wLength,
                         void* data) {
    if (!dev || !uhci_initialized) {
        serial_puts("[UHCI] ERROR: Controller not initialized\n");
        return -1;
    }
    
    if (!control_td || !control_qh) {
        serial_puts("[UHCI] ERROR: Structures not initialized\n");
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
    
    serial_puts("[UHCI] Control transfer: addr=");
    serial_puts_num(dev->address);
    serial_puts(" req=0x");
    serial_puts_num_hex(bRequest);
    serial_puts(" len=");
    serial_puts_num(wLength);
    serial_puts("\n");
    
    // Определяем направление
    uint8_t data_pid;
    uint8_t status_pid;
    
    if (wLength > 0 && (bmRequestType & 0x80)) {
        // IN transfer (device to host)
        data_pid = 0x69; // IN pid
        status_pid = 0x2D; // OUT pid для статуса
    } else if (wLength > 0) {
        // OUT transfer (host to device)
        data_pid = 0xE1; // OUT pid
        status_pid = 0x69; // IN pid для статуса
    } else {
        // No data stage
        data_pid = 0;
        status_pid = 0x69; // IN pid для статуса
    }
    
    // Создаем TD цепочку
    uhci_td_t* status_td = create_status_td(0x00000001, status_pid, 
                                           dev->address, 0);
    if (!status_td) return -1;
    
    uhci_td_t* data_td = NULL;
    if (wLength > 0) {
        data_td = create_data_td((uint32_t)status_td, data, wLength,
                                data_pid, dev->address, 0,
                                dev->max_packet_size, dev->interfaces[0].endpoints[0].toggle);
        if (!data_td) return -1;
    }
    
    uhci_td_t* setup_td = create_setup_td(data_td ? (uint32_t)data_td : (uint32_t)status_td,
                                         (uint8_t*)&setup, dev->address, 0,
                                         dev->max_packet_size);
    if (!setup_td) return -1;
    
    // Обновляем очередь
    control_qh->element_pointer = (uint32_t)setup_td | 0x00000002;
    
    // Ждем фрейма
    uint16_t frame_num = uhci_read_reg(UHCI_FRNUM);
    frame_num = (frame_num + 2) & 0x3FF;
    
    // Устанавливаем Frame List
    uhci_write_reg(UHCI_FLBASEADD, (uint32_t)control_qh);
    
    // Ждем завершения
    int result = 0;
    
    // Setup stage
    if (wait_for_td_completion(setup_td, 100) < 0) {
        serial_puts("[UHCI] Setup stage failed\n");
        result = -1;
        goto cleanup;
    }
    
    // Data stage (если есть)
    if (data_td && wLength > 0) {
        if (wait_for_td_completion(data_td, 100) < 0) {
            serial_puts("[UHCI] Data stage failed\n");
            result = -1;
            goto cleanup;
        }
        
        // Если это IN transfer, копируем данные
        if ((bmRequestType & 0x80) && data) {
            uhci_memcpy(data, data_buffer, wLength);
        }
        
        // Переключаем data toggle
        dev->interfaces[0].endpoints[0].toggle ^= 1;
    }
    
    // Status stage
    if (wait_for_td_completion(status_td, 100) < 0) {
        serial_puts("[UHCI] Status stage failed\n");
        result = -1;
        goto cleanup;
    }
    
    result = wLength;
    
cleanup:
    // Очищаем очередь
    control_qh->element_pointer = 0x00000001;
    
    return result;
}

// Прерывающая транзакция UHCI
int uhci_interrupt_transfer(uint8_t controller_idx,
                           usb_device_t* dev,
                           uint8_t endpoint,
                           void* buffer,
                           uint16_t length,
                           uint32_t timeout_ms) {
    if (!dev || !uhci_initialized || !buffer) {
        serial_puts("[UHCI] ERROR: Invalid parameters for interrupt transfer\n");
        return -1;
    }
    
    serial_puts("[UHCI] Interrupt transfer: endpoint=0x");
    serial_puts_num_hex(endpoint);
    serial_puts(" len=");
    serial_puts_num(length);
    serial_puts("\n");
    
    // Находим endpoint
    uint8_t ep_num = endpoint & 0x0F;
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
        serial_puts("[UHCI] ERROR: Endpoint not found\n");
        return -1;
    }
    
    // Создаем TD
    uhci_td_t td;
    uhci_memset(&td, 0, sizeof(uhci_td_t));
    
    // Link Pointer (терминируем)
    td.link_pointer = 0x00000001;
    
    // Status/Control
    td.status_control = (1 << 23); // Активный
    td.status_control |= (3 << 19); // 3 ошибки
    if (ep->toggle) {
        td.status_control |= (1 << 18); // Data Toggle
    }
    
    // Token
    if (direction == USB_ENDPOINT_IN) {
        td.token = (0x69 << 21); // IN pid
    } else {
        td.token = (0xE1 << 21); // OUT pid
    }
    
    td.token |= (ep_num << 15);
    td.token |= (dev->address << 8);
    td.token |= ((length - 1) << 0);
    
    // Buffer Pointer
    td.buffer_pointer = (uint32_t)data_buffer;
    
    // Копируем данные для OUT
    if (direction == USB_ENDPOINT_OUT && length > 0) {
        uhci_memcpy(data_buffer, buffer, length);
    }
    
    // Создаем временный QH
    uhci_qh_t qh;
    uhci_memset(&qh, 0, sizeof(uhci_qh_t));
    qh.link_pointer = 0x00000001;
    qh.element_pointer = (uint32_t)&td | 0x00000002;
    
    // Устанавливаем Frame List
    uhci_write_reg(UHCI_FLBASEADD, (uint32_t)&qh);
    
    // Ждем завершения
    uint32_t start_ticks = timer_get_ticks();
    int result = -1;
    
    while (timer_get_ticks() - start_ticks < timeout_ms) {
        if (!(td.status_control & (1 << 23))) {
            // TD завершен
            if (td.status_control & (1 << 22)) {
                // Ошибка
                serial_puts("[UHCI] Interrupt TD error\n");
                break;
            }
            
            // Успех
            result = length;
            
            // Копируем данные для IN
            if (direction == USB_ENDPOINT_IN && length > 0) {
                uhci_memcpy(buffer, data_buffer, length);
            }
            
            // Переключаем data toggle
            ep->toggle ^= 1;
            break;
        }
        timer_sleep_ms(1);
    }
    
    if (result < 0) {
        serial_puts("[UHCI] Interrupt transfer timeout\n");
    }
    
    return result;
}

// Инициализация UHCI контроллера
void uhci_init(uint32_t base) {
    serial_puts("[UHCI] Initializing at 0x");
    serial_puts_num_hex(base);
    serial_puts("\n");
    
    uhci_base = base;
    
    // 1. Останавливаем контроллер
    uhci_write_reg(UHCI_CMD, 0);
    uhci_delay_ms(10);
    
    // 2. Проверяем если уже запущен
    if (!(uhci_read_reg(UHCI_STS) & UHCI_STS_HCHALTED)) {
        serial_puts("[UHCI] Controller was running, stopping...\n");
        uhci_write_reg(UHCI_CMD, 0);
        uhci_delay_ms(100);
    }
    
    // 3. Сброс контроллера
    uhci_write_reg(UHCI_CMD, UHCI_CMD_HCRESET);
    uhci_delay_ms(50);
    
    // 4. Сброс глобальный
    uhci_write_reg(UHCI_CMD, UHCI_CMD_GRESET);
    uhci_delay_ms(50);
    
    // 5. Очищаем статус
    uhci_write_reg(UHCI_STS, 0xFFFF);
    
    // 6. Отключаем прерывания
    uhci_write_reg(UHCI_INTR, 0);
    
    // 7. Инициализируем структуры
    if (!uhci_init_structures()) {
        serial_puts("[UHCI] ERROR: Failed to init structures\n");
        return;
    }
    
    // 8. Включаем порты
    for (uint8_t port = 0; port < uhci_ports; port++) {
        uint16_t port_addr = (port == 0) ? UHCI_PORTSC1 : UHCI_PORTSC2;
        uint16_t port_status = uhci_read_reg(port_addr);
        
        // Включаем питание порта
        if (!(port_status & 0x0100)) {
            port_status |= 0x0100;
            uhci_write_reg(port_addr, port_status);
            uhci_delay_ms(20);
        }
    }
    
    // 9. Запускаем контроллер
    uhci_write_reg(UHCI_CMD, UHCI_CMD_RUN);
    uhci_delay_ms(10);
    
    // 10. Проверяем статус
    uint16_t status = uhci_read_reg(UHCI_STS);
    if (status & UHCI_STS_HCHALTED) {
        serial_puts("[UHCI] ERROR: Controller halted after start\n");
        return;
    }
    
    uhci_initialized = 1;
    serial_puts("[UHCI] Initialization successful\n");
}

// Обнаружение устройств UHCI
uint8_t uhci_detect_devices(void) {
    if (!uhci_initialized) {
        serial_puts("[UHCI] Cannot detect: controller not initialized\n");
        return 0;
    }
    
    serial_puts("[UHCI] Detecting devices...\n");
    
    uint8_t device_count = 0;
    
    for (uint8_t port = 0; port < uhci_ports; port++) {
        uint16_t port_addr = (port == 0) ? UHCI_PORTSC1 : UHCI_PORTSC2;
        uint16_t port_status = uhci_read_reg(port_addr);
        
        serial_puts("[UHCI] Port ");
        serial_puts_num(port);
        serial_puts(": 0x");
        serial_puts_num_hex(port_status);
        
        if (port_status & UHCI_PORT_CONNECT) {
            serial_puts(" [CONNECTED]");
            
            // Определяем скорость
            uint8_t low_speed = (port_status & UHCI_PORT_LSDA) ? 1 : 0;
            
            // Добавляем устройство
            _usb_add_device(port, low_speed ? USB_SPEED_LOW : USB_SPEED_FULL, 
                          0, "UHCI Device");
            device_count++;
            
            // Включаем порт если не включен
            if (!(port_status & UHCI_PORT_ENABLE)) {
                port_status |= UHCI_PORT_ENABLE;
                uhci_write_reg(port_addr, port_status);
                uhci_delay_ms(10);
            }
            
            // Сброс порта
            port_status |= UHCI_PORT_RESET;
            uhci_write_reg(port_addr, port_status);
            uhci_delay_ms(50);
            
            port_status &= ~UHCI_PORT_RESET;
            uhci_write_reg(port_addr, port_status);
            uhci_delay_ms(20);
        } else {
            serial_puts(" [DISCONNECTED]");
        }
        
        serial_puts("\n");
    }
    
    serial_puts("[UHCI] Found ");
    serial_puts_num(device_count);
    serial_puts(" device(s)\n");
    
    return device_count;
}

// Опрос UHCI
void uhci_poll(void) {
    if (!uhci_initialized) return;
    
    static uint32_t last_poll = 0;
    uint32_t current_ticks = timer_get_ticks();
    
    if (current_ticks - last_poll < 500) {
        return;
    }
    
    last_poll = current_ticks;
    
    // Проверяем порты на подключение/отключение
    for (uint8_t port = 0; port < uhci_ports; port++) {
        uint16_t port_addr = (port == 0) ? UHCI_PORTSC1 : UHCI_PORTSC2;
        uint16_t port_status = uhci_read_reg(port_addr);
        
        // Можно добавить логику обнаружения изменений
        (void)port_status;
    }
}

// Проверка наличия UHCI
uint8_t uhci_is_present(void) {
    return uhci_initialized;
}