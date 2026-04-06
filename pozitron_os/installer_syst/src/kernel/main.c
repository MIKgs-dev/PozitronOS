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
#include "lib/mini_printf.h"
#include "kernel/paging.h"
#include "kernel/device.h"
#include "kernel/notif.h"
#include "drivers/ahci.h"
#include "drivers/ata.h"
#include "drivers/disk.h"
#include "kernel/timer_utils.h"

static uint8_t system_running = 1;
static uint32_t system_img_start = 0;
static uint32_t system_img_end = 0;
static uint32_t system_img_size = 0;

static Window* main_window = NULL;
static Widget* progress_bar = NULL;
static Widget* status_label = NULL;
static uint32_t selected_disk = 0;
static uint8_t installing = 0;

static enum {
    STAGE_WELCOME,
    STAGE_SELECT_DISK,
    STAGE_INSTALLING,
    STAGE_COMPLETE
} current_stage = STAGE_WELCOME;

extern void check_stack_overflow(void);
uint8_t taskbar_disabled = 1;

static void completion_message(void) {
    serial_puts("[INSTALLER] Installation complete. Please reboot your computer.\n");
    
    if (main_window->first_widget) {
        Widget* w = main_window->first_widget;
        while (w) {
            Widget* next = w->next;
            wg_destroy_widget(w);
            w = next;
        }
        main_window->first_widget = NULL;
        main_window->last_widget = NULL;
    }
    
    wg_create_label(main_window, "                Installation Complete!", 0.1f, 0.25f);
    wg_create_label(main_window, "       PozitronOS has been successfully installed.", 0.1f, 0.38f);
    wg_create_label(main_window, "     Please reboot your computer to start the system.", 0.1f, 0.45f);
    wg_create_label(main_window, "You may now turn off the computer.", 0.25f, 0.52f);
    
    main_window->needs_redraw = 1;
    gui_render();
    if (vesa_is_double_buffer_enabled()) vesa_swap_buffers();
    
    current_stage = STAGE_COMPLETE;
}

static void update_install_progress(uint32_t current, uint32_t total, uint32_t bytes_written, uint32_t total_bytes, uint32_t elapsed_ms) {
    if (!progress_bar) return;
    
    uint32_t percent = (current * 100) / total;
    wg_set_progressbar_value(progress_bar, percent);
    
    char status[128];
    int pos = 0;
    
    status[pos++] = 'W';
    status[pos++] = 'r';
    status[pos++] = 'i';
    status[pos++] = 't';
    status[pos++] = 'i';
    status[pos++] = 'n';
    status[pos++] = 'g';
    status[pos++] = ':';
    status[pos++] = ' ';
    
    if (percent < 10) {
        status[pos++] = ' ';
        status[pos++] = ' ';
        status[pos++] = '0' + percent;
    } else if (percent < 100) {
        status[pos++] = ' ';
        status[pos++] = '0' + (percent / 10);
        status[pos++] = '0' + (percent % 10);
    } else {
        status[pos++] = '1';
        status[pos++] = '0';
        status[pos++] = '0';
    }
    status[pos++] = '%';
    status[pos++] = ' ';
    
    if (elapsed_ms > 0 && bytes_written > 0) {
        uint32_t speed_kb = (bytes_written / 1024) / (elapsed_ms / 1000 + 1);
        status[pos++] = '(';
        
        if (speed_kb < 10) {
            status[pos++] = '0' + speed_kb;
        } else if (speed_kb < 100) {
            status[pos++] = '0' + (speed_kb / 10);
            status[pos++] = '0' + (speed_kb % 10);
        } else if (speed_kb < 1000) {
            status[pos++] = '0' + (speed_kb / 100);
            status[pos++] = '0' + ((speed_kb / 10) % 10);
            status[pos++] = '0' + (speed_kb % 10);
        } else {
            status[pos++] = '0' + (speed_kb / 1000);
            status[pos++] = '0' + ((speed_kb / 100) % 10);
            status[pos++] = '0' + ((speed_kb / 10) % 10);
            status[pos++] = '0' + (speed_kb % 10);
        }
        
        status[pos++] = ' ';
        status[pos++] = 'K';
        status[pos++] = 'B';
        status[pos++] = '/';
        status[pos++] = 's';
        status[pos++] = ')';
        status[pos++] = ' ';
    }
    
    if (elapsed_ms > 0 && percent > 0 && percent < 100) {
        uint32_t remaining_ms = (elapsed_ms * (100 - percent)) / percent;
        uint32_t remaining_sec = remaining_ms / 1000;
        
        status[pos++] = '(';
        status[pos++] = '~';
        
        if (remaining_sec < 60) {
            if (remaining_sec < 10) {
                status[pos++] = '0';
                status[pos++] = '0' + remaining_sec;
            } else {
                status[pos++] = '0' + (remaining_sec / 10);
                status[pos++] = '0' + (remaining_sec % 10);
            }
            status[pos++] = 's';
        } else {
            uint32_t minutes = remaining_sec / 60;
            uint32_t seconds = remaining_sec % 60;
            if (minutes < 10) {
                status[pos++] = '0';
                status[pos++] = '0' + minutes;
            } else {
                status[pos++] = '0' + (minutes / 10);
                status[pos++] = '0' + (minutes % 10);
            }
            status[pos++] = 'm';
            if (seconds < 10) {
                status[pos++] = '0';
                status[pos++] = '0' + seconds;
            } else {
                status[pos++] = '0' + (seconds / 10);
                status[pos++] = '0' + (seconds % 10);
            }
            status[pos++] = 's';
        }
        
        status[pos++] = ')';
    }
    
    status[pos] = '\0';
    
    if (status_label) {
        wg_set_text(status_label, status);
    }
    
    if (percent % 10 == 0 && percent > 0) {
        serial_puts("[INSTALLER] Progress: ");
        serial_puts_num(percent);
        serial_puts("% (");
        if (elapsed_ms > 0 && bytes_written > 0) {
            uint32_t speed_kb = (bytes_written / 1024) / (elapsed_ms / 1000 + 1);
            serial_puts_num(speed_kb);
            serial_puts(" KB/s");
            
            if (percent < 100) {
                uint32_t remaining_ms = (elapsed_ms * (100 - percent)) / percent;
                uint32_t remaining_sec = remaining_ms / 1000;
                serial_puts(", ETA: ");
                if (remaining_sec < 60) {
                    serial_puts_num(remaining_sec);
                    serial_puts("s");
                } else {
                    serial_puts_num(remaining_sec / 60);
                    serial_puts("m");
                    serial_puts_num(remaining_sec % 60);
                    serial_puts("s");
                }
            }
        }
        serial_puts(")\n");
    }
}

static int prepare_disk(disk_t* target) {
    serial_puts("[INSTALLER] Clearing MBR and boot area (first 2 MB)...\n");
    
    if (status_label) {
        wg_set_text(status_label, "Preparing disk (clearing boot area)...");
    }
    if (progress_bar) {
        wg_set_progressbar_value(progress_bar, 5);
    }
    if (main_window) {
        main_window->needs_redraw = 1;
        gui_render();
        if (vesa_is_double_buffer_enabled()) vesa_swap_buffers();
    }
    
    // Очищаем только первые 2 MB (MBR + загрузочная область)
    uint32_t clear_size = 2 * 1024 * 1024;
    uint8_t* zero_buffer = (uint8_t*)kmalloc_aligned(clear_size, 512);
    if (!zero_buffer) {
        serial_puts("[INSTALLER] WARNING: Failed to allocate buffer, skipping clear\n");
        return 0;
    }
    memset(zero_buffer, 0, clear_size);
    
    uint32_t sectors_to_clear = clear_size / target->sector_size;
    
    serial_puts("[INSTALLER] Clearing first ");
    serial_puts_num(sectors_to_clear);
    serial_puts(" sectors (");
    serial_puts_num(clear_size / 1024);
    serial_puts(" KB)...\n");
    
    int result = disk_write(target, 0, sectors_to_clear, zero_buffer);
    if (result < 0) {
        serial_puts("[INSTALLER] WARNING: Failed to clear boot area, continuing anyway\n");
    }
    
    kfree_aligned(zero_buffer);
    
    if (target->flush) {
        target->flush(target);
        for (int i = 0; i < 100; i++) yield();
    }
    
    serial_puts("[INSTALLER] Disk preparation complete\n");
    
    if (progress_bar) {
        wg_set_progressbar_value(progress_bar, 10);
    }
    if (main_window) {
        main_window->needs_redraw = 1;
        gui_render();
        if (vesa_is_double_buffer_enabled()) vesa_swap_buffers();
    }
    
    return 0;
}

static void perform_installation(void) {
    if (selected_disk >= disk_get_count()) {
        serial_puts("[INSTALLER] ERROR: Invalid disk selected\n");
        return;
    }
    
    disk_t* target = disk_get(selected_disk);
    if (!target) {
        serial_puts("[INSTALLER] ERROR: Disk not found\n");
        return;
    }
    
    if (target->type == DISK_TYPE_ATAPI) {
        serial_puts("[INSTALLER] ERROR: Cannot install to CD-ROM\n");
        return;
    }
    
    // Проверка размера диска
    uint64_t disk_size_bytes = target->sectors * target->sector_size;
    if (disk_size_bytes < system_img_size) {
        serial_puts("[INSTALLER] ERROR: Disk is too small!\n");
        serial_puts("[INSTALLER] Required: ");
        serial_puts_num(system_img_size / 1024 / 1024);
        serial_puts(" MB, Available: ");
        serial_puts_num((uint32_t)(disk_size_bytes / 1024 / 1024));
        serial_puts(" MB\n");
        
        if (main_window && status_label) {
            wg_set_text(status_label, "ERROR: Disk too small!");
        }
        return;
    }
    
    serial_puts("[INSTALLER] Disk size check passed: ");
    serial_puts_num((uint32_t)(disk_size_bytes / 1024 / 1024));
    serial_puts(" MB >= ");
    serial_puts_num(system_img_size / 1024 / 1024);
    serial_puts(" MB\n");
    
    // Только подготовка MBR/загрузочной области, без полной очистки диска
    if (prepare_disk(target) != 0) {
        serial_puts("[INSTALLER] WARNING: Disk preparation had issues, continuing...\n");
    }
    
    // Нотификация для ATA
    if (target->type == DISK_TYPE_ATA) {
        serial_puts("[INSTALLER] NOTE: ATA PIO mode - installation may be slower than AHCI\n");
        if (status_label) {
            wg_set_text(status_label, "Installing to ATA disk (may be slower)...");
        }
        notif_warning("ATA Disk", "ATA PIO mode is slower. Installation may take several minutes.");
    }

    serial_puts("\n=== INSTALLATION STARTED ===\n");
    serial_puts("[INSTALLER] Target disk: ");
    serial_puts(target->model);
    serial_puts("\n");
    serial_puts("[INSTALLER] Image size: ");
    serial_puts_num(system_img_size / 1024 / 1024);
    serial_puts(" MB\n");
    serial_puts("[INSTALLER] Writing image to disk...\n");
    
    uint8_t* img = (uint8_t*)system_img_start;
    uint32_t sectors = system_img_size / 512;

    uint32_t write_sectors_per_block = 4096;  // 2 MB за раз
    uint32_t total_write_blocks = (sectors + write_sectors_per_block - 1) / write_sectors_per_block;
    uint32_t bytes_written_total = 0;
    uint32_t write_start_time = timer_get_ticks();
    
    for (uint32_t block = 0; block < total_write_blocks; block++) {
        uint32_t start_sector = block * write_sectors_per_block;
        uint32_t sectors_to_write = write_sectors_per_block;
        
        if (start_sector + sectors_to_write > sectors) {
            sectors_to_write = sectors - start_sector;
        }
        if (sectors_to_write == 0) break;
        
        uint32_t bytes_to_write = sectors_to_write * 512;
        
        serial_puts("[INSTALLER] Writing block ");
        serial_puts_num(block + 1);
        serial_puts("/");
        serial_puts_num(total_write_blocks);
        serial_puts(" (");
        serial_puts_num(bytes_to_write / 1024);
        serial_puts(" KB)\n");
        
        int result = disk_write(target, start_sector, sectors_to_write, 
                                img + start_sector * 512);
        if (result < 0) {
            serial_puts("[INSTALLER] Warning: write returned error, continuing anyway...\n");
        }
        
        bytes_written_total += bytes_to_write;
        uint32_t elapsed_ms = (timer_get_ticks() - write_start_time) * 10;
        update_install_progress(block + 1, total_write_blocks, bytes_written_total, system_img_size, elapsed_ms);
        
        if (main_window) {
            main_window->needs_redraw = 1;
            gui_render();
            if (vesa_is_double_buffer_enabled()) vesa_swap_buffers();
        }

        if ((block + 1) % 8 == 0 || block == total_write_blocks - 1) {
            serial_puts("[INSTALLER] Flushing...\n");
            if (target->flush) {
                target->flush(target);
            }
            serial_puts("[INSTALLER] Flush done\n");
            for (int i = 0; i < 500; i++) yield();
        }
    }
    
    serial_puts("[INSTALLER] Final sync...\n");
    if (target->flush) {
        for (int i = 0; i < 5; i++) {
            target->flush(target);
            for (int j = 0; j < 100; j++) yield();
        }
    }
    
    serial_puts("[INSTALLER] Verifying MBR on target disk...\n");
    uint8_t verify_mbr[512] __attribute__((aligned(512)));
    if (disk_read(target, 0, 1, verify_mbr) == 0) {
        if (verify_mbr[510] != 0x55 || verify_mbr[511] != 0xAA) {
            serial_puts("[INSTALLER] MBR signature missing, fixing...\n");
            verify_mbr[510] = 0x55;
            verify_mbr[511] = 0xAA;
            if (disk_write(target, 0, 1, verify_mbr) == 0) {
                serial_puts("[INSTALLER] MBR signature fixed\n");
            }
        }
        
        if (disk_read(target, 0, 1, verify_mbr) == 0) {
            serial_puts("[INSTALLER] MBR signature bytes: 0x");
            serial_puts_num_hex(verify_mbr[510]);
            serial_puts(" 0x");
            serial_puts_num_hex(verify_mbr[511]);
            serial_puts("\n");
            
            if (verify_mbr[510] == 0x55 && verify_mbr[511] == 0xAA) {
                serial_puts("[INSTALLER] MBR verification PASSED\n");
            } else {
                serial_puts("[INSTALLER] MBR verification FAILED!\n");
            }
        }
    } else {
        serial_puts("[INSTALLER] Failed to read MBR for verification\n");
    }
    
    uint32_t elapsed_ms = (timer_get_ticks() - write_start_time) * 10;
    
    serial_puts("[INSTALLER] Installation complete!\n");
    serial_puts("[INSTALLER] Time elapsed: ");
    serial_puts_num(elapsed_ms / 1000);
    serial_puts(".");
    serial_puts_num((elapsed_ms % 1000) / 100);
    serial_puts(" seconds\n");
    
    if (bytes_written_total > 0 && elapsed_ms > 0) {
        uint32_t speed_kb_per_sec = (bytes_written_total / 1024) / ((elapsed_ms / 1000) + 1);
        serial_puts("[INSTALLER] Average speed: ");
        serial_puts_num(speed_kb_per_sec);
        serial_puts(" KB/s\n");
    }
    serial_puts("=== INSTALLATION FINISHED ===\n\n");
    
    if (progress_bar) {
        wg_set_progressbar_value(progress_bar, 100);
    }
    
    if (status_label) {
        wg_set_text(status_label, "Installation complete!");
    }
    
    if (main_window) {
        main_window->needs_redraw = 1;
        gui_render();
        if (vesa_is_double_buffer_enabled()) vesa_swap_buffers();
    }
    
    completion_message();
}

static void start_installation(Widget* button, void* userdata) {
    (void)button;
    (void)userdata;
    
    if (installing) return;
    if (selected_disk >= disk_get_count()) return;
    
    disk_t* target = disk_get(selected_disk);
    if (!target || target->type == DISK_TYPE_ATAPI) return;
    
    uint64_t disk_size_bytes = target->sectors * target->sector_size;
    if (disk_size_bytes < system_img_size) {
        serial_puts("[INSTALLER] ERROR: Disk too small!\n");
        if (status_label) {
            wg_set_text(status_label, "ERROR: Disk too small!");
        }
        notif_error("Installation Error", "Target disk is too small for system image");
        return;
    }
    
    installing = 1;
    current_stage = STAGE_INSTALLING;
    
    if (main_window->first_widget) {
        Widget* w = main_window->first_widget;
        while (w) {
            Widget* next = w->next;
            wg_destroy_widget(w);
            w = next;
        }
        main_window->first_widget = NULL;
        main_window->last_widget = NULL;
    }
    
    wg_create_label(main_window, "  Installing PozitronOS...", 0.28f, 0.20f);
    progress_bar = wg_create_progressbar(main_window, 0.1f, 0.35f, 0.8f, 0.06f, 0);
    status_label = wg_create_label(main_window, "Preparing...", 0.1f, 0.45f);
    
    main_window->needs_redraw = 1;
    gui_render();
    if (vesa_is_double_buffer_enabled()) vesa_swap_buffers();
    
    serial_puts("[INSTALLER] Starting installation to disk ");
    serial_puts_num(selected_disk);
    serial_puts("\n");
    
    perform_installation();
}

static void select_disk_callback(Widget* button, void* userdata) {
    if (installing) return;
    
    uint32_t disk_index = (uint32_t)(uintptr_t)userdata;
    selected_disk = disk_index;
    
    disk_t* d = disk_get(selected_disk);
    uint64_t disk_size_bytes = d->sectors * d->sector_size;
    uint32_t disk_size_mb = (uint32_t)(disk_size_bytes / 1024 / 1024);
    uint32_t required_mb = system_img_size / 1024 / 1024;
    
    serial_puts("[INSTALLER] Selected disk: ");
    serial_puts_num(selected_disk);
    serial_puts(" (");
    serial_puts_num(disk_size_mb);
    serial_puts(" MB)");
    if (disk_size_mb >= required_mb) {
        serial_puts(" - OK\n");
    } else {
        serial_puts(" - TOO SMALL!\n");
    }
    
    if (main_window && main_window->first_widget) {
        Widget* w = main_window->first_widget;
        while (w) {
            if (w->type == WIDGET_LABEL && w->text && w->text[0] == 'S' && w->text[1] == 'e') {
                char new_text[128];
                if (disk_size_mb >= required_mb) {
                    sprintf(new_text, "Selected: Disk %d (%d MB) - OK", selected_disk, disk_size_mb);
                } else {
                    sprintf(new_text, "Selected: Disk %d (%d MB) - TOO SMALL!", selected_disk, disk_size_mb);
                }
                wg_set_text(w, new_text);
                break;
            }
            w = w->next;
        }
    }
    
    if (main_window) {
        main_window->needs_redraw = 1;
    }
}

static void welcome_install_callback(Widget* button, void* userdata) {
    (void)button;
    (void)userdata;
    current_stage = STAGE_SELECT_DISK;
    
    if (main_window->first_widget) {
        Widget* w = main_window->first_widget;
        while (w) {
            Widget* next = w->next;
            wg_destroy_widget(w);
            w = next;
        }
        main_window->first_widget = NULL;
        main_window->last_widget = NULL;
    }
    
    wg_create_label(main_window, "Select target disk:", 0.32f, 0.10f);
    
    int disk_count_total = disk_get_count();
    int disk_index = 0;
    float button_y = 0.18f;
    float button_height = 0.08f;
    int first_hdd = -1;
    uint32_t required_mb = system_img_size / 1024 / 1024;
    
    for (int i = 0; i < disk_count_total; i++) {
        disk_t* d = disk_get(i);
        
        if (d->type == DISK_TYPE_ATAPI) {
            continue;
        }
        
        if (first_hdd == -1) {
            first_hdd = i;
            selected_disk = i;
        }
        
        uint64_t disk_size_bytes = d->sectors * d->sector_size;
        uint32_t disk_size_mb = (uint32_t)(disk_size_bytes / 1024 / 1024);
        uint32_t disk_size_gb = disk_size_mb / 1024;
        
        char button_text[256];
        int pos = 0;
        
        if (d->type == DISK_TYPE_ATA || d->type == DISK_TYPE_ATAPI) {
            button_text[pos++] = '[';
            button_text[pos++] = 'A';
            button_text[pos++] = 'T';
            button_text[pos++] = 'A';
            button_text[pos++] = ']';
            button_text[pos++] = ' ';
        } else if (d->type == DISK_TYPE_AHCI || d->type == DISK_TYPE_SATA) {
            button_text[pos++] = '[';
            button_text[pos++] = 'S';
            button_text[pos++] = 'A';
            button_text[pos++] = 'T';
            button_text[pos++] = 'A';
            button_text[pos++] = ']';
            button_text[pos++] = ' ';
        } else {
            button_text[pos++] = '[';
            button_text[pos++] = '?';
            button_text[pos++] = ']';
            button_text[pos++] = ' ';
        }
        
        button_text[pos++] = 'D';
        button_text[pos++] = 'i';
        button_text[pos++] = 's';
        button_text[pos++] = 'k';
        button_text[pos++] = ' ';
        button_text[pos++] = '0' + disk_index;
        button_text[pos++] = ':';
        button_text[pos++] = ' ';
        
        const char* model = d->model;
        while (*model && pos < 200) {
            button_text[pos++] = *model++;
        }
        
        button_text[pos++] = ' ';
        button_text[pos++] = '(';
        
        if (disk_size_gb >= 1) {
            if (disk_size_gb < 10) {
                button_text[pos++] = '0' + disk_size_gb;
            } else if (disk_size_gb < 100) {
                button_text[pos++] = '0' + (disk_size_gb / 10);
                button_text[pos++] = '0' + (disk_size_gb % 10);
            } else {
                button_text[pos++] = '0' + (disk_size_gb / 100);
                button_text[pos++] = '0' + ((disk_size_gb / 10) % 10);
                button_text[pos++] = '0' + (disk_size_gb % 10);
            }
            button_text[pos++] = ' ';
            button_text[pos++] = 'G';
            button_text[pos++] = 'B';
        } else {
            if (disk_size_mb < 10) {
                button_text[pos++] = '0' + disk_size_mb;
            } else if (disk_size_mb < 100) {
                button_text[pos++] = '0' + (disk_size_mb / 10);
                button_text[pos++] = '0' + (disk_size_mb % 10);
            } else {
                button_text[pos++] = '0' + (disk_size_mb / 100);
                button_text[pos++] = '0' + ((disk_size_mb / 10) % 10);
                button_text[pos++] = '0' + (disk_size_mb % 10);
            }
            button_text[pos++] = ' ';
            button_text[pos++] = 'M';
            button_text[pos++] = 'B';
        }
        
        if (disk_size_mb < required_mb) {
            button_text[pos++] = ' ';
            button_text[pos++] = '-';
            button_text[pos++] = ' ';
            button_text[pos++] = 'T';
            button_text[pos++] = 'O';
            button_text[pos++] = 'O';
            button_text[pos++] = ' ';
            button_text[pos++] = 'S';
            button_text[pos++] = 'M';
            button_text[pos++] = 'A';
            button_text[pos++] = 'L';
            button_text[pos++] = 'L';
            button_text[pos++] = '!';
        }
        
        button_text[pos++] = ')';
        button_text[pos] = '\0';
        
        wg_create_button(main_window, button_text, 0.1f, button_y, 0.8f, button_height,
                        select_disk_callback, (void*)(uintptr_t)i);
        
        disk_index++;
        button_y += button_height + 0.02f;
    }
    
    if (disk_index == 0) {
        wg_create_label(main_window, "No hard disks found!", 0.28f, 0.35f);
        wg_create_label(main_window, "Please check your hardware and try again.", 0.18f, 0.43f);
    }
    
    char selected_text[128];
    sprintf(selected_text, "Selected: Disk %d", selected_disk);
    wg_create_label(main_window, selected_text, 0.35f, button_y + 0.02f);
    
    wg_create_button(main_window, "Back", 0.05f, 0.88f, 0.2f, 0.07f,
                    welcome_install_callback, NULL);
    
    wg_create_button(main_window, "Install", 0.7f, 0.88f, 0.25f, 0.07f,
                    start_installation, NULL);
    
    main_window->needs_redraw = 1;
}

static void create_welcome_stage(Window* win) {
    wg_create_label(win, "      PozitronOS", 0.35f, 0.25f);
    wg_create_label(win, "   Setup", 0.42f, 0.32f);
    wg_create_label(win, "This wizard will install PozitronOS on your computer.  ", 0.18f, 0.45f);
    wg_create_label(win, "Make sure you have backed up your data before continuing. ", 0.14f, 0.52f);
    
    wg_create_button(win, "Install", 0.35f, 0.75f, 0.3f, 0.08f,
                    welcome_install_callback, NULL);
}

static void create_installer_window(void) {
    serial_puts("\n=== CREATING INSTALLER WINDOW ===\n");
    
    uint32_t screen_width = vesa_get_width();
    uint32_t screen_height = vesa_get_height();
    
    uint32_t win_width = 650;
    uint32_t win_height = 500;
    uint32_t win_x = (screen_width - win_width) / 2;
    uint32_t win_y = (screen_height - win_height) / 2;
    
    if (win_x + win_width > screen_width) win_x = 0;
    if (win_y + win_height > screen_height) win_y = 0;
    
    main_window = wm_create_window("PozitronOS Installer",
                                   win_x, win_y, win_width, win_height,
                                   WINDOW_HAS_TITLE);
    
    if (!main_window) {
        serial_puts("[INSTALLER] ERROR: Failed to create window\n");
        return;
    }
    
    main_window->closable = 0;
    main_window->movable = 0;
    
    serial_puts("[INSTALLER] Installer window created\n");
    serial_puts("[INSTALLER] System image: ");
    serial_puts_num(system_img_size / 1024 / 1024);
    serial_puts(" MB\n");
}

static void handle_keyboard_events(event_t* event) {
    if (!event) return;
    
    if (event->type == EVENT_KEY_PRESS) {
        switch (event->data1) {
            case 0x0F:
                if (event->data2 & 0x02) {
                    gui_focus_prev();
                } else {
                    gui_focus_next();
                }
                break;
                
            default:
                break;
        }
    }
}

void kernel_main(uint32_t magic, multiboot_info_t* mb_info) {
    if (magic == MULTIBOOT_BOOTLOADER_MAGIC) {
        serial_puts("[INSTALLER] Multiboot magic OK\n");
        
        if (mb_info->flags & (1 << 3)) {
            serial_puts("[INSTALLER] Module flag present\n");
            
            uint32_t* mods = (uint32_t*)(uintptr_t)mb_info->mods_addr;
            
            serial_puts("[INSTALLER] Modules count: ");
            serial_puts_num(mb_info->mods_count);
            serial_puts("\n");
            
            if (mb_info->mods_count >= 1) {
                system_img_start = mods[0];
                system_img_end = mods[1];
                system_img_size = system_img_end - system_img_start;
                
                serial_puts("[INSTALLER] Module 0: start=0x");
                serial_puts_num_hex(system_img_start);
                serial_puts(" end=0x");
                serial_puts_num_hex(system_img_end);
                serial_puts(" size=");
                serial_puts_num(system_img_size / 1024);
                serial_puts(" KB\n");
            } else {
                serial_puts("[INSTALLER] ERROR: No modules loaded!\n");
            }
        } else {
            serial_puts("[INSTALLER] ERROR: No module flag in multiboot!\n");
        }
    } else {
        serial_puts("[INSTALLER] ERROR: Invalid multiboot magic!\n");
    }
    
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
    
    memory_init();
    memory_dump();
    vga_puts("[ OK ] MEMORY ALLOCATION SYSTEM OK\n");
    debug_heap_layout();
    
    if(!vesa_init(mb_info)) {
        vga_puts("[ERROR] VBE/VESA INITIALISATION FAILED\n");
    } else {
        vga_puts("[ OK ] VBE/VESA OK\n");
    }
    vesa_enable_double_buffer();

    vesa_fill(0x000000);
    if (vesa_is_double_buffer_enabled()) {
        vesa_swap_buffers();
    }
    
    for (int i = 0; i < 500000; i++) asm volatile("nop");

    scanner_init();
    vga_puts("[INFO] SCANNING HARDWARE START\n");
    scanner_scan_all();
    scanner_dump_all();
    vga_puts("[ OK ] SCANNING HARDWARE FINISH\n");

    paging_init();

    if (system_img_size > 0) {
        uint32_t img_start_page = system_img_start & ~0xFFF;
        uint32_t img_end_page = (system_img_end + 0xFFF) & ~0xFFF;
        
        serial_puts("[PAGING] Mapping system image region: 0x");
        serial_puts_num_hex(img_start_page);
        serial_puts(" - 0x");
        serial_puts_num_hex(img_end_page);
        serial_puts("\n");
        
        for (uint32_t addr = img_start_page; addr < img_end_page; addr += PAGE_SIZE) {
            paging_map_page(current_directory, addr, addr, PAGE_PRESENT | PAGE_WRITABLE);
        }
        
        uint8_t test = *(uint8_t*)system_img_start;
        serial_puts("[PAGING] Module verification: 0x");
        serial_puts_num_hex(test);
        serial_puts("\n");
    }

    cmos_init();
    vga_puts("[ OK ] CMOS RTC OK\n");

    device_init();
    notif_init();

    disk_init();

    uint32_t screen_width = vesa_get_width();
    uint32_t screen_height = vesa_get_height();

    vesa_cache_background();
    vesa_init_dirty();
    vesa_mark_dirty_all();
    vesa_cursor_init();
    vesa_cursor_set_visible(1);

    event_init();
    vga_puts("[ OK ] EVENT SYSTEM OK\n");

    keyboard_init();
    mouse_init();

    if (vesa_is_background_cached()) {
        vesa_restore_background();
    }

    vga_puts("[INFO] STARTUP GUI ENVIRONMENT\n");
    gui_init(screen_width, screen_height);

    taskbar_init();
    taskbar_disabled = 1;

    vga_puts("[ OK ] GUI ENVIRONMENT OK\n");

    if (system_img_size == 0) {
        serial_puts("[INSTALLER] ERROR: No system image loaded!\n");
        vga_puts("[ERROR] No system image found!\n");
        
        uint32_t win_width = 400;
        uint32_t win_height = 150;
        uint32_t win_x = (screen_width - win_width) / 2;
        uint32_t win_y = (screen_height - win_height) / 2;
        
        Window* error_win = wm_create_window("Error", win_x, win_y, win_width, win_height,
                                            WINDOW_HAS_TITLE);
        if (error_win) {
            error_win->closable = 0;
            error_win->movable = 0;
            wg_create_label(error_win, "No system image found!", 0.1f, 0.35f);
            wg_create_label(error_win, "Please check your installation media.", 0.1f, 0.55f);
        }
    } else {
        create_installer_window();
        create_welcome_stage(main_window);
    }

    serial_puts("\n=== INSTALLER READY ===\n");
    vga_puts("[INFO] INSTALLER READY\n");

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

        vesa_hide_cursor();

        mouse_update();

        if (is_shutdown_mode_active()) {
            update_shutdown_animation();
        }

        if (vesa_is_background_cached()) {
            vesa_restore_background_dirty();
        }

        gui_render();

        notif_update();
        notif_render();

        vesa_show_cursor();
        vesa_cursor_update();

        if (vesa_is_double_buffer_enabled()) {
            vesa_swap_buffers();
        }
    }
}