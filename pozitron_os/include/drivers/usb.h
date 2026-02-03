#ifndef USB_H
#define USB_H

#include <stdint.h>

// === USB КОНСТАНТЫ ===
#define USB_MAX_DEVICES         32
#define USB_MAX_ENDPOINTS       16
#define USB_PACKET_SIZE         512
#define USB_MAX_PACKET_SIZE     1024

// Типы USB контроллеров
typedef enum {
    USB_TYPE_NONE = 0,
    USB_TYPE_UHCI,      // USB 1.1 (Intel)
    USB_TYPE_OHCI,      // USB 1.1 (другие производители)
    USB_TYPE_EHCI,      // USB 2.0
    USB_TYPE_XHCI       // USB 3.0+
} usb_controller_type_t;

// Скорости USB
typedef enum {
    USB_SPEED_LOW = 0,   // 1.5 Mbps
    USB_SPEED_FULL,      // 12 Mbps
    USB_SPEED_HIGH       // 480 Mbps
} usb_speed_t;

// Типы дескрипторов
#define USB_DESC_TYPE_DEVICE        0x01
#define USB_DESC_TYPE_CONFIGURATION 0x02
#define USB_DESC_TYPE_STRING        0x03
#define USB_DESC_TYPE_INTERFACE     0x04
#define USB_DESC_TYPE_ENDPOINT      0x05
#define USB_DESC_TYPE_HID           0x21
#define USB_DESC_TYPE_REPORT        0x22

// Классы устройств
#define USB_CLASS_HID               0x03
#define USB_CLASS_MASS_STORAGE      0x08
#define USB_CLASS_HUB               0x09

// HID подклассы
#define HID_SUBCLASS_BOOT           0x01

// HID протоколы
#define HID_PROTOCOL_KEYBOARD       0x01
#define HID_PROTOCOL_MOUSE          0x02

// Типы запросов (bmRequestType)
#define USB_REQ_TYPE_STANDARD       (0x00 << 5)
#define USB_REQ_TYPE_CLASS          (0x01 << 5)
#define USB_REQ_TYPE_VENDOR         (0x02 << 5)
#define USB_REQ_TYPE_RESERVED       (0x03 << 5)

#define USB_REQ_RECIPIENT_DEVICE    0x00
#define USB_REQ_RECIPIENT_INTERFACE 0x01
#define USB_REQ_RECIPIENT_ENDPOINT  0x02
#define USB_REQ_RECIPIENT_OTHER     0x03

#define USB_REQ_DIR_HOST_TO_DEVICE  0x00
#define USB_REQ_DIR_DEVICE_TO_HOST  0x80

// Стандартные запросы
#define USB_REQ_GET_STATUS          0x00
#define USB_REQ_CLEAR_FEATURE       0x01
#define USB_REQ_SET_FEATURE         0x03
#define USB_REQ_SET_ADDRESS         0x05
#define USB_REQ_GET_DESCRIPTOR      0x06
#define USB_REQ_SET_DESCRIPTOR      0x07
#define USB_REQ_GET_CONFIGURATION   0x08
#define USB_REQ_SET_CONFIGURATION   0x09
#define USB_REQ_GET_INTERFACE       0x0A
#define USB_REQ_SET_INTERFACE       0x0B
#define USB_REQ_SYNCH_FRAME         0x0C

// Типы конечных точек (bmAttributes)
#define USB_ENDPOINT_TYPE_MASK      0x03
#define USB_ENDPOINT_TYPE_CONTROL   0x00
#define USB_ENDPOINT_TYPE_ISOCH     0x01
#define USB_ENDPOINT_TYPE_BULK      0x02
#define USB_ENDPOINT_TYPE_INTERRUPT 0x03

#define USB_ENDPOINT_DIR_MASK       0x80
#define USB_ENDPOINT_IN             0x80
#define USB_ENDPOINT_OUT            0x00

// Структура USB запроса (Setup Packet)
typedef struct {
    uint8_t bmRequestType;
    uint8_t bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} __attribute__((packed)) usb_setup_packet_t;

// Структура дескриптора устройства
typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t bcdUSB;
    uint8_t bDeviceClass;
    uint8_t bDeviceSubClass;
    uint8_t bDeviceProtocol;
    uint8_t bMaxPacketSize0;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t iManufacturer;
    uint8_t iProduct;
    uint8_t iSerialNumber;
    uint8_t bNumConfigurations;
} __attribute__((packed)) usb_device_descriptor_t;

// Структура дескриптора конфигурации
typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t wTotalLength;
    uint8_t bNumInterfaces;
    uint8_t bConfigurationValue;
    uint8_t iConfiguration;
    uint8_t bmAttributes;
    uint8_t bMaxPower;
} __attribute__((packed)) usb_config_descriptor_t;

// Структура дескриптора интерфейса
typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint8_t bInterfaceNumber;
    uint8_t bAlternateSetting;
    uint8_t bNumEndpoints;
    uint8_t bInterfaceClass;
    uint8_t bInterfaceSubClass;
    uint8_t bInterfaceProtocol;
    uint8_t iInterface;
} __attribute__((packed)) usb_interface_descriptor_t;

// Структура дескриптора конечной точки
typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint8_t bEndpointAddress;
    uint8_t bmAttributes;
    uint16_t wMaxPacketSize;
    uint8_t bInterval;
} __attribute__((packed)) usb_endpoint_descriptor_t;

// Структура HID дескриптора
typedef struct {
    uint8_t bLength;
    uint8_t bDescriptorType;
    uint16_t bcdHID;
    uint8_t bCountryCode;
    uint8_t bNumDescriptors;
    uint8_t bDescriptorType2;
    uint16_t wDescriptorLength;
} __attribute__((packed)) usb_hid_descriptor_t;

// Структура USB конечной точки
typedef struct {
    uint8_t address;
    uint8_t attributes;
    uint16_t max_packet_size;
    uint8_t interval;
    uint8_t toggle;
} usb_endpoint_t;

// Структура USB интерфейса
typedef struct {
    uint8_t number;
    uint8_t class;
    uint8_t subclass;
    uint8_t protocol;
    uint8_t num_endpoints;
    usb_endpoint_t endpoints[USB_MAX_ENDPOINTS];
    uint16_t hid_report_size;
} usb_interface_t;

// Структура USB устройства
typedef struct {
    uint8_t         present;       // 1 = устройство подключено
    uint8_t         address;       // USB адрес (1-127)
    usb_speed_t     speed;         // Скорость устройства
    uint16_t        vendor_id;     // VID
    uint16_t        product_id;    // PID
    uint8_t         class;         // Класс устройства
    uint8_t         subclass;      // Подкласс
    uint8_t         protocol;      // Протокол
    uint8_t         port;          // Номер порта
    uint8_t         hub_port;      // Порт хаба (если через хаб)
    uint8_t         hub_addr;      // Адрес хаба
    uint8_t         max_packet_size; // Размер пакета endpoint 0
    char            name[64];      // Имя устройства
    char            description[128]; // Описание
    
    // Конфигурация
    uint8_t         configuration;
    uint8_t         num_interfaces;
    usb_interface_t interfaces[4];
    
    // Для HID
    uint8_t         is_hid;
    uint8_t         hid_interface;
    uint8_t         hid_endpoint_in;
    uint8_t         hid_endpoint_out;
    uint16_t        hid_report_size;
    
    // Контроллер
    usb_controller_type_t controller_type;
    uint8_t         controller_index;
} usb_device_t;

// Структура USB контроллера
typedef struct {
    usb_controller_type_t type;    // Тип контроллера
    uint32_t              base;    // Базовый адрес
    uint32_t              op_base; // Операционный базовый адрес (для EHCI)
    uint8_t               ports;   // Количество портов
    uint8_t               enabled; // 1 = включен
    char                  name[32]; // Название
    
    // PCI информация
    uint8_t               pci_bus;
    uint8_t               pci_device;
    uint8_t               pci_function;
    uint16_t              vendor_id;
    uint16_t              device_id;
    uint8_t               prog_if;
} usb_controller_t;

// Структура для статуса USB
typedef struct {
    uint8_t controllers_found;
    uint8_t controllers_enabled;
    uint8_t devices_found;
    uint8_t uhci_count;
    uint8_t ohci_count;
    uint8_t ehci_count;
    uint8_t xhci_count;
    uint8_t hid_devices;
    uint8_t storage_devices;
} usb_status_t;

// === ПУБЛИЧНЫЕ ФУНКЦИИ ===
void usb_system_init(void);                // Инициализация всей USB системы
void usb_init(void);                       // Алиас для usb_system_init
void usb_poll(void);                       // Опрос USB (вызывать в главном цикле)
void usb_detect_controllers(void);         // Поиск контроллеров
uint8_t usb_get_device_count(void);        // Количество обнаруженных устройств
usb_device_t* usb_get_device(uint8_t idx); // Получить устройство по индексу
const char* usb_get_controller_name(usb_controller_type_t type); // Имя контроллера

// Новые функции для GUI
usb_status_t usb_get_status(void);          // Получить статус USB системы
uint8_t usb_get_controller_count(void);     // Получить количество контроллеров
usb_controller_t* usb_get_controller(uint8_t index); // Получить контроллер по индексу

// НОВЫЕ ФУНКЦИИ ДЛЯ HID
int usb_control_transfer(usb_device_t* dev, 
                        uint8_t bmRequestType,
                        uint8_t bRequest,
                        uint16_t wValue,
                        uint16_t wIndex,
                        uint16_t wLength,
                        void* data);

int usb_interrupt_transfer(usb_device_t* dev,
                          uint8_t endpoint,
                          void* buffer,
                          uint16_t length,
                          uint32_t timeout_ms);

uint8_t usb_configure_device(usb_device_t* dev);
uint8_t usb_configure_hid_device(usb_device_t* dev);
void usb_process_hid_reports(void);

// Функции для чтения дескрипторов
int usb_get_descriptor(usb_device_t* dev, uint8_t type, uint8_t index, 
                      uint16_t lang, void* buffer, uint16_t length);
int usb_get_device_descriptor(usb_device_t* dev, usb_device_descriptor_t* desc);
int usb_get_config_descriptor(usb_device_t* dev, uint8_t config_index, 
                             void* buffer, uint16_t length);

// === ВНУТРЕННИЕ ФУНКЦИИ (для драйверов) ===
extern void _usb_add_device(uint8_t port, usb_speed_t speed, 
                           uint32_t controller_idx, const char* type_name);
extern void _usb_device_enumerated(usb_device_t* dev, uint8_t success);
extern void _usb_hid_report_received(usb_device_t* dev, uint8_t* report, 
                                    uint16_t length);

#endif // USB_H