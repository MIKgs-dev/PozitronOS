#include "drivers/serial.h"
#include "drivers/vga.h"
#include "drivers/pic.h"
#include "drivers/timer.h"
#include "drivers/keyboard.h"
#include "drivers/mouse.h"
#include "drivers/cmos.h"
#include "core/gdt.h"
#include "core/idt.h"
#include "core/isr.h"
#include "core/event.h"
#include "drivers/vesa.h"
#include "kernel/memory.h"
#include "gui/gui.h"
#include "hw/scanner.h"
#include "drivers/power.h"
#include "gui/shutdown.h"
#include "kernel/multiboot_util.h"
#include "kernel/logo.h"
#include "stddef.h"
#include "lib/string.h"
#include "kernel/scheduler.h"
#include "kernel/paging.h"
#include "kernel/userspace.h"
#include "kernel/device.h"
#include "drivers/usb/usb.h"
#include "drivers/usb/scsi_cmds.h"
#include "drivers/ahci.h"

static uint8_t system_running = 1;

#define EVENT_PACK_MOUSE(x, y, btn) (((x) & 0xFFFF) | (((y) & 0xFFFF) << 16) | (((btn) & 0xFF) << 24))
#define EVENT_UNPACK_X(data) ((int16_t)((data) & 0xFFFF))
#define EVENT_UNPACK_Y(data) ((int16_t)(((data) >> 16) & 0xFFFF))
#define EVENT_UNPACK_BUTTON(data) (((data) >> 24) & 0xFF)

extern void check_stack_overflow(void);

static void update_progress_callback(Widget* button, void* userdata) {
    if (!button || !userdata) return;
    
    Widget* progressbar = (Widget*)userdata;
    static uint32_t progress_value = 0;
    
    progress_value += 10;
    if (progress_value > 100) progress_value = 0;
    
    wg_set_progressbar_value(progressbar, progress_value);
    
    serial_puts("[DEMO] Progress updated: ");
    serial_puts_num(progress_value);
    serial_puts("%\n");
}

static void slider_changed_callback(Widget* slider, void* userdata) {
    if (!slider) return;
    
    uint32_t value = wg_get_slider_value(slider);
    
    serial_puts("[DEMO] Slider changed: ");
    serial_puts_num(value);
    serial_puts("\n");
}

static void checkbox_toggled_callback(Widget* checkbox, void* userdata) {
    if (!checkbox) return;
    
    uint8_t checked = wg_get_checkbox_state(checkbox);
    
    serial_puts("[DEMO] Checkbox ");
    serial_puts(checked ? "checked" : "unchecked");
    if (checkbox->text) {
        serial_puts(": ");
        serial_puts(checkbox->text);
    }
    serial_puts("\n");
}

static void list_item_selected_callback(Widget* list, void* userdata) {
    if (!list) return;
    
    uint32_t selected = wg_list_get_selected(list);
    serial_puts("[DEMO] List item selected: ");
    serial_puts_num(selected);
    serial_puts("\n");
}

static void dropdown_changed_callback(Widget* dropdown, void* userdata) {
    if (!dropdown) return;
    
    uint32_t selected = wg_dropdown_get_selected(dropdown);
    serial_puts("[DEMO] Dropdown changed: ");
    serial_puts_num(selected);
    serial_puts("\n");
}

static void tab_changed_callback(Widget* tab, void* userdata) {
    if (!tab) return;
    
    uint32_t active = wg_tab_get_active(tab);
    serial_puts("[DEMO] Tab changed: ");
    serial_puts_num(active);
    serial_puts("\n");
}

static void menubar_item_callback(void* data) {
    char* item_name = (char*)data;
    serial_puts("[DEMO] Menu item clicked: ");
    serial_puts(item_name);
    serial_puts("\n");
}

static void close_demo_window(Widget* button, void* userdata) {
    if (!userdata) return;
    
    Window* window = (Window*)userdata;
    wm_close_window(window);
    
    serial_puts("[DEMO] Demo window closed\n");
}

static void input_changed_callback(Widget* input, void* userdata) {
    if (!input) return;
    
    char* text = wg_input_get_text(input);
    serial_puts("[DEMO] Input changed: ");
    serial_puts(text);
    serial_puts("\n");
}

static void create_showcase_window(void) {
    serial_puts("\n=== CREATING GUI SHOWCASE WINDOW ===\n");
    
    uint32_t screen_width = vesa_get_width();
    uint32_t screen_height = vesa_get_height();
    
    uint32_t win_width = 800;
    uint32_t win_height = 600;
    uint32_t win_x = (screen_width - win_width) / 2;
    uint32_t win_y = (screen_height - win_height) / 2 - 20;
    
    Window* win = wm_create_window("PozitronOS GUI Showcase",
                                  win_x, win_y, win_width, win_height,
                                  WINDOW_CLOSABLE | WINDOW_MOVABLE | 
                                  WINDOW_HAS_TITLE | WINDOW_MINIMIZABLE |
                                  WINDOW_MAXIMIZABLE | WINDOW_RESIZABLE);
    
    if (!win) {
        serial_puts("[DEMO] ERROR: Failed to create showcase window\n");
        return;
    }
    
    Widget* menubar = wg_create_menubar(win, 0.0f, 0.05f, 1.0f);
    if (menubar) {
        uint32_t file_menu = wg_menubar_add_menu(menubar, "File");
        uint32_t edit_menu = wg_menubar_add_menu(menubar, "Edit");
        uint32_t view_menu = wg_menubar_add_menu(menubar, "View");
        uint32_t help_menu = wg_menubar_add_menu(menubar, "Help");
        
        wg_menubar_add_item(menubar, file_menu, "New", menubar_item_callback, "New");
        wg_menubar_add_item(menubar, file_menu, "Open...", menubar_item_callback, "Open");
        wg_menubar_add_item(menubar, file_menu, "Save", menubar_item_callback, "Save");
        wg_menubar_add_item(menubar, file_menu, "Save As...", menubar_item_callback, "Save As");
        wg_menubar_add_item(menubar, file_menu, "Exit", menubar_item_callback, "Exit");
        
        wg_menubar_add_item(menubar, edit_menu, "Undo", menubar_item_callback, "Undo");
        wg_menubar_add_item(menubar, edit_menu, "Redo", menubar_item_callback, "Redo");
        wg_menubar_add_item(menubar, edit_menu, "Cut", menubar_item_callback, "Cut");
        wg_menubar_add_item(menubar, edit_menu, "Copy", menubar_item_callback, "Copy");
        wg_menubar_add_item(menubar, edit_menu, "Paste", menubar_item_callback, "Paste");
        
        wg_menubar_add_item(menubar, view_menu, "Zoom In", menubar_item_callback, "Zoom In");
        wg_menubar_add_item(menubar, view_menu, "Zoom Out", menubar_item_callback, "Zoom Out");
        wg_menubar_add_item(menubar, view_menu, "Full Screen", menubar_item_callback, "Full Screen");
        
        wg_menubar_add_item(menubar, help_menu, "About", menubar_item_callback, "About");
        wg_menubar_add_item(menubar, help_menu, "Documentation", menubar_item_callback, "Docs");
    }
    
    float left_col = 0.03f;
    float col_width = 0.30f;
    float row_height = 0.06f;
    float current_y = 0.15f;
    
    wg_create_label(win, "Buttons:", left_col, current_y);
    current_y += 0.03f;
    
    wg_create_button(win, "Button 1", left_col, current_y, 0.12f, 0.04f, NULL, NULL);
    wg_create_button(win, "Button 2", left_col + 0.13f, current_y, 0.12f, 0.04f, NULL, NULL);
    wg_create_button(win, "Button 3", left_col + 0.26f, current_y, 0.12f, 0.04f, NULL, NULL);
    
    current_y += 0.06f;
    
    wg_create_label(win, "Checkboxes:", left_col, current_y);
    current_y += 0.03f;
    
    Widget* cb1 = wg_create_checkbox(win, "Option 1", left_col, current_y, 1);
    Widget* cb2 = wg_create_checkbox(win, "Option 2", left_col, current_y + 0.04f, 0);
    Widget* cb3 = wg_create_checkbox(win, "Option 3", left_col, current_y + 0.08f, 0);
    
    if (cb1) wg_set_callback(cb1, checkbox_toggled_callback, NULL);
    if (cb2) wg_set_callback(cb2, checkbox_toggled_callback, NULL);
    if (cb3) wg_set_callback(cb3, checkbox_toggled_callback, NULL);
    
    current_y += 0.14f;
    
    float center_col = 0.36f;
    current_y = 0.15f;
    
    wg_create_label(win, "Sliders:", center_col, current_y);
    current_y += 0.03f;
    
    wg_create_label(win, "Volume:", center_col, current_y);
    current_y += 0.02f;
    Widget* slider1 = wg_create_slider(win, center_col, current_y, 0.25f, 0.03f, 0, 100, 50);
    if (slider1) wg_set_callback(slider1, slider_changed_callback, NULL);
    
    current_y += 0.05f;
    
    wg_create_label(win, "Brightness:", center_col, current_y);
    current_y += 0.02f;
    Widget* slider2 = wg_create_slider(win, center_col, current_y, 0.25f, 0.03f, 0, 100, 75);
    if (slider2) wg_set_callback(slider2, slider_changed_callback, NULL);
    
    current_y += 0.08f;
    
    wg_create_label(win, "Progress:", center_col, current_y);
    current_y += 0.03f;
    
    Widget* progress1 = wg_create_progressbar(win, center_col, current_y, 0.25f, 0.03f, 30);
    current_y += 0.04f;
    Widget* progress2 = wg_create_progressbar(win, center_col, current_y, 0.25f, 0.03f, 60);
    
    current_y += 0.05f;
    Widget* update_btn = wg_create_button(win, "Update", center_col, current_y, 0.15f, 0.04f,
                                         update_progress_callback, progress1);
    
    float right_col = 0.70f;
    current_y = 0.15f;
    
    wg_create_label(win, "List:", right_col, current_y);
    current_y += 0.03f;
    
    Widget* list = wg_create_list(win, right_col, current_y, 0.25f, 0.20f, 6);
    if (list) {
        wg_list_add_item(list, "Item 1", NULL);
        wg_list_add_item(list, "Item 2", NULL);
        wg_list_add_item(list, "Item 3", NULL);
        wg_list_add_item(list, "Item 4", NULL);
        wg_list_add_item(list, "Item 5", NULL);
        wg_list_add_item(list, "Item 6", NULL);
        wg_list_add_item(list, "Item 7", NULL);
        wg_list_add_item(list, "Item 8", NULL);
        wg_list_add_item(list, "Item 9", NULL);
        wg_list_add_item(list, "Item 10", NULL);
        wg_list_set_selected(list, 2);
        list->on_change = list_item_selected_callback;
    }
    
    current_y += 0.23f;
    
    wg_create_label(win, "Dropdown:", right_col, current_y);
    current_y += 0.03f;
    
    Widget* dropdown = wg_create_dropdown(win, right_col, current_y, 0.25f, 0.04f, 5);
    if (dropdown) {
        wg_dropdown_add_item(dropdown, "Option A", NULL);
        wg_dropdown_add_item(dropdown, "Option B", NULL);
        wg_dropdown_add_item(dropdown, "Option C", NULL);
        wg_dropdown_add_item(dropdown, "Option D", NULL);
        wg_dropdown_add_item(dropdown, "Option E", NULL);
        wg_dropdown_set_selected(dropdown, 0);
        dropdown->on_change = dropdown_changed_callback;
    }
    
    current_y += 0.07f;
    
    wg_create_label(win, "Scrollbar:", right_col, current_y);
    current_y += 0.03f;
    
    Widget* scroll = wg_create_scrollbar(win, right_col + 0.10f, current_y, 0.03f, 0.12f, 1);
    if (scroll) {
        wg_scrollbar_set_range(scroll, 0, 100, 10);
        wg_scrollbar_set_value(scroll, 30);
    }
    
    float bottom_y = 0.70f;
    
    wg_create_label(win, "Text Input:", left_col, bottom_y);
    
    Widget* input1 = wg_create_input(win, left_col, bottom_y + 0.04f, 0.30f, 0.04f, "Type here...");
    if (input1) {
        input1->can_focus = 1;
        input1->on_change = input_changed_callback;
    }
    
    Widget* input2 = wg_create_input(win, left_col, bottom_y + 0.09f, 0.30f, 0.04f, "Password");
    if (input2) {
        input2->can_focus = 1;
        InputData* data = (InputData*)input2->data;
        if (data) data->password_mode = 1;
        input2->on_change = input_changed_callback;
    }
    
    Widget* input3 = wg_create_input(win, left_col, bottom_y + 0.14f, 0.30f, 0.04f, 
                                     "Long text for scrolling demo");
    if (input3) {
        input3->can_focus = 1;
        input3->on_change = input_changed_callback;
    }
    
    Widget* close_btn = wg_create_button(win, "Close Window", 0.70f, 0.85f, 0.25f, 0.05f,
                                        close_demo_window, win);
    
    serial_puts("[DEMO] Showcase window created successfully\n");
    serial_puts("[DEMO] Press F1 to open this window again\n");
}

static void handle_keyboard_events(event_t* event) {
    if (!event) return;
    
    if (event->type == EVENT_KEY_PRESS) {
        switch (event->data1) {
            case 0x3B:
                create_showcase_window();
                break;
                
            case 0x3C:
                wm_dump_info();
                break;
                
            case 0x01:
                if (gui_state.focused_window && 
                    IS_VALID_WINDOW_PTR(gui_state.focused_window)) {
                    wm_close_window(gui_state.focused_window);
                }
                break;
                
            case 0x57:
                if (gui_state.focused_window && 
                    IS_VALID_WINDOW_PTR(gui_state.focused_window) &&
                    gui_state.focused_window->maximizable) {
                    if (gui_state.focused_window->maximized) {
                        wm_restore_window(gui_state.focused_window);
                    } else {
                        wm_maximize_window(gui_state.focused_window);
                    }
                }
                break;
                
            case 0x0F:
                if (event->data2 & 0x02) {
                    gui_focus_prev();
                } else {
                    gui_focus_next();
                }
                break;
                
            case 0x5B:
            case 0x5C:
                start_menu_toggle();
                break;
                
            default:
                break;
        }
    }
}

void kernel_main(uint32_t magic, multiboot_info_t* mb_info) {
    multiboot_dump_info(mb_info);
    memory_init_multiboot(mb_info);
    extern uint32_t stack_guard;
    stack_guard = 0xDEADBEEF;

    serial_init();
    vga_init();
    vga_puts("\n");

    gdt_init();
    vga_puts("[ OK ] GDT OK\n");
    idt_init();
    vga_puts("[ OK ] IDT OK\n");
    pic_init();
    vga_puts("[ OK ] PIC OK\n");
    isr_init();
    vga_puts("[ OK ] ISR OK\n");
    asm volatile("sti");

    timer_init(100);
    vga_puts("[ OK ] TIMER OK\n");
    keyboard_init();
    
    memory_init();
    print_memory_map();
    memory_dump();
    vga_puts("[ OK ] MEMORY ALLOCATION SYSTEM OK\n");
    
    if(!vesa_init(mb_info)) {
        vga_puts("[ERROR] VBE/VESA INITIALISATION FAILED\n");
    } else {
        vga_puts("[ OK ] VBE/VESA OK\n");
    }
    vesa_enable_double_buffer();

    show_boot_logo();
    boot_progress = 15;
    update_boot_progress();

    cmos_init();
    vga_puts("[ OK ] CMOS RTC OK\n");
    boot_progress = 25;
    update_boot_progress();

    scanner_init();
    vga_puts("[INFO] SCANNING HARDWARE START\n");
    scanner_scan_all();
    scanner_dump_all();
    vga_puts("[ OK ] SCANNING HARDWARE FINISH\n");

    boot_progress = 40;
    update_boot_progress();

    device_init();

    init_devices();
    hci_init();
    poll_usb();
    
    boot_progress = 50;
    update_boot_progress();

    paging_init();

    scheduler_init();

    ahci_init();

    boot_progress = 60;
    update_boot_progress();

    uint32_t screen_width = vesa_get_width();
    uint32_t screen_height = vesa_get_height();

    vesa_cache_background();
    vesa_init_dirty();
    vesa_mark_dirty_all();
    vesa_cursor_init();
    vesa_cursor_set_visible(1);
    boot_progress = 70;
    update_boot_progress();

    mouse_init();
    boot_progress = 90;
    update_boot_progress();

    event_init();
    vga_puts("[ OK ] EVENT SYSTEM OK\n");

    boot_progress = 100;
    update_boot_progress();
    fade_out_boot_logo();

    if (vesa_is_background_cached()) {
        vesa_restore_background();
    }

    vga_puts("[INFO] STARTUP GUI ENVIRONMENT\n");
    gui_init(screen_width, screen_height);

    taskbar_init();

    vga_puts("[ OK ] GUI ENVIRONMENT OK\n");

    serial_puts("\n=== SYSTEM READY ===\n");
    vga_puts("[INFO] SYSTEMS READY\n");

    gui_render();
    vesa_cursor_update();
    if (vesa_is_double_buffer_enabled()) {
        vesa_swap_buffers();
    }

    while(system_running) {
        check_stack_overflow();
        asm volatile("hlt");

        event_t event;
        while (event_poll(&event)) {
            gui_handle_event(&event);
            handle_keyboard_events(&event);
        }

        poll_usb();

        vesa_hide_cursor();

        if (is_shutdown_mode_active()) {
            update_shutdown_animation();
        }

        if (vesa_is_background_cached()) {
            vesa_restore_background_dirty();
        }

        gui_render();

        vesa_show_cursor();
        vesa_cursor_update();

        if (vesa_is_double_buffer_enabled()) {
            vesa_swap_buffers();
        }
    }
}