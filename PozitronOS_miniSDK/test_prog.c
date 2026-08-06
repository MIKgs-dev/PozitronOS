#include "pozitron.h"

// ============================================================
// ВСТРОЕННЫЕ ФУНКЦИИ
// ============================================================
static inline void* memset(void* s, int c, int n) {
    char* p = (char*)s;
    for (int i = 0; i < n; i++) p[i] = (char)c;
    return s;
}

static inline int strlen(const char* s) {
    int len = 0;
    while (s[len]) len++;
    return len;
}

static inline void int_to_str(int num, char* str) {
    if (num == 0) {
        str[0] = '0';
        str[1] = '\0';
        return;
    }
    
    char temp[32];
    int i = 0;
    int is_neg = 0;
    
    if (num < 0) {
        is_neg = 1;
        num = -num;
    }
    
    while (num > 0) {
        temp[i++] = '0' + (num % 10);
        num /= 10;
    }
    
    int j = 0;
    if (is_neg) str[j++] = '-';
    while (i > 0) str[j++] = temp[--i];
    str[j] = '\0';
}

// ============================================================
// ГЕНЕРАТОР СЛУЧАЙНЫХ ЧИСЕЛ
// ============================================================
static uint32_t rand_seed = 0x12345678;

static int rand(void) {
    rand_seed = rand_seed * 1103515245 + 12345;
    return (rand_seed >> 16) & 0x7FFF;
}

// ============================================================
// КОНСТАНТЫ ИГРЫ
// ============================================================
#define GRID_SIZE       20
#define MAX_SNAKE       400

static const uint32_t COLORS[] = {
    0x1A1A1E, // BG_MAIN
    0x25252B, // BG_PANEL
    0x2D2D35, // BG_HEADER
    0x3A3A44, // BG_HOVER
    0x4A4A56, // BG_SELECTED
    0x4A8AB5, // ACCENT_PRIMARY
    0x5A9AC5, // ACCENT_HOVER
    0x8AB8D8, // ACCENT_LIGHT
    0xE8E8EC, // TEXT_PRIMARY
    0xB8B8C0, // TEXT_SECONDARY
    0x888890, // TEXT_DIM
    0x4A8AB5, // SNAKE_HEAD
    0x3A7AA5, // SNAKE_BODY
    0x5A6A8A, // SNAKE_BODY_DARK
    0xE84A4A, // FOOD_COLOR
    0x4A8A4A, // FOOD_SPECIAL
    0x6A7A8A, // BORDER
};

enum {
    C_BG_MAIN, C_BG_PANEL, C_BG_HEADER, C_BG_HOVER, C_BG_SELECTED,
    C_ACCENT_PRIMARY, C_ACCENT_HOVER, C_ACCENT_LIGHT,
    C_TEXT_PRIMARY, C_TEXT_SECONDARY, C_TEXT_DIM,
    C_SNAKE_HEAD, C_SNAKE_BODY, C_SNAKE_BODY_DARK,
    C_FOOD_COLOR, C_FOOD_SPECIAL, C_BORDER
};

// ============================================================
// ШРИФТ 8x12 - ПРАВИЛЬНЫЙ ФОРМАТ!
// ============================================================
// Каждый символ - 12 байт, каждый байт - 8 пикселей по вертикали
// Бит 0 = верхний пиксель, бит 7 = нижний
static const uint8_t FONT_8x12[][12] = {
    // '0' - '9'
    {0x3C,0x66,0x6E,0x76,0x66,0x66,0x66,0x66,0x66,0x3C,0x00,0x00},
    {0x18,0x38,0x78,0x18,0x18,0x18,0x18,0x18,0x18,0x7E,0x00,0x00},
    {0x3C,0x66,0x06,0x0C,0x18,0x30,0x60,0x60,0x66,0x7E,0x00,0x00},
    {0x3C,0x66,0x06,0x1C,0x06,0x06,0x06,0x06,0x66,0x3C,0x00,0x00},
    {0x0C,0x1C,0x3C,0x6C,0xCC,0xFE,0x0C,0x0C,0x0C,0x1E,0x00,0x00},
    {0x7E,0x60,0x60,0x7C,0x06,0x06,0x06,0x06,0x66,0x3C,0x00,0x00},
    {0x1C,0x30,0x60,0x7C,0x66,0x66,0x66,0x66,0x66,0x3C,0x00,0x00},
    {0x7E,0x06,0x0C,0x18,0x18,0x30,0x30,0x60,0x60,0x60,0x00,0x00},
    {0x3C,0x66,0x66,0x3C,0x66,0x66,0x66,0x66,0x66,0x3C,0x00,0x00},
    {0x3C,0x66,0x66,0x66,0x66,0x3E,0x06,0x0C,0x38,0x00,0x00,0x00},
    // 'A' - 'Z'
    {0x18,0x3C,0x66,0x66,0x7E,0x66,0x66,0x66,0x66,0x66,0x00,0x00},
    {0x7C,0x66,0x66,0x7C,0x66,0x66,0x66,0x66,0x66,0x7C,0x00,0x00},
    {0x3C,0x66,0x60,0x60,0x60,0x60,0x60,0x60,0x66,0x3C,0x00,0x00},
    {0x78,0x6C,0x66,0x66,0x66,0x66,0x66,0x66,0x6C,0x78,0x00,0x00},
    {0x7E,0x60,0x60,0x7C,0x60,0x60,0x60,0x60,0x60,0x7E,0x00,0x00},
    {0x7E,0x60,0x60,0x7C,0x60,0x60,0x60,0x60,0x60,0x60,0x00,0x00},
    {0x3C,0x66,0x60,0x60,0x6E,0x66,0x66,0x66,0x66,0x3C,0x00,0x00},
    {0x66,0x66,0x66,0x7E,0x66,0x66,0x66,0x66,0x66,0x66,0x00,0x00},
    {0x3C,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00},
    {0x1E,0x0C,0x0C,0x0C,0x0C,0x0C,0x0C,0x6C,0x6C,0x38,0x00,0x00},
    {0x66,0x6C,0x78,0x70,0x60,0x70,0x78,0x6C,0x66,0x66,0x00,0x00},
    {0x60,0x60,0x60,0x60,0x60,0x60,0x60,0x60,0x60,0x7E,0x00,0x00},
    {0x63,0x77,0x7F,0x6B,0x6B,0x63,0x63,0x63,0x63,0x63,0x00,0x00},
    {0x66,0x76,0x7E,0x7E,0x6E,0x66,0x66,0x66,0x66,0x66,0x00,0x00},
    {0x3C,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x3C,0x00,0x00},
    {0x7C,0x66,0x66,0x66,0x7C,0x60,0x60,0x60,0x60,0x60,0x00,0x00},
    {0x3C,0x66,0x66,0x66,0x66,0x66,0x66,0x6E,0x3C,0x06,0x00,0x00},
    {0x7C,0x66,0x66,0x66,0x7C,0x6C,0x66,0x66,0x66,0x66,0x00,0x00},
    {0x3C,0x66,0x60,0x30,0x18,0x0C,0x06,0x66,0x66,0x3C,0x00,0x00},
    {0x7E,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x00,0x00},
    {0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x3C,0x00,0x00},
    {0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x3C,0x18,0x18,0x00,0x00},
    {0x63,0x63,0x63,0x6B,0x6B,0x7F,0x77,0x63,0x63,0x63,0x00,0x00},
    {0x66,0x66,0x3C,0x18,0x18,0x18,0x3C,0x66,0x66,0x66,0x00,0x00},
    {0x66,0x66,0x66,0x3C,0x18,0x18,0x18,0x18,0x18,0x18,0x00,0x00},
    {0x7E,0x06,0x0C,0x18,0x18,0x30,0x60,0x60,0x60,0x7E,0x00,0x00},
    // 'a' - 'z' (строчные)
    {0x00,0x00,0x00,0x3C,0x06,0x3E,0x66,0x66,0x66,0x3E,0x00,0x00},
    {0x60,0x60,0x60,0x7C,0x66,0x66,0x66,0x66,0x66,0x7C,0x00,0x00},
    {0x00,0x00,0x00,0x3C,0x66,0x60,0x60,0x60,0x66,0x3C,0x00,0x00},
    {0x06,0x06,0x06,0x3E,0x66,0x66,0x66,0x66,0x66,0x3E,0x00,0x00},
    {0x00,0x00,0x00,0x3C,0x66,0x7E,0x60,0x60,0x66,0x3C,0x00,0x00},
    {0x1C,0x30,0x30,0x7C,0x30,0x30,0x30,0x30,0x30,0x30,0x00,0x00},
    {0x00,0x00,0x00,0x3E,0x66,0x66,0x66,0x3E,0x06,0x3C,0x00,0x00},
    {0x60,0x60,0x60,0x7C,0x66,0x66,0x66,0x66,0x66,0x66,0x00,0x00},
    {0x18,0x00,0x38,0x18,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00},
    {0x0C,0x00,0x1C,0x0C,0x0C,0x0C,0x0C,0x6C,0x6C,0x38,0x00,0x00},
    {0x60,0x60,0x60,0x6C,0x78,0x70,0x60,0x70,0x78,0x6C,0x00,0x00},
    {0x38,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x3C,0x00,0x00},
    {0x00,0x00,0x00,0x6C,0x7E,0x7E,0x6B,0x63,0x63,0x63,0x00,0x00},
    {0x00,0x00,0x00,0x7C,0x66,0x66,0x66,0x66,0x66,0x66,0x00,0x00},
    {0x00,0x00,0x00,0x3C,0x66,0x66,0x66,0x66,0x66,0x3C,0x00,0x00},
    {0x00,0x00,0x00,0x7C,0x66,0x66,0x66,0x7C,0x60,0x60,0x00,0x00},
    {0x00,0x00,0x00,0x3E,0x66,0x66,0x66,0x3E,0x06,0x06,0x00,0x00},
    {0x00,0x00,0x00,0x6C,0x76,0x60,0x60,0x60,0x60,0x60,0x00,0x00},
    {0x00,0x00,0x00,0x3C,0x60,0x3C,0x06,0x06,0x66,0x3C,0x00,0x00},
    {0x30,0x30,0x30,0x7C,0x30,0x30,0x30,0x30,0x30,0x1C,0x00,0x00},
    {0x00,0x00,0x00,0x66,0x66,0x66,0x66,0x66,0x66,0x3E,0x00,0x00},
    {0x00,0x00,0x00,0x66,0x66,0x66,0x66,0x66,0x3C,0x18,0x00,0x00},
    {0x00,0x00,0x00,0x63,0x63,0x6B,0x7F,0x7F,0x3E,0x22,0x00,0x00},
    {0x00,0x00,0x00,0x66,0x3C,0x18,0x18,0x3C,0x66,0x66,0x00,0x00},
    {0x00,0x00,0x00,0x66,0x66,0x66,0x66,0x3E,0x06,0x3C,0x00,0x00},
    {0x00,0x00,0x00,0x7E,0x0C,0x18,0x30,0x60,0x60,0x7E,0x00,0x00},
};

static const int FONT_W = 8;
static const int FONT_H = 12;

// ============================================================
// СТРУКТУРЫ
// ============================================================
typedef struct {
    int x, y;
} Point;

typedef struct {
    Point body[MAX_SNAKE];
    int length;
    int direction;
    int next_direction;
    int alive;
    int score;
    int high_score;
} Snake;

typedef struct {
    Point pos;
    int type;
    int active;
} Food;

typedef struct {
    int window_id;
    int running;
    int paused;
    int game_over;
    int frame_count;
    int move_timer;
    int move_delay;
    
    Snake snake;
    Food food;
    Food special_food;
    int special_food_timer;
    
    int view_w, view_h;
    int is_visible;
    int need_redraw;
    
    int grid_x, grid_y;
    int grid_w, grid_h;
    int cell_size;
} Game;

// ============================================================
// ПРОТОТИПЫ
// ============================================================
static void game_spawn_food(Game* g);
static void game_spawn_special_food(Game* g);
static void game_move_snake(Game* g);

// ============================================================
// ФУНКЦИИ РИСОВАНИЯ
// ============================================================
static inline void draw_pixel_safe(int win, int x, int y, uint32_t color, int w, int h) {
    if (x >= 0 && x < w && y >= 0 && y < h)
        pozitron_draw_pixel(win, x, y, color);
}

static void draw_rect(int win, int x, int y, int w, int h, uint32_t color, int clip_w, int clip_h) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > clip_w) w = clip_w - x;
    if (y + h > clip_h) h = clip_h - y;
    if (w <= 0 || h <= 0) return;
    
    for (int dy = 0; dy < h; dy++)
        for (int dx = 0; dx < w; dx++)
            pozitron_draw_pixel(win, x + dx, y + dy, color);
}

// ПРАВИЛЬНАЯ ОТРИСОВКА ШРИФТА
static void draw_char(int win, int x, int y, unsigned char ch, uint32_t color, int clip_w, int clip_h) {
    int idx;
    
    // Определяем индекс символа
    if (ch >= '0' && ch <= '9') {
        idx = ch - '0';
    } else if (ch >= 'A' && ch <= 'Z') {
        idx = 10 + (ch - 'A');
    } else if (ch >= 'a' && ch <= 'z') {
        idx = 36 + (ch - 'a');
    } else if (ch == ' ') {
        return;
    } else if (ch == ':') {
        // Двоеточие - рисуем простой
        draw_rect(win, x + 2, y + 2, 2, 2, color, clip_w, clip_h);
        draw_rect(win, x + 2, y + 6, 2, 2, color, clip_w, clip_h);
        return;
    } else {
        idx = 0; // По умолчанию '0'
    }
    
    const uint8_t* glyph = FONT_8x12[idx];
    
    // Рисуем символ: каждый байт - столбец пикселей
    for (int col = 0; col < FONT_W; col++) {
        uint8_t column = glyph[col];
        for (int row = 0; row < FONT_H; row++) {
            if (column & (1 << row)) {
                draw_pixel_safe(win, x + col, y + row, color, clip_w, clip_h);
            }
        }
    }
}

static void draw_text(int win, int x, int y, const char* text, uint32_t color, int clip_w, int clip_h) {
    int cx = x;
    while (*text) {
        unsigned char ch = (unsigned char)*text++;
        draw_char(win, cx, y, ch, color, clip_w, clip_h);
        cx += FONT_W + 1;
        if (cx + FONT_W > clip_w) break;
    }
}

static void draw_text_center(int win, int x, int y, const char* text, uint32_t color, int clip_w, int clip_h) {
    int len = 0;
    while (text[len]) len++;
    int text_w = len * (FONT_W + 1) - 1;
    draw_text(win, x - text_w/2, y, text, color, clip_w, clip_h);
}

// ============================================================
// ЛОГИКА ИГРЫ
// ============================================================
static void game_init(Game* g) {
    g->snake.length = 3;
    g->snake.body[0].x = GRID_SIZE / 2;
    g->snake.body[0].y = GRID_SIZE / 2;
    g->snake.body[1].x = GRID_SIZE / 2 - 1;
    g->snake.body[1].y = GRID_SIZE / 2;
    g->snake.body[2].x = GRID_SIZE / 2 - 2;
    g->snake.body[2].y = GRID_SIZE / 2;
    g->snake.direction = 1;
    g->snake.next_direction = 1;
    g->snake.alive = 1;
    g->snake.score = 0;
    g->snake.high_score = 0;
    
    g->food.active = 0;
    g->special_food.active = 0;
    g->special_food_timer = 0;
    
    g->paused = 0;
    g->game_over = 0;
    g->move_delay = 12;
    g->move_timer = 0;
    
    game_spawn_food(g);
}

static void game_spawn_food(Game* g) {
    int attempts = 0;
    int max_attempts = 1000;
    
    while (attempts < max_attempts) {
        int fx = rand() % GRID_SIZE;
        int fy = rand() % GRID_SIZE;
        
        int on_snake = 0;
        for (int i = 0; i < g->snake.length; i++) {
            if (g->snake.body[i].x == fx && g->snake.body[i].y == fy) {
                on_snake = 1;
                break;
            }
        }
        
        if (g->special_food.active && 
            g->special_food.pos.x == fx && 
            g->special_food.pos.y == fy) {
            on_snake = 1;
        }
        
        if (!on_snake) {
            g->food.pos.x = fx;
            g->food.pos.y = fy;
            g->food.type = 0;
            g->food.active = 1;
            return;
        }
        attempts++;
    }
}

static void game_spawn_special_food(Game* g) {
    if (g->special_food.active) return;
    
    int attempts = 0;
    int max_attempts = 1000;
    
    while (attempts < max_attempts) {
        int fx = rand() % GRID_SIZE;
        int fy = rand() % GRID_SIZE;
        
        int on_snake = 0;
        for (int i = 0; i < g->snake.length; i++) {
            if (g->snake.body[i].x == fx && g->snake.body[i].y == fy) {
                on_snake = 1;
                break;
            }
        }
        
        if (!on_snake && !(g->food.active && g->food.pos.x == fx && g->food.pos.y == fy)) {
            g->special_food.pos.x = fx;
            g->special_food.pos.y = fy;
            g->special_food.type = 1;
            g->special_food.active = 1;
            g->special_food_timer = 150;
            return;
        }
        attempts++;
    }
}

static void game_move_snake(Game* g) {
    if (g->paused || g->game_over || !g->snake.alive) return;
    
    g->snake.direction = g->snake.next_direction;
    
    Point new_head = g->snake.body[0];
    
    switch (g->snake.direction) {
        case 0: new_head.y--; break;
        case 1: new_head.x++; break;
        case 2: new_head.y++; break;
        case 3: new_head.x--; break;
    }
    
    if (new_head.x < 0 || new_head.x >= GRID_SIZE || 
        new_head.y < 0 || new_head.y >= GRID_SIZE) {
        g->snake.alive = 0;
        g->game_over = 1;
        return;
    }
    
    for (int i = 0; i < g->snake.length; i++) {
        if (g->snake.body[i].x == new_head.x && 
            g->snake.body[i].y == new_head.y) {
            g->snake.alive = 0;
            g->game_over = 1;
            return;
        }
    }
    
    int ate_food = 0;
    if (g->food.active && 
        g->food.pos.x == new_head.x && 
        g->food.pos.y == new_head.y) {
        ate_food = 1;
        g->food.active = 0;
        g->snake.score += 10;
        g->move_delay = (g->move_delay > 4) ? g->move_delay - 1 : 4;
    }
    
    int ate_special = 0;
    if (g->special_food.active &&
        g->special_food.pos.x == new_head.x &&
        g->special_food.pos.y == new_head.y) {
        ate_special = 1;
        g->special_food.active = 0;
        g->snake.score += 50;
        g->special_food_timer = 0;
    }
    
    for (int i = g->snake.length - 1; i > 0; i--) {
        g->snake.body[i] = g->snake.body[i-1];
    }
    g->snake.body[0] = new_head;
    
    if (ate_food) {
        g->snake.length++;
        g->snake.body[g->snake.length - 1] = g->snake.body[g->snake.length - 2];
        game_spawn_food(g);
        
        if (rand() % 10 < 3) {
            game_spawn_special_food(g);
        }
    }
    
    if (ate_special) {
        g->snake.length += 2;
    }
}

// ============================================================
// ОТРИСОВКА
// ============================================================
static void game_draw_grid(Game* g) {
    for (int y = 0; y < GRID_SIZE; y++) {
        for (int x = 0; x < GRID_SIZE; x++) {
            int px = g->grid_x + x * g->cell_size;
            int py = g->grid_y + y * g->cell_size;
            
            uint32_t color = 0x2A2A32;
            if ((x + y) % 2 == 0) {
                color = 0x22222A;
            }
            
            draw_rect(g->window_id, px, py, g->cell_size, g->cell_size, 
                     color, g->view_w, g->view_h);
        }
    }
}

static void game_draw_snake(Game* g) {
    for (int i = 0; i < g->snake.length; i++) {
        int px = g->grid_x + g->snake.body[i].x * g->cell_size;
        int py = g->grid_y + g->snake.body[i].y * g->cell_size;
        
        uint32_t color;
        if (i == 0) {
            color = COLORS[C_SNAKE_HEAD];
            int eye_offset = g->cell_size / 4;
            int eye_size = g->cell_size / 6;
            
            int ex1, ey1, ex2, ey2;
            switch (g->snake.direction) {
                case 0:
                    ex1 = px + eye_offset; ey1 = py + eye_offset;
                    ex2 = px + g->cell_size - eye_offset - eye_size; ey2 = py + eye_offset;
                    break;
                case 1:
                    ex1 = px + g->cell_size - eye_offset - eye_size; ey1 = py + eye_offset;
                    ex2 = px + g->cell_size - eye_offset - eye_size; ey2 = py + g->cell_size - eye_offset - eye_size;
                    break;
                case 2:
                    ex1 = px + eye_offset; ey1 = py + g->cell_size - eye_offset - eye_size;
                    ex2 = px + g->cell_size - eye_offset - eye_size; ey2 = py + g->cell_size - eye_offset - eye_size;
                    break;
                case 3:
                    ex1 = px + eye_offset; ey1 = py + eye_offset;
                    ex2 = px + eye_offset; ey2 = py + g->cell_size - eye_offset - eye_size;
                    break;
            }
            
            draw_rect(g->window_id, ex1, ey1, eye_size, eye_size, 0xFFFFFF, g->view_w, g->view_h);
            draw_rect(g->window_id, ex2, ey2, eye_size, eye_size, 0xFFFFFF, g->view_w, g->view_h);
            
            int pupil_size = eye_size / 2;
            int poff = eye_size / 4;
            draw_rect(g->window_id, ex1 + poff, ey1 + poff, pupil_size, pupil_size, 0x000000, g->view_w, g->view_h);
            draw_rect(g->window_id, ex2 + poff, ey2 + poff, pupil_size, pupil_size, 0x000000, g->view_w, g->view_h);
            
        } else if (i % 2 == 0) {
            color = COLORS[C_SNAKE_BODY];
        } else {
            color = COLORS[C_SNAKE_BODY_DARK];
        }
        
        int margin = 1;
        draw_rect(g->window_id, px + margin, py + margin, 
                 g->cell_size - margin*2, g->cell_size - margin*2,
                 color, g->view_w, g->view_h);
    }
}

static void game_draw_food(Game* g) {
    if (!g->food.active) return;
    
    int px = g->grid_x + g->food.pos.x * g->cell_size;
    int py = g->grid_y + g->food.pos.y * g->cell_size;
    
    float pulse = 0.8f + 0.2f * (g->frame_count % 30) / 30.0f;
    int size = (int)(g->cell_size * pulse * 0.6f);
    int offset = (g->cell_size - size) / 2;
    
    draw_rect(g->window_id, px + offset, py + offset, size, size,
             COLORS[C_FOOD_COLOR], g->view_w, g->view_h);
    
    int highlight = size / 3;
    draw_rect(g->window_id, px + offset + 2, py + offset + 2, highlight, highlight,
             0xFFFFFF, g->view_w, g->view_h);
}

static void game_draw_special_food(Game* g) {
    if (!g->special_food.active) return;
    
    if (g->special_food_timer < 30 && (g->special_food_timer % 6) < 3) {
        return;
    }
    
    int px = g->grid_x + g->special_food.pos.x * g->cell_size;
    int py = g->grid_y + g->special_food.pos.y * g->cell_size;
    
    float pulse = 0.7f + 0.3f * (g->frame_count % 40) / 40.0f;
    int size = (int)(g->cell_size * pulse * 0.7f);
    int offset = (g->cell_size - size) / 2;
    
    draw_rect(g->window_id, px + offset, py + offset, size, size,
             COLORS[C_FOOD_SPECIAL], g->view_w, g->view_h);
    
    int star_size = size / 3;
    draw_rect(g->window_id, px + offset + star_size, py + offset, star_size, star_size,
             0xFFFFFF, g->view_w, g->view_h);
    draw_rect(g->window_id, px + offset, py + offset + star_size, star_size, star_size,
             0xFFFFFF, g->view_w, g->view_h);
    draw_rect(g->window_id, px + offset + star_size*2, py + offset + star_size, 
             star_size, star_size, 0xFFFFFF, g->view_w, g->view_h);
    draw_rect(g->window_id, px + offset + star_size, py + offset + star_size*2, 
             star_size, star_size, 0xFFFFFF, g->view_w, g->view_h);
}

static void game_draw_ui(Game* g) {
    draw_rect(g->window_id, 0, 0, g->view_w, 32,
             COLORS[C_BG_HEADER], g->view_w, g->view_h);
    draw_text(g->window_id, 12, 10, "SNAKE", COLORS[C_ACCENT_LIGHT], g->view_w, g->view_h);
    
    char score_text[32];
    int_to_str(g->snake.score, score_text);
    draw_text(g->window_id, 120, 10, "Score:", COLORS[C_TEXT_SECONDARY], g->view_w, g->view_h);
    draw_text(g->window_id, 180, 10, score_text, COLORS[C_TEXT_PRIMARY], g->view_w, g->view_h);
    
    char high_text[32];
    int_to_str(g->snake.high_score, high_text);
    draw_text(g->window_id, 280, 10, "Best:", COLORS[C_TEXT_SECONDARY], g->view_w, g->view_h);
    draw_text(g->window_id, 330, 10, high_text, COLORS[C_ACCENT_HOVER], g->view_w, g->view_h);
    
    draw_text(g->window_id, g->view_w - 380, 10, "Arrows:Move", COLORS[C_TEXT_DIM], g->view_w, g->view_h);
    draw_text(g->window_id, g->view_w - 220, 10, "P:Pause", COLORS[C_TEXT_DIM], g->view_w, g->view_h);
    draw_text(g->window_id, g->view_w - 120, 10, "ESC:Quit", COLORS[C_TEXT_DIM], g->view_w, g->view_h);
    
    draw_rect(g->window_id, 0, 32, g->view_w, 1,
             COLORS[C_BORDER], g->view_w, g->view_h);
    
    if (g->paused) {
        draw_rect(g->window_id, g->view_w/2 - 150, g->view_h/2 - 40, 300, 80,
                 0x80000000, g->view_w, g->view_h);
        draw_text_center(g->window_id, g->view_w/2, g->view_h/2 - 10,
                        "PAUSED", COLORS[C_ACCENT_LIGHT], g->view_w, g->view_h);
        draw_text_center(g->window_id, g->view_w/2, g->view_h/2 + 20,
                        "Press P to continue", COLORS[C_TEXT_DIM], g->view_w, g->view_h);
    }
    
    if (g->game_over) {
        draw_rect(g->window_id, g->view_w/2 - 180, g->view_h/2 - 60, 360, 120,
                 0xCC000000, g->view_w, g->view_h);
        
        draw_rect(g->window_id, g->view_w/2 - 180, g->view_h/2 - 60, 360, 1,
                 COLORS[C_FOOD_COLOR], g->view_w, g->view_h);
        draw_rect(g->window_id, g->view_w/2 - 180, g->view_h/2 + 60, 360, 1,
                 COLORS[C_FOOD_COLOR], g->view_w, g->view_h);
        draw_rect(g->window_id, g->view_w/2 - 180, g->view_h/2 - 60, 1, 120,
                 COLORS[C_FOOD_COLOR], g->view_w, g->view_h);
        draw_rect(g->window_id, g->view_w/2 + 180, g->view_h/2 - 60, 1, 120,
                 COLORS[C_FOOD_COLOR], g->view_w, g->view_h);
        
        draw_text_center(g->window_id, g->view_w/2, g->view_h/2 - 30,
                        "GAME OVER", COLORS[C_FOOD_COLOR], g->view_w, g->view_h);
        
        char final_score[64];
        int_to_str(g->snake.score, final_score);
        draw_text_center(g->window_id, g->view_w/2, g->view_h/2 + 5,
                        "Score:", COLORS[C_TEXT_SECONDARY], g->view_w, g->view_h);
        draw_text(g->window_id, g->view_w/2 + 20, g->view_h/2 + 5,
                 final_score, COLORS[C_TEXT_PRIMARY], g->view_w, g->view_h);
        
        draw_text_center(g->window_id, g->view_w/2, g->view_h/2 + 35,
                        "Press R to restart", COLORS[C_TEXT_DIM], g->view_w, g->view_h);
    }
}

static void game_draw(Game* g) {
    if (!g->is_visible) {
        g->need_redraw = 1;
        return;
    }
    
    if (!g->need_redraw) return;
    
    draw_rect(g->window_id, 0, 0, g->view_w, g->view_h,
             COLORS[C_BG_MAIN], g->view_w, g->view_h);
    
    game_draw_grid(g);
    game_draw_food(g);
    game_draw_special_food(g);
    game_draw_snake(g);
    game_draw_ui(g);
    
    g->need_redraw = 0;
}

// ============================================================
// ОБРАБОТКА ВВОДА
// ============================================================
static void game_handle_input(Game* g, input_event_t* ev) {
    if (ev->type == POZ_EVENT_KEY_DOWN) {
        uint8_t sc = ev->key.scancode;
        
        if (sc == POZ_KEY_ESC) {
            g->running = 0;
        }
        else if (sc == POZ_KEY_P) {
            if (!g->game_over) {
                g->paused = !g->paused;
                g->need_redraw = 1;
            }
        }
        else if (sc == POZ_KEY_R && g->game_over) {
            game_init(g);
            g->need_redraw = 1;
        }
        else if (!g->paused && !g->game_over) {
            switch (sc) {
                case POZ_KEY_UP:
                    if (g->snake.direction != 2)
                        g->snake.next_direction = 0;
                    break;
                case POZ_KEY_DOWN:
                    if (g->snake.direction != 0)
                        g->snake.next_direction = 2;
                    break;
                case POZ_KEY_LEFT:
                    if (g->snake.direction != 1)
                        g->snake.next_direction = 3;
                    break;
                case POZ_KEY_RIGHT:
                    if (g->snake.direction != 3)
                        g->snake.next_direction = 1;
                    break;
            }
        }
    }
}

// ============================================================
// MAIN
// ============================================================
int main(int argc, char** argv) {
    (void)argc; (void)argv;
    
    Game game;
    memset(&game, 0, sizeof(Game));
    
    rand_seed = 0xDEADBEEF;
    for (int i = 0; i < 10; i++) rand();
    
    win_params_t wp;
    wp.title = "Snake Game";
    wp.x = 80;
    wp.y = 40;
    wp.width = 700;
    wp.height = 600;
    wp.flags = WINDOW_CLOSABLE | WINDOW_MOVABLE | WINDOW_HAS_TITLE | WINDOW_RESIZABLE | WINDOW_MAXIMIZABLE;
    
    game.window_id = pozitron_create_window(&wp);
    if (game.window_id < 0) {
        pozitron_exit(1);
    }
    
    pozitron_enable_canvas(game.window_id);
    pozitron_enable_input(game.window_id, 1);
    
    window_info_t winfo;
    if (pozitron_get_window_info(game.window_id, &winfo) == 0) {
        game.view_w = winfo.client_width;
        game.view_h = winfo.client_height;
        game.is_visible = winfo.is_visible;
    }
    
    int padding_x = 60;
    int padding_y = 60;
    int grid_width = game.view_w - padding_x * 2;
    int grid_height = game.view_h - padding_y * 2 - 40;
    game.cell_size = (grid_width < grid_height) ? grid_width / GRID_SIZE : grid_height / GRID_SIZE;
    if (game.cell_size < 8) game.cell_size = 8;
    
    game.grid_w = game.cell_size * GRID_SIZE;
    game.grid_h = game.cell_size * GRID_SIZE;
    game.grid_x = (game.view_w - game.grid_w) / 2;
    game.grid_y = (game.view_h - game.grid_h) / 2 + 20;
    
    game_init(&game);
    game.running = 1;
    game.need_redraw = 1;
    
    input_event_t ev;
    
    while (game.running) {
        if (pozitron_get_window_info(game.window_id, &winfo) == 0) {
            int old_w = game.view_w;
            int old_h = game.view_h;
            game.view_w = winfo.client_width;
            game.view_h = winfo.client_height;
            game.is_visible = winfo.is_visible;
            
            if (old_w != game.view_w || old_h != game.view_h) {
                grid_width = game.view_w - padding_x * 2;
                grid_height = game.view_h - padding_y * 2 - 40;
                game.cell_size = (grid_width < grid_height) ? grid_width / GRID_SIZE : grid_height / GRID_SIZE;
                if (game.cell_size < 8) game.cell_size = 8;
                game.grid_w = game.cell_size * GRID_SIZE;
                game.grid_h = game.cell_size * GRID_SIZE;
                game.grid_x = (game.view_w - game.grid_w) / 2;
                game.grid_y = (game.view_h - game.grid_h) / 2 + 20;
                game.need_redraw = 1;
            }
        }
        
        if (game.is_visible) {
            while (pozitron_poll_input(game.window_id, &ev)) {
                game_handle_input(&game, &ev);
            }
        }
        
        if (game.is_visible && !game.paused && !game.game_over && game.snake.alive) {
            game.move_timer++;
            
            if (game.move_timer >= game.move_delay) {
                game.move_timer = 0;
                game_move_snake(&game);
                game.need_redraw = 1;
            }
            
            if (game.special_food.active) {
                game.special_food_timer--;
                if (game.special_food_timer <= 0) {
                    game.special_food.active = 0;
                    game.need_redraw = 1;
                }
            }
            
            if (game.snake.score > game.snake.high_score) {
                game.snake.high_score = game.snake.score;
            }
            
            game.frame_count++;
        }
        
        if (game.is_visible && game.need_redraw) {
            game_draw(&game);
            pozitron_flush_window(game.window_id);
        }
        
        if (!game.is_visible) {
            pozitron_msleep(200);
            continue;
        }
        
        pozitron_msleep(16);
    }
    
    pozitron_close_window(game.window_id);
    pozitron_exit(0);
    return 0;
}