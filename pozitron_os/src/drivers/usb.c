#include "drivers/usb.h"
#include "drivers/serial.h"
#include "drivers/ports.h"
#include "kernel/memory.h"
#include <stddef.h>

// === ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ ===
static usb_controller_t controllers[4];
static uint8_t controller_count = 0;
static usb_device_t usb_devices[USB_MAX_DEVICES];
static uint8_t usb_device_count = 0;
static uint8_t next_device_address = 1;

// === СВОИ РЕАЛИЗАЦИИ mem* функций ===
static void usb_memset(void* ptr, uint8_t value, uint32_t size) {
    uint8_t* p = (uint8_t*)ptr;
    for (uint32_t i = 0; i < size; i++) {
        p[i] = value;
    }
}

static void usb_memcpy(void* dst, const void* src, uint32_t size) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    for (uint32_t i = 0; i < size; i++) {
        d[i] = s[i];
    }
}

static int usb_memcmp(const void* ptr1, const void* ptr2, uint32_t size) {
    const uint8_t* p1 = (const uint8_t*)ptr1;
    const uint8_t* p2 = (const uint8_t*)ptr2;
    for (uint32_t i = 0; i < size; i++) {
        if (p1[i] != p2[i]) {
            return p1[i] - p2[i];
        }
    }
    return 0;
}

// === ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ ===

const char* usb_get_controller_name(usb_controller_type_t type) {
    switch(type) {
        case USB_TYPE_UHCI: return "UHCI (USB 1.1)";
        case USB_TYPE_OHCI: return "OHCI (USB 1.1)";
        case USB_TYPE_EHCI: return "EHCI (USB 2.0)";
        case USB_TYPE_XHCI: return "xHCI (USB 3.0)";
        default: return "Unknown";
    }
}

// Преобразование скорости в строку
static const char* usb_speed_to_str(usb_speed_t speed) {
    switch(speed) {
        case USB_SPEED_LOW: return "Low (1.5Mbps)";
        case USB_SPEED_FULL: return "Full (12Mbps)";
        case USB_SPEED_HIGH: return "High (480Mbps)";
        default: return "Unknown";
    }
}

// Задержка с использованием pause
static void usb_delay_ms(uint32_t ms) {
    // 1ms примерно = 1000 пауз на процессоре без таймера
    // Настраиваем под ваш процессор
    for (volatile uint32_t i = 0; i < ms * 1000; i++) {
        asm volatile ("pause");
    }
}

// Задержка микросекунд
static void usb_delay_us(uint32_t us) {
    for (volatile uint32_t i = 0; i < us * 10; i++) {
        asm volatile ("pause");
    }
}

// Добавление USB устройства
void _usb_add_device(uint8_t port, usb_speed_t speed, 
                     uint32_t controller_idx, const char* type_name) {
    if (usb_device_count >= USB_MAX_DEVICES) {
        serial_puts("[USB] WARNING: Too many devices, ignoring\n");
        return;
    }
    
    if (controller_idx >= controller_count) {
        serial_puts("[USB] ERROR: Invalid controller index\n");
        return;
    }
    
    usb_device_t* dev = &usb_devices[usb_device_count];
    
    // Очищаем структуру
    usb_memset(dev, 0, sizeof(usb_device_t));
    
    dev->present = 1;
    dev->address = 0; // Будет назначен при enumeration
    dev->speed = speed;
    dev->port = port;
    dev->controller_type = controllers[controller_idx].type;
    dev->controller_index = controller_idx;
    dev->max_packet_size = 8; // Default for control endpoint
    
    // Формируем имя
    char temp[32];
    char* d = dev->name;
    
    const char* prefix = "USB ";
    while (*prefix) *d++ = *prefix++;
    
    // Добавляем тип
    const char* speed_str = usb_speed_to_str(speed);
    while (*speed_str && (d - dev->name) < 62) *d++ = *speed_str++;
    
    *d++ = ' ';
    *d++ = 'D';
    *d++ = 'e';
    *d++ = 'v';
    *d++ = 'i';
    *d++ = 'c';
    *d++ = 'e';
    *d = '\0';
    
    // Описание
    d = dev->description;
    const char* desc = "Connected to port ";
    while (*desc) *d++ = *desc++;
    
    // Номер порта
    uint8_t port_num = port + 1;
    if (port_num >= 10) {
        *d++ = '0' + (port_num / 10);
        *d++ = '0' + (port_num % 10);
    } else {
        *d++ = '0' + port_num;
    }
    
    *d = '\0';
    
    usb_device_count++;
    
    serial_puts("[USB] New device: Port ");
    serial_puts_num(port);
    serial_puts(" (");
    serial_puts(speed_str);
    serial_puts(")\n");
}

// Уведомление о завершении enumeration
void _usb_device_enumerated(usb_device_t* dev, uint8_t success) {
    if (!dev) return;
    
    if (success) {
        serial_puts("[USB] Device enumerated successfully: ");
        serial_puts(dev->name);
        serial_puts("\n");
        
        // Обновляем имя на основе VID/PID
        if (dev->vendor_id || dev->product_id) {
            char* d = dev->name;
            const char* prefix = "USB Device ";
            while (*prefix) *d++ = *prefix++;
            
            // VID
            *d++ = '0';
            *d++ = 'x';
            uint16_t vid = dev->vendor_id;
            for (int i = 12; i >= 0; i -= 4) {
                uint8_t nibble = (vid >> i) & 0xF;
                *d++ = nibble < 10 ? '0' + nibble : 'A' + (nibble - 10);
            }
            
            *d++ = ':';
            
            // PID
            *d++ = '0';
            *d++ = 'x';
            uint16_t pid = dev->product_id;
            for (int i = 12; i >= 0; i -= 4) {
                uint8_t nibble = (pid >> i) & 0xF;
                *d++ = nibble < 10 ? '0' + nibble : 'A' + (nibble - 10);
            }
            
            *d = '\0';
        }
    } else {
        serial_puts("[USB] Device enumeration failed: ");
        serial_puts(dev->name);
        serial_puts("\n");
        dev->present = 0;
    }
}

// Уведомление о получении HID отчета
void _usb_hid_report_received(usb_device_t* dev, uint8_t* report, uint16_t length) {
    if (!dev || !report || length == 0) return;
    
    // Здесь будет обработка HID отчетов (клавиатура, мышь)
    // Пока просто логируем
    serial_puts("[USB HID] Report from ");
    serial_puts(dev->name);
    serial_puts(": ");
    
    for (uint16_t i = 0; i < length && i < 8; i++) {
        serial_puts(" 0x");
        serial_puts_num_hex(report[i]);
    }
    
    if (length > 8) serial_puts(" ...");
    serial_puts("\n");
}

// === УПРАВЛЯЮЩАЯ ТРАНЗАКЦИЯ (Control Transfer) ===
int usb_control_transfer(usb_device_t* dev, 
                        uint8_t bmRequestType,
                        uint8_t bRequest,
                        uint16_t wValue,
                        uint16_t wIndex,
                        uint16_t wLength,
                        void* data) {
    if (!dev || !dev->present) {
        serial_puts("[USB] ERROR: Device not present for control transfer\n");
        return -1;
    }
    
    // Выбираем правильную функцию контроллера
    switch(dev->controller_type) {
        case USB_TYPE_UHCI: {
            extern int uhci_control_transfer(uint8_t controller_idx,
                                           usb_device_t* dev,
                                           uint8_t bmRequestType,
                                           uint8_t bRequest,
                                           uint16_t wValue,
                                           uint16_t wIndex,
                                           uint16_t wLength,
                                           void* data);
            return uhci_control_transfer(dev->controller_index, dev,
                                        bmRequestType, bRequest,
                                        wValue, wIndex, wLength, data);
        }
        
        case USB_TYPE_OHCI: {
            extern int ohci_control_transfer(uint8_t controller_idx,
                                           usb_device_t* dev,
                                           uint8_t bmRequestType,
                                           uint8_t bRequest,
                                           uint16_t wValue,
                                           uint16_t wIndex,
                                           uint16_t wLength,
                                           void* data);
            return ohci_control_transfer(dev->controller_index, dev,
                                        bmRequestType, bRequest,
                                        wValue, wIndex, wLength, data);
        }
        
        case USB_TYPE_EHCI: {
            extern int ehci_control_transfer(uint8_t controller_idx,
                                           usb_device_t* dev,
                                           uint8_t bmRequestType,
                                           uint8_t bRequest,
                                           uint16_t wValue,
                                           uint16_t wIndex,
                                           uint16_t wLength,
                                           void* data);
            return ehci_control_transfer(dev->controller_index, dev,
                                        bmRequestType, bRequest,
                                        wValue, wIndex, wLength, data);
        }
        
        default:
            serial_puts("[USB] ERROR: Unsupported controller type for control transfer\n");
            return -1;
    }
}

// === ПРЕРЫВАЮЩАЯ ТРАНЗАКЦИЯ (Interrupt Transfer) ===
int usb_interrupt_transfer(usb_device_t* dev,
                          uint8_t endpoint,
                          void* buffer,
                          uint16_t length,
                          uint32_t timeout_ms) {
    if (!dev || !dev->present) {
        serial_puts("[USB] ERROR: Device not present for interrupt transfer\n");
        return -1;
    }
    
    // Выбираем правильную функцию контроллера
    switch(dev->controller_type) {
        case USB_TYPE_UHCI: {
            extern int uhci_interrupt_transfer(uint8_t controller_idx,
                                             usb_device_t* dev,
                                             uint8_t endpoint,
                                             void* buffer,
                                             uint16_t length,
                                             uint32_t timeout_ms);
            return uhci_interrupt_transfer(dev->controller_index, dev,
                                          endpoint, buffer, length, timeout_ms);
        }
        
        case USB_TYPE_OHCI: {
            extern int ohci_interrupt_transfer(uint8_t controller_idx,
                                             usb_device_t* dev,
                                             uint8_t endpoint,
                                             void* buffer,
                                             uint16_t length,
                                             uint32_t timeout_ms);
            return ohci_interrupt_transfer(dev->controller_index, dev,
                                          endpoint, buffer, length, timeout_ms);
        }
        
        case USB_TYPE_EHCI: {
            extern int ehci_interrupt_transfer(uint8_t controller_idx,
                                             usb_device_t* dev,
                                             uint8_t endpoint,
                                             void* buffer,
                                             uint16_t length,
                                             uint32_t timeout_ms);
            return ehci_interrupt_transfer(dev->controller_index, dev,
                                          endpoint, buffer, length, timeout_ms);
        }
        
        default:
            serial_puts("[USB] ERROR: Unsupported controller type for interrupt transfer\n");
            return -1;
    }
}

// === ЧТЕНИЕ ДЕСКРИПТОРОВ ===

// Получение дескриптора
int usb_get_descriptor(usb_device_t* dev, uint8_t type, uint8_t index, 
                      uint16_t lang, void* buffer, uint16_t length) {
    if (!dev || !buffer) return -1;
    
    usb_setup_packet_t setup = {
        .bmRequestType = USB_REQ_DIR_DEVICE_TO_HOST | USB_REQ_TYPE_STANDARD | USB_REQ_RECIPIENT_DEVICE,
        .bRequest = USB_REQ_GET_DESCRIPTOR,
        .wValue = (type << 8) | index,
        .wIndex = lang,
        .wLength = length
    };
    
    return usb_control_transfer(dev, 
                               setup.bmRequestType,
                               setup.bRequest,
                               setup.wValue,
                               setup.wIndex,
                               setup.wLength,
                               buffer);
}

// Получение дескриптора устройства
int usb_get_device_descriptor(usb_device_t* dev, usb_device_descriptor_t* desc) {
    if (!dev || !desc) return -1;
    
    int result = usb_get_descriptor(dev, USB_DESC_TYPE_DEVICE, 0, 0, 
                                   desc, sizeof(usb_device_descriptor_t));
    
    if (result >= 0 && desc->bLength >= sizeof(usb_device_descriptor_t)) {
        // Обновляем информацию об устройстве
        dev->vendor_id = desc->idVendor;
        dev->product_id = desc->idProduct;
        dev->class = desc->bDeviceClass;
        dev->subclass = desc->bDeviceSubClass;
        dev->protocol = desc->bDeviceProtocol;
        dev->max_packet_size = desc->bMaxPacketSize0;
        
        serial_puts("[USB] Device descriptor: VID=0x");
        serial_puts_num_hex(desc->idVendor);
        serial_puts(" PID=0x");
        serial_puts_num_hex(desc->idProduct);
        serial_puts(" Class=0x");
        serial_puts_num_hex(desc->bDeviceClass);
        serial_puts("\n");
    }
    
    return result;
}

// Получение дескриптора конфигурации
int usb_get_config_descriptor(usb_device_t* dev, uint8_t config_index, 
                             void* buffer, uint16_t length) {
    if (!dev || !buffer) return -1;
    
    return usb_get_descriptor(dev, USB_DESC_TYPE_CONFIGURATION, config_index, 0,
                             buffer, length);
}

// === КОНФИГУРАЦИЯ УСТРОЙСТВ ===

// Установка адреса устройства
static int usb_set_address(usb_device_t* dev, uint8_t address) {
    if (!dev) return -1;
    
    usb_setup_packet_t setup = {
        .bmRequestType = USB_REQ_DIR_HOST_TO_DEVICE | USB_REQ_TYPE_STANDARD | USB_REQ_RECIPIENT_DEVICE,
        .bRequest = USB_REQ_SET_ADDRESS,
        .wValue = address,
        .wIndex = 0,
        .wLength = 0
    };
    
    int result = usb_control_transfer(dev, 
                                     setup.bmRequestType,
                                     setup.bRequest,
                                     setup.wValue,
                                     setup.wIndex,
                                     setup.wLength,
                                     NULL);
    
    if (result >= 0) {
        dev->address = address;
        serial_puts("[USB] Set address ");
        serial_puts_num(address);
        serial_puts(" for device\n");
        
        // Задержка после SET_ADDRESS
        usb_delay_ms(10);
    }
    
    return result;
}

// Установка конфигурации
static int usb_set_configuration(usb_device_t* dev, uint8_t config) {
    if (!dev) return -1;
    
    usb_setup_packet_t setup = {
        .bmRequestType = USB_REQ_DIR_HOST_TO_DEVICE | USB_REQ_TYPE_STANDARD | USB_REQ_RECIPIENT_DEVICE,
        .bRequest = USB_REQ_SET_CONFIGURATION,
        .wValue = config,
        .wIndex = 0,
        .wLength = 0
    };
    
    int result = usb_control_transfer(dev, 
                                     setup.bmRequestType,
                                     setup.bRequest,
                                     setup.wValue,
                                     setup.wIndex,
                                     setup.wLength,
                                     NULL);
    
    if (result >= 0) {
        dev->configuration = config;
        serial_puts("[USB] Set configuration ");
        serial_puts_num(config);
        serial_puts(" for device\n");
    }
    
    return result;
}

// Анализ дескриптора конфигурации для HID устройств
static int parse_configuration_descriptor(usb_device_t* dev, uint8_t* buffer, uint16_t length) {
    if (!dev || !buffer) return -1;
    
    uint8_t* ptr = buffer;
    uint16_t remaining = length;
    
    usb_config_descriptor_t* config_desc = NULL;
    usb_interface_descriptor_t* iface_desc = NULL;
    usb_endpoint_descriptor_t* ep_desc = NULL;
    usb_hid_descriptor_t* hid_desc = NULL;
    
    uint8_t current_interface = 0;
    uint8_t current_endpoint = 0;
    
    while (remaining > 0) {
        uint8_t desc_len = ptr[0];
        uint8_t desc_type = ptr[1];
        
        if (desc_len == 0 || desc_len > remaining) {
            break;
        }
        
        switch(desc_type) {
            case USB_DESC_TYPE_CONFIGURATION:
                if (desc_len >= sizeof(usb_config_descriptor_t)) {
                    config_desc = (usb_config_descriptor_t*)ptr;
                    dev->num_interfaces = config_desc->bNumInterfaces;
                }
                break;
                
            case USB_DESC_TYPE_INTERFACE:
                if (desc_len >= sizeof(usb_interface_descriptor_t)) {
                    iface_desc = (usb_interface_descriptor_t*)ptr;
                    
                    if (current_interface < 4) {
                        usb_interface_t* iface = &dev->interfaces[current_interface];
                        iface->number = iface_desc->bInterfaceNumber;
                        iface->class = iface_desc->bInterfaceClass;
                        iface->subclass = iface_desc->bInterfaceSubClass;
                        iface->protocol = iface_desc->bInterfaceProtocol;
                        iface->num_endpoints = iface_desc->bNumEndpoints;
                        
                        // Если это HID интерфейс
                        if (iface_desc->bInterfaceClass == USB_CLASS_HID) {
                            dev->is_hid = 1;
                            dev->hid_interface = iface_desc->bInterfaceNumber;
                            serial_puts("[USB] Found HID interface: ");
                            serial_puts_num(iface_desc->bInterfaceNumber);
                            serial_puts("\n");
                        }
                        
                        current_interface++;
                        current_endpoint = 0;
                    }
                }
                break;
                
            case USB_DESC_TYPE_ENDPOINT:
                if (desc_len >= sizeof(usb_endpoint_descriptor_t)) {
                    ep_desc = (usb_endpoint_descriptor_t*)ptr;
                    
                    if (current_interface > 0 && current_endpoint < USB_MAX_ENDPOINTS) {
                        usb_interface_t* iface = &dev->interfaces[current_interface - 1];
                        usb_endpoint_t* ep = &iface->endpoints[current_endpoint];
                        
                        ep->address = ep_desc->bEndpointAddress;
                        ep->attributes = ep_desc->bmAttributes;
                        ep->max_packet_size = ep_desc->wMaxPacketSize;
                        ep->interval = ep_desc->bInterval;
                        ep->toggle = 0;
                        
                        // Если это IN endpoint для HID
                        if (dev->is_hid && 
                            iface->number == dev->hid_interface &&
                            (ep->address & USB_ENDPOINT_DIR_MASK) == USB_ENDPOINT_IN &&
                            (ep->attributes & USB_ENDPOINT_TYPE_MASK) == USB_ENDPOINT_TYPE_INTERRUPT) {
                            dev->hid_endpoint_in = ep->address;
                            serial_puts("[USB] Found HID IN endpoint: 0x");
                            serial_puts_num_hex(ep->address);
                            serial_puts("\n");
                        }
                        
                        current_endpoint++;
                    }
                }
                break;
                
            case USB_DESC_TYPE_HID:
                if (desc_len >= sizeof(usb_hid_descriptor_t)) {
                    hid_desc = (usb_hid_descriptor_t*)ptr;
                    
                    if (dev->is_hid) {
                        dev->hid_report_size = hid_desc->wDescriptorLength;
                        serial_puts("[USB] HID report size: ");
                        serial_puts_num(hid_desc->wDescriptorLength);
                        serial_puts("\n");
                    }
                }
                break;
        }
        
        ptr += desc_len;
        remaining -= desc_len;
    }
    
    return 0;
}

// Конфигурация HID устройства
uint8_t usb_configure_hid_device(usb_device_t* dev) {
    if (!dev || !dev->is_hid) return 0;
    
    serial_puts("[USB] Configuring HID device\n");
    
    // Устанавливаем протокол (Boot Protocol для клавиатуры/мыши)
    usb_setup_packet_t setup = {
        .bmRequestType = USB_REQ_DIR_HOST_TO_DEVICE | USB_REQ_TYPE_CLASS | USB_REQ_RECIPIENT_INTERFACE,
        .bRequest = 0x0B, // SET_PROTOCOL
        .wValue = 0,      // Boot Protocol
        .wIndex = dev->hid_interface,
        .wLength = 0
    };
    
    int result = usb_control_transfer(dev, 
                                     setup.bmRequestType,
                                     setup.bRequest,
                                     setup.wValue,
                                     setup.wIndex,
                                     setup.wLength,
                                     NULL);
    
    if (result < 0) {
        serial_puts("[USB] Failed to set HID protocol\n");
        return 0;
    }
    
    serial_puts("[USB] HID device configured successfully\n");
    return 1;
}

// Конфигурация устройства
uint8_t usb_configure_device(usb_device_t* dev) {
    if (!dev || dev->address == 0) {
        serial_puts("[USB] Cannot configure: device not addressed\n");
        return 0;
    }
    
    serial_puts("[USB] Configuring device at address ");
    serial_puts_num(dev->address);
    serial_puts("\n");
    
    // 1. Читаем дескриптор устройства
    usb_device_descriptor_t dev_desc;
    if (usb_get_device_descriptor(dev, &dev_desc) < 0) {
        serial_puts("[USB] Failed to get device descriptor\n");
        return 0;
    }
    
    // 2. Читаем первую конфигурация
    uint8_t config_buffer[256];
    if (usb_get_config_descriptor(dev, 0, config_buffer, sizeof(config_buffer)) < 0) {
        serial_puts("[USB] Failed to get config descriptor\n");
        return 0;
    }
    
    // 3. Анализируем дескриптор конфигурации
    if (parse_configuration_descriptor(dev, config_buffer, sizeof(config_buffer)) < 0) {
        serial_puts("[USB] Failed to parse config descriptor\n");
        return 0;
    }
    
    // 4. Устанавливаем конфигурацию
    if (usb_set_configuration(dev, 1) < 0) {
        serial_puts("[USB] Failed to set configuration\n");
        return 0;
    }
    
    // 5. Если это HID устройство, конфигурируем его
    if (dev->is_hid) {
        if (!usb_configure_hid_device(dev)) {
            serial_puts("[USB] Failed to configure HID device\n");
            return 0;
        }
    }
    
    serial_puts("[USB] Device configured successfully\n");
    return 1;
}

// Перечисление устройства
static void enumerate_device(usb_device_t* dev) {
    if (!dev || dev->address != 0) return;
    
    serial_puts("[USB] Enumerating device\n");
    
    // 1. Сначала работаем с адресом 0
    dev->address = 0;
    
    // 2. Получаем дескриптор устройства (еще с адресом 0)
    usb_device_descriptor_t dev_desc;
    if (usb_get_device_descriptor(dev, &dev_desc) < 0) {
        serial_puts("[USB] Failed to get initial device descriptor\n");
        _usb_device_enumerated(dev, 0);
        return;
    }
    
    // 3. Назначаем новый адрес
    uint8_t new_address = next_device_address++;
    if (next_device_address > 127) next_device_address = 1;
    
    if (usb_set_address(dev, new_address) < 0) {
        serial_puts("[USB] Failed to set address\n");
        _usb_device_enumerated(dev, 0);
        return;
    }
    
    // 4. Задержка для стабилизации
    usb_delay_ms(50);
    
    // 5. Конфигурируем устройство с новым адресом
    if (!usb_configure_device(dev)) {
        serial_puts("[USB] Failed to configure device\n");
        _usb_device_enumerated(dev, 0);
        return;
    }
    
    _usb_device_enumerated(dev, 1);
}

// Обработка HID отчетов
void usb_process_hid_reports(void) {
    for (uint8_t i = 0; i < usb_device_count; i++) {
        usb_device_t* dev = &usb_devices[i];
        
        if (!dev->present || !dev->is_hid || dev->hid_endpoint_in == 0) {
            continue;
        }
        
        uint8_t report_buffer[64];
        int result = usb_interrupt_transfer(dev, dev->hid_endpoint_in,
                                          report_buffer, sizeof(report_buffer), 0);
        
        if (result > 0) {
            _usb_hid_report_received(dev, report_buffer, result);
        }
    }
}

// === ОСНОВНЫЕ ФУНКЦИИ USB СИСТЕМЫ ===

// Детальное сканирование PCI
static void usb_scan_pci_detailed(void) {
    serial_puts("[USB] DETAILED PCI scan for USB...\n");
    
    controller_count = 0;
    
    // Сканируем шину 0 полностью
    for (uint8_t device = 0; device < 32; device++) {
        for (uint8_t function = 0; function < 8; function++) {
            uint32_t address = 0x80000000 | (0 << 16) | (device << 11) | (function << 8);
            
            // Читаем Vendor/Device
            outl(0xCF8, address);
            uint32_t vendor_device = inl(0xCFC);
            
            if (vendor_device == 0xFFFFFFFF) {
                if (function == 0) break; // Нет устройства
                continue; // Нет функции
            }
            
            uint16_t vendor_id = vendor_device & 0xFFFF;
            uint16_t device_id = vendor_device >> 16;
            
            // Читаем Class/Subclass/ProgIF
            outl(0xCF8, address | 0x08);
            uint32_t class_rev = inl(0xCFC);
            uint8_t class_code = (class_rev >> 24) & 0xFF;
            uint8_t subclass = (class_rev >> 16) & 0xFF;
            uint8_t prog_if = (class_rev >> 8) & 0xFF;
            // uint8_t revision = class_rev & 0xFF; // Не используется
            
            // Проверяем если это USB контроллер
            if (class_code == 0x0C && subclass == 0x03) {
                serial_puts("[USB] FOUND CONTROLLER! ");
                
                usb_controller_type_t type = USB_TYPE_NONE;
                
                switch(prog_if) {
                    case 0x00: 
                        type = USB_TYPE_UHCI;
                        serial_puts("UHCI (USB 1.1)");
                        break;
                    case 0x10: 
                        type = USB_TYPE_OHCI;
                        serial_puts("OHCI (USB 1.1)");
                        break;
                    case 0x20: 
                        type = USB_TYPE_EHCI;
                        serial_puts("EHCI (USB 2.0)");
                        break;
                    case 0x30: 
                        type = USB_TYPE_XHCI;
                        serial_puts("xHCI (USB 3.0)");
                        break;
                    default:
                        serial_puts("Unknown USB");
                        break;
                }
                
                if (type != USB_TYPE_NONE && controller_count < 4) {
                    // Читаем BAR0
                    outl(0xCF8, address | 0x10);
                    uint32_t bar0 = inl(0xCFC);
                    
                    serial_puts(" at BAR0=0x");
                    serial_puts_num_hex(bar0);
                    
                    controllers[controller_count].type = type;
                    
                    if (bar0 & 0x01) {
                        // I/O пространство
                        controllers[controller_count].base = bar0 & 0xFFFFFFFC;
                    } else {
                        // Memory пространство
                        controllers[controller_count].base = bar0 & 0xFFFFFFF0;
                    }
                    
                    // Для EHCI устанавливаем op_base
                    if (type == USB_TYPE_EHCI) {
                        controllers[controller_count].op_base = controllers[controller_count].base + 0x10;
                    } else {
                        controllers[controller_count].op_base = controllers[controller_count].base;
                    }
                    
                    controllers[controller_count].enabled = 0;
                    controllers[controller_count].ports = 2; // Предполагаем
                    
                    // PCI информация
                    controllers[controller_count].pci_bus = 0;
                    controllers[controller_count].pci_device = device;
                    controllers[controller_count].pci_function = function;
                    controllers[controller_count].vendor_id = vendor_id;
                    controllers[controller_count].device_id = device_id;
                    controllers[controller_count].prog_if = prog_if;
                    
                    const char* name = usb_get_controller_name(type);
                    char* ptr = controllers[controller_count].name;
                    while (*name && (ptr - controllers[controller_count].name) < 31) {
                        *ptr++ = *name++;
                    }
                    *ptr = '\0';
                    
                    controller_count++;
                }
                
                serial_puts("\n");
            }
        }
    }
    
    if (controller_count == 0) {
        serial_puts("[USB] NO controllers found after detailed scan!\n");
    } else {
        serial_puts("[USB] Found ");
        serial_puts_num(controller_count);
        serial_puts(" controller(s)\n");
    }
}

// Инициализация USB системы
void usb_system_init(void) {
    serial_puts("\n=== USB SYSTEM INITIALIZATION ===\n");
    
    controller_count = 0;
    usb_device_count = 0;
    next_device_address = 1;
    
    // Очищаем массивы
    usb_memset(controllers, 0, sizeof(controllers));
    usb_memset(usb_devices, 0, sizeof(usb_devices));
    
    // Детальное сканирование
    usb_scan_pci_detailed();
    
    // Если нашли контроллеры - инициализируем
    for (uint8_t i = 0; i < controller_count; i++) {
        serial_puts("[USB] Initializing ");
        serial_puts(controllers[i].name);
        serial_puts(" at 0x");
        serial_puts_num_hex(controllers[i].base);
        serial_puts("\n");
        
        controllers[i].enabled = 1;
        
        // Объявляем функции с extern
        switch(controllers[i].type) {
            case USB_TYPE_UHCI: {
                extern void uhci_init(uint32_t base);
                uhci_init(controllers[i].base);
                
                extern uint8_t uhci_detect_devices(void);
                uhci_detect_devices();
                break;
            }
        
            case USB_TYPE_OHCI: {
                extern void ohci_init(uint32_t base);
                ohci_init(controllers[i].base);
                
                extern uint8_t ohci_detect_devices(void);
                ohci_detect_devices();
                break;
            }
        
            case USB_TYPE_EHCI: {
                extern void ehci_init(uint32_t cap_base, uint32_t op_base);
                ehci_init(controllers[i].base, controllers[i].op_base);
                
                extern uint8_t ehci_detect_devices(void);
                ehci_detect_devices();
                break;
            }
        
            default:
                break;
        }
    }
    
    // Перечисляем найденные устройства
    serial_puts("[USB] Enumerating devices...\n");
    for (uint8_t i = 0; i < usb_device_count; i++) {
        enumerate_device(&usb_devices[i]);
    }
    
    serial_puts("[USB] Total USB devices: ");
    serial_puts_num(usb_device_count);
    serial_puts("\n");
    
    serial_puts("=====================================\n");
}

// Опрос USB
void usb_poll(void) {
    // Опрашиваем контроллеры
    for (uint8_t i = 0; i < controller_count; i++) {
        if (controllers[i].enabled) {
            switch(controllers[i].type) {
                case USB_TYPE_UHCI: {
                    extern void uhci_poll(void);
                    uhci_poll();
                    break;
                }
                case USB_TYPE_OHCI: {
                    extern void ohci_poll(void);
                    ohci_poll();
                    break;
                }
                case USB_TYPE_EHCI: {
                    extern void ehci_poll(void);
                    ehci_poll();
                    break;
                }
                default:
                    break;
            }
        }
    }
    
    // Обрабатываем HID отчеты
    usb_process_hid_reports();
}

// Получение статуса
usb_status_t usb_get_status(void) {
    usb_status_t status;
    usb_memset(&status, 0, sizeof(usb_status_t));
    
    status.controllers_found = controller_count;
    status.devices_found = usb_device_count;
    
    // Считаем по типам
    for (uint8_t i = 0; i < controller_count; i++) {
        if (controllers[i].enabled) {
            status.controllers_enabled++;
        }
        
        switch(controllers[i].type) {
            case USB_TYPE_UHCI: status.uhci_count++; break;
            case USB_TYPE_OHCI: status.ohci_count++; break;
            case USB_TYPE_EHCI: status.ehci_count++; break;
            case USB_TYPE_XHCI: status.xhci_count++; break;
            default: break;
        }
    }
    
    // Считаем HID и storage устройства
    for (uint8_t i = 0; i < usb_device_count; i++) {
        if (usb_devices[i].present) {
            if (usb_devices[i].is_hid) {
                status.hid_devices++;
            }
            if (usb_devices[i].class == USB_CLASS_MASS_STORAGE) {
                status.storage_devices++;
            }
        }
    }
    
    return status;
}

// Остальные функции остаются без изменений
void usb_detect_controllers(void) {
    usb_scan_pci_detailed();
}

uint8_t usb_get_device_count(void) {
    return usb_device_count;
}

usb_device_t* usb_get_device(uint8_t idx) {
    if (idx >= usb_device_count) return NULL;
    return &usb_devices[idx];
}

uint8_t usb_get_controller_count(void) {
    return controller_count;
}

usb_controller_t* usb_get_controller(uint8_t index) {
    if (index >= controller_count) return NULL;
    return &controllers[index];
}

void usb_init(void) {
    usb_system_init();
}