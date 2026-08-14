#include "gpio.h"
#include "config.h"
#include <Arduino.h>

void gpio_init(void)
{
    pinMode(CMD_GPIO_PIN, INPUT);
    pinMode(RDY_GPIO_PIN, OUTPUT);
    digitalWrite(RDY_GPIO_PIN, LOW);
}

bool gpio_get_cmd(void)
{
    return (digitalRead(CMD_GPIO_PIN) != LOW);
}

void gpio_set_rdy(bool state)
{
    digitalWrite(RDY_GPIO_PIN, state ? HIGH : LOW);
}
