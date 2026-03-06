#define GPIO_MODE_OUTPUT    1
#define GPIO_MODE_INPUT     2
#define GPIO_MODE_ALT_PP    3

void gpio_set_mode(uint32_t pin, uint32_t mode);
void gpio_write(uint32_t pin, uint32_t value);
uint32_t gpio_read(uint32_t pin);