#ifndef KERNEL_AUTH_H
#define KERNEL_AUTH_H

#include <stdint.h>

#define AUTH_MAX_USERNAME 32
#define AUTH_MAX_PASSWORD 128
#define AUTH_MAX_HOME 256
#define AUTH_MAX_SHELL 64
#define AUTH_MAX_HINT 256
#define AUTH_MAX_FULLNAME 128
#define AUTH_PASSWD_PATH "/etc/passwd"

typedef enum {
    AUTH_OK = 0,
    AUTH_ERR_USER_NOT_FOUND = -1,
    AUTH_ERR_WRONG_PASSWORD = -2,
    AUTH_ERR_DISABLED = -3,
    AUTH_ERR_LOCKED = -4,
    AUTH_ERR_INTERNAL = -5
} auth_status_t;

typedef struct {
    char username[AUTH_MAX_USERNAME];
    char password_hash[32];      // hex-строка хеша (8 символов)
    uint32_t uid;
    uint32_t gid;
    char fullname[AUTH_MAX_FULLNAME];
    char hint[AUTH_MAX_HINT];
    char home[AUTH_MAX_HOME];
    char shell[AUTH_MAX_SHELL];
    uint8_t disabled;            // 1 - учётная запись отключена
    uint8_t locked;              // 1 - заблокирована (после неудачных попыток)
    uint32_t fail_count;         // количество неудачных попыток входа
    uint32_t last_login;         // timestamp последнего успешного входа
} auth_user_t;

int auth_init(void);
auth_user_t* auth_get_user(const char* username);
void auth_free_user(auth_user_t* user);
auth_status_t auth_verify(const char* username, const char* password);
auth_status_t auth_verify_with_lockout(const char* username, const char* password);
auth_status_t auth_change_password(const char* username, const char* old_password, const char* new_password);
auth_status_t auth_force_change_password(const char* username, const char* new_password);
auth_status_t auth_create_user(const char* username, const char* password, 
                                const char* fullname, const char* hint,
                                const char* home, const char* shell);
auth_status_t auth_delete_user(const char* username, int delete_home);
auth_status_t auth_disable_user(const char* username, int disable);
int auth_is_disabled(const char* username);
auth_status_t auth_lock_user(const char* username);
auth_status_t auth_unlock_user(const char* username);
int auth_is_locked(const char* username);
void auth_reset_fail_count(const char* username);
char* auth_get_home(const char* username);
char* auth_get_shell(const char* username);
char* auth_get_hint(const char* username);
int auth_user_exists(const char* username);
char** auth_get_all_users(int* count);
int auth_create_home(const char* username);
const char* auth_hash_password(const char* password);
int auth_check_hash(const char* password, const char* hash);

#endif