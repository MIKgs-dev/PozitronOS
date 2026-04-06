#ifndef KERNEL_NOTIF_H
#define KERNEL_NOTIF_H

#include <stdint.h>

#define NOTIF_MAX_MSG    128
#define NOTIF_MAX_TITLE  32
#define NOTIF_MAX_COUNT  16
#define NOTIF_LIFETIME  1800       // 18 секунд при 100 Гц
#define NOTIF_MOVE_STEPS 10        // 0.1 секунды на перемещение
#define NOTIF_WIDTH      300
#define NOTIF_HEIGHT     80
#define NOTIF_PADDING    10

typedef enum {
    NOTIF_INFO = 0,
    NOTIF_WARNING,
    NOTIF_ERROR,
} notif_type_t;

typedef struct {
    notif_type_t type;
    char title[NOTIF_MAX_TITLE];
    char message[NOTIF_MAX_MSG];
    uint32_t created_tick;
    uint32_t id;
    
    // Для анимации перемещения
    uint8_t moving;
    int32_t target_y;
    int32_t current_y;
    uint32_t move_step;
    int32_t move_start_y;
} notif_t;

void notif_init(void);
void notif_update(void);  // Вызывать в главном цикле
void notif_render(void);  // Рисовать поверх всего

void notif_info(const char* title, const char* message);
void notif_warning(const char* title, const char* message);
void notif_error(const char* title, const char* message);
void notif_printf(notif_type_t type, const char* title, const char* format, ...);

#endif