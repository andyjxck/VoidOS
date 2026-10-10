#ifndef _HAL_HAL_GPIO_H
#define _HAL_HAL_GPIO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HAL_GPIO_PULL_NONE = 0,
    HAL_GPIO_PULL_UP = 1,
    HAL_GPIO_PULL_DOWN = 2,
} hal_gpio_pull_t;

int hal_gpio_init_out(int pin, int val);
int hal_gpio_write(int pin, int val);
int hal_gpio_read(int pin);

#ifdef __cplusplus
}
#endif

#endif
