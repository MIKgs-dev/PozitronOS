#include "kernel/auth.h"
#include "kernel/memory.h"
#include "drivers/serial.h"
#include "drivers/timer.h"
#include "fs/vfs.h"
#include "lib/string.h"
#include "lib/mini_printf.h"

#define AUTH_MAX_LINE 512
#define AUTH_MAX_FAIL_COUNT 5

// ============ ХЕШИРОВАНИЕ (djb2) ============
static uint32_t hash_djb2(const char* str) {
    uint32_t hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash;
}

const char* auth_hash_password(const char* password) {
    static char hash_buf[9];
    uint32_t hash = hash_djb2(password);
    const char hex[] = "0123456789ABCDEF";
    for (int i = 0; i < 8; i++) {
        hash_buf[7 - i] = hex[hash & 0xF];
        hash >>= 4;
    }
    hash_buf[8] = '\0';
    return hash_buf;
}

int auth_check_hash(const char* password, const char* hash) {
    const char* computed = auth_hash_password(password);
    return strcmp(computed, hash) == 0;
}

// ============ ПАРСИНГ СТРОКИ /etc/passwd ============
// Формат: username:password_hash:uid:gid:fullname:hint:home:shell[:flags]
static int parse_passwd_line(char* line, auth_user_t* user) {
    if (!line || !user) return -1;
    
    char* token = line;
    char* end;
    
    memset(user, 0, sizeof(auth_user_t));
    
    // username
    end = strchr(token, ':');
    if (!end) return -1;
    *end = '\0';
    strncpy(user->username, token, AUTH_MAX_USERNAME - 1);
    token = end + 1;
    
    // password_hash
    end = strchr(token, ':');
    if (!end) return -1;
    *end = '\0';
    strncpy(user->password_hash, token, 31);
    token = end + 1;
    
    // uid
    end = strchr(token, ':');
    if (!end) return -1;
    *end = '\0';
    user->uid = atoi(token);
    token = end + 1;
    
    // gid
    end = strchr(token, ':');
    if (!end) return -1;
    *end = '\0';
    user->gid = atoi(token);
    token = end + 1;
    
    // fullname
    end = strchr(token, ':');
    if (!end) return -1;
    *end = '\0';
    strncpy(user->fullname, token, AUTH_MAX_FULLNAME - 1);
    token = end + 1;
    
    // hint
    end = strchr(token, ':');
    if (!end) return -1;
    *end = '\0';
    strncpy(user->hint, token, AUTH_MAX_HINT - 1);
    token = end + 1;
    
    // home
    end = strchr(token, ':');
    if (!end) return -1;
    *end = '\0';
    strncpy(user->home, token, AUTH_MAX_HOME - 1);
    token = end + 1;
    
    // shell и опциональные флаги
    char* flags = strchr(token, ':');
    if (flags) {
        *flags = '\0';
        strncpy(user->shell, token, AUTH_MAX_SHELL - 1);
        token = flags + 1;
        user->disabled = (strchr(token, 'D') != NULL);
        user->locked = (strchr(token, 'L') != NULL);
    } else {
        strncpy(user->shell, token, AUTH_MAX_SHELL - 1);
    }
    
    return 0;
}

// Формирование строки для /etc/passwd
static void format_passwd_line(auth_user_t* user, char* out, int out_size) {
    (void)out_size;
    sprintf(out, "%s:%s:%d:%d:%s:%s:%s:%s%s%s\n",
            user->username, user->password_hash, user->uid, user->gid,
            user->fullname, user->hint, user->home, user->shell,
            user->disabled ? "D" : "", user->locked ? "L" : "");
}

// ============ ЧТЕНИЕ ВСЕГО ФАЙЛА СРАЗУ ============
static char* read_entire_file(const char* path, uint32_t* size) {
    struct vfs_file* file;
    if (vfs_open(path, FS_O_RDONLY, &file) != 0) {
        return NULL;
    }
    
    // Узнаём размер файла
    vfs_lseek(file, 0, 2);  // SEEK_END
    uint32_t file_size = file->f_pos;
    vfs_lseek(file, 0, 0);  // SEEK_SET
    
    if (file_size == 0 || file_size > 65536) {  // Максимум 64KB
        vfs_close(file);
        return NULL;
    }
    
    char* content = (char*)kmalloc(file_size + 1);
    if (!content) {
        vfs_close(file);
        return NULL;
    }
    
    uint32_t bytes_read;
    if (vfs_read(file, content, file_size, &bytes_read) != 0 || bytes_read != file_size) {
        kfree(content);
        vfs_close(file);
        return NULL;
    }
    content[file_size] = '\0';
    vfs_close(file);
    
    if (size) *size = file_size;
    return content;
}

// ============ ЗАПИСЬ ВСЕГО ФАЙЛА (перезапись) ============
static int write_entire_file(const char* path, const char* content, uint32_t size) {
    struct vfs_file* file;
    if (vfs_open(path, FS_O_WRONLY | FS_O_CREAT | FS_O_TRUNC, &file) != 0) {
        return -1;
    }
    
    uint32_t written;
    int ret = vfs_write(file, content, size, &written);
    vfs_close(file);
    
    return (ret == 0 && written == size) ? 0 : -1;
}

// ============ ПОИСК ПОЛЬЗОВАТЕЛЯ В ФАЙЛЕ ============
static int find_user_in_passwd(const char* username, auth_user_t* out_user) {
    if (!username || !out_user) return -1;
    
    uint32_t file_size;
    char* content = read_entire_file(AUTH_PASSWD_PATH, &file_size);
    if (!content) return -1;
    
    char* line = strtok(content, "\n");
    int found = 0;
    
    while (line) {
        char* colon = strchr(line, ':');
        if (colon) {
            *colon = '\0';
            if (strcmp(line, username) == 0) {
                *colon = ':';
                if (parse_passwd_line(line, out_user) == 0) {
                    found = 1;
                }
                break;
            }
        }
        line = strtok(NULL, "\n");
    }
    
    kfree(content);
    return found ? 0 : -1;
}

// ============ ОБНОВЛЕНИЕ ПОЛЬЗОВАТЕЛЯ В ФАЙЛЕ ============
static int update_user_in_passwd(auth_user_t* user) {
    if (!user) return -1;
    
    uint32_t file_size;
    char* content = read_entire_file(AUTH_PASSWD_PATH, &file_size);
    if (!content) return -1;
    
    // Строим новый контент
    char* new_content = NULL;
    uint32_t new_size = 0;
    
    char* line = strtok(content, "\n");
    int found = 0;
    
    while (line) {
        char* colon = strchr(line, ':');
        if (colon) {
            *colon = '\0';
            if (strcmp(line, user->username) == 0) {
                // Заменяем строку пользователя
                char new_line[AUTH_MAX_LINE];
                format_passwd_line(user, new_line, AUTH_MAX_LINE);
                uint32_t len = strlen(new_line);
                char* tmp = (char*)krealloc(new_content, new_size + len + 1);
                if (!tmp) {
                    kfree(content);
                    return -1;
                }
                new_content = tmp;
                memcpy(new_content + new_size, new_line, len);
                new_size += len;
                found = 1;
            } else {
                // Оставляем как есть
                *colon = ':';
                uint32_t len = strlen(line) + 1;
                char* tmp = (char*)krealloc(new_content, new_size + len + 1);
                if (!tmp) {
                    kfree(content);
                    return -1;
                }
                new_content = tmp;
                memcpy(new_content + new_size, line, len);
                new_content[new_size + len - 1] = '\n';
                new_size += len;
            }
        }
        line = strtok(NULL, "\n");
    }
    
    // Если пользователь не найден - добавляем в конец
    if (!found) {
        char new_line[AUTH_MAX_LINE];
        format_passwd_line(user, new_line, AUTH_MAX_LINE);
        uint32_t len = strlen(new_line);
        char* tmp = (char*)krealloc(new_content, new_size + len + 1);
        if (tmp) {
            new_content = tmp;
            memcpy(new_content + new_size, new_line, len);
            new_size += len;
        }
    }
    
    kfree(content);
    
    if (!new_content) return -1;
    
    int ret = write_entire_file(AUTH_PASSWD_PATH, new_content, new_size);
    kfree(new_content);
    return ret;
}

// ============ ПУБЛИЧНЫЕ ФУНКЦИИ ============

int auth_init(void) {
    serial_puts("[AUTH] Initializing authentication system\n");
    
    // Убеждаемся, что файл существует
    struct vfs_file* file;
    if (vfs_open(AUTH_PASSWD_PATH, FS_O_RDONLY, &file) != 0) {
        serial_puts("[AUTH] /etc/passwd not found, creating empty\n");
        vfs_open(AUTH_PASSWD_PATH, FS_O_WRONLY | FS_O_CREAT, &file);
        if (file) vfs_close(file);
    } else {
        vfs_close(file);
    }

    uint32_t size;
    char* content = read_entire_file(AUTH_PASSWD_PATH, &size);
    if (content) {
        serial_puts("[AUTH] /etc/passwd content:\n");
        serial_puts(content);
        serial_puts("\n");
        kfree(content);
    }
    
    return 0;
}

auth_user_t* auth_get_user(const char* username) {
    if (!username) return NULL;
    
    auth_user_t* user = (auth_user_t*)kmalloc(sizeof(auth_user_t));
    if (!user) return NULL;
    
    if (find_user_in_passwd(username, user) == 0) {
        return user;
    }
    
    kfree(user);
    return NULL;
}

void auth_free_user(auth_user_t* user) {
    if (user) kfree(user);
}

auth_status_t auth_verify(const char* username, const char* password) {
    if (!username || !password) return AUTH_ERR_INTERNAL;
    
    auth_user_t* user = auth_get_user(username);
    if (!user) return AUTH_ERR_USER_NOT_FOUND;
    
    if (user->disabled) {
        auth_free_user(user);
        return AUTH_ERR_DISABLED;
    }
    
    if (user->locked) {
        auth_free_user(user);
        return AUTH_ERR_LOCKED;
    }
    
    int ok = auth_check_hash(password, user->password_hash);
    auth_free_user(user);
    
    return ok ? AUTH_OK : AUTH_ERR_WRONG_PASSWORD;
}

auth_status_t auth_verify_with_lockout(const char* username, const char* password) {
    if (!username || !password) return AUTH_ERR_INTERNAL;
    
    auth_user_t* user = auth_get_user(username);
    if (!user) return AUTH_ERR_USER_NOT_FOUND;
    
    if (user->disabled) {
        auth_free_user(user);
        return AUTH_ERR_DISABLED;
    }
    
    if (user->locked) {
        auth_free_user(user);
        return AUTH_ERR_LOCKED;
    }
    
    if (auth_check_hash(password, user->password_hash)) {
        // Успех - сбрасываем счётчик и обновляем файл
        user->fail_count = 0;
        user->last_login = timer_get_ticks();
        update_user_in_passwd(user);
        auth_free_user(user);
        return AUTH_OK;
    }
    
    // Неудача - увеличиваем счётчик
    user->fail_count++;
    
    if (user->fail_count >= AUTH_MAX_FAIL_COUNT) {
        user->locked = 1;
        serial_puts("[AUTH] User ");
        serial_puts(username);
        serial_puts(" has been locked\n");
    }
    
    // Сохраняем изменения (fail_count или locked)
    update_user_in_passwd(user);
    auth_free_user(user);
    return AUTH_ERR_WRONG_PASSWORD;
}

auth_status_t auth_change_password(const char* username, const char* old_password, const char* new_password) {
    if (!username || !old_password || !new_password) return AUTH_ERR_INTERNAL;
    if (strlen(new_password) == 0) return AUTH_ERR_INTERNAL;
    
    auth_status_t status = auth_verify(username, old_password);
    if (status != AUTH_OK) return status;
    
    return auth_force_change_password(username, new_password);
}

auth_status_t auth_force_change_password(const char* username, const char* new_password) {
    if (!username || !new_password) return AUTH_ERR_INTERNAL;
    
    auth_user_t* user = auth_get_user(username);
    if (!user) return AUTH_ERR_USER_NOT_FOUND;
    
    const char* new_hash = auth_hash_password(new_password);
    strncpy(user->password_hash, new_hash, 31);
    
    int ret = update_user_in_passwd(user);
    auth_free_user(user);
    
    return ret == 0 ? AUTH_OK : AUTH_ERR_INTERNAL;
}

int auth_user_exists(const char* username) {
    auth_user_t* user = auth_get_user(username);
    if (user) {
        auth_free_user(user);
        return 1;
    }
    return 0;
}

char* auth_get_home(const char* username) {
    auth_user_t* user = auth_get_user(username);
    if (!user) return NULL;
    
    char* home = (char*)kmalloc(strlen(user->home) + 1);
    if (home) strcpy(home, user->home);
    auth_free_user(user);
    return home;
}

char* auth_get_shell(const char* username) {
    auth_user_t* user = auth_get_user(username);
    if (!user) return NULL;
    
    char* shell = (char*)kmalloc(strlen(user->shell) + 1);
    if (shell) strcpy(shell, user->shell);
    auth_free_user(user);
    return shell;
}

char* auth_get_hint(const char* username) {
    auth_user_t* user = auth_get_user(username);
    if (!user) return NULL;
    
    char* hint = (char*)kmalloc(strlen(user->hint) + 1);
    if (hint) strcpy(hint, user->hint);
    auth_free_user(user);
    return hint;
}

int auth_is_disabled(const char* username) {
    auth_user_t* user = auth_get_user(username);
    if (!user) return 0;
    int result = user->disabled;
    auth_free_user(user);
    return result;
}

int auth_is_locked(const char* username) {
    auth_user_t* user = auth_get_user(username);
    if (!user) return 0;
    int result = user->locked;
    auth_free_user(user);
    return result;
}

auth_status_t auth_disable_user(const char* username, int disable) {
    auth_user_t* user = auth_get_user(username);
    if (!user) return AUTH_ERR_USER_NOT_FOUND;
    
    user->disabled = disable ? 1 : 0;
    int ret = update_user_in_passwd(user);
    auth_free_user(user);
    
    return ret == 0 ? AUTH_OK : AUTH_ERR_INTERNAL;
}

auth_status_t auth_lock_user(const char* username) {
    auth_user_t* user = auth_get_user(username);
    if (!user) return AUTH_ERR_USER_NOT_FOUND;
    
    user->locked = 1;
    int ret = update_user_in_passwd(user);
    auth_free_user(user);
    
    return ret == 0 ? AUTH_OK : AUTH_ERR_INTERNAL;
}

auth_status_t auth_unlock_user(const char* username) {
    auth_user_t* user = auth_get_user(username);
    if (!user) return AUTH_ERR_USER_NOT_FOUND;
    
    user->locked = 0;
    user->fail_count = 0;
    int ret = update_user_in_passwd(user);
    auth_free_user(user);
    
    return ret == 0 ? AUTH_OK : AUTH_ERR_INTERNAL;
}

void auth_reset_fail_count(const char* username) {
    auth_user_t* user = auth_get_user(username);
    if (!user) return;
    
    user->fail_count = 0;
    update_user_in_passwd(user);
    auth_free_user(user);
}

int auth_create_home(const char* username) {
    char user_path[256];
    sprintf(user_path, "/users/%s", username);
    
    if (vfs_mkdir_p(user_path, 0755) != 0) {
        serial_puts("[AUTH] Failed to create home directory\n");
        return -1;
    }
    
    if (vfs_copy_template("/users/Default", user_path) != 0) {
        serial_puts("[AUTH] Failed to copy user template\n");
        return -1;
    }
    
    return 0;
}

char** auth_get_all_users(int* count) {
    if (!count) return NULL;
    
    uint32_t file_size;
    char* content = read_entire_file(AUTH_PASSWD_PATH, &file_size);
    if (!content) {
        *count = 0;
        return NULL;
    }
    
    char** users = NULL;
    int user_count = 0;
    char* line = strtok(content, "\n");
    
    while (line) {
        char* colon = strchr(line, ':');
        if (colon) {
            *colon = '\0';
            if (strcmp(line, "Default") != 0) {
                char** new_users = (char**)krealloc(users, (user_count + 1) * sizeof(char*));
                if (new_users) {
                    users = new_users;
                    users[user_count] = (char*)kmalloc(strlen(line) + 1);
                    if (users[user_count]) {
                        strcpy(users[user_count], line);
                        user_count++;
                    }
                }
            }
        }
        line = strtok(NULL, "\n");
    }
    
    kfree(content);
    *count = user_count;
    return users;
}