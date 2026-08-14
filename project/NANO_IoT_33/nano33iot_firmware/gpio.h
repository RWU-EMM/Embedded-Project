#ifndef GPIO_H
#define GPIO_H

#include <stdbool.h>

void gpio_init(void);
bool gpio_get_cmd(void);
void gpio_set_rdy(bool state);

#endif
