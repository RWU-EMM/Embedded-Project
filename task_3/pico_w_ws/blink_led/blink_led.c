#include "pico/stdlib.h"
#include <stdbool.h>

#ifdef CYW43_WL_GPIO_LED_PIN
#include "pico/cyw43_arch.h"
#endif

#define LED_DELAY_MS 1000

int pico_led_init(void) {
#ifdef CYW43_WL_GPIO_LED_PIN
    return cyw43_arch_init();
#else
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
    return PICO_OK;
#endif
}

void pico_set_led(bool led_on) {
#ifdef CYW43_WL_GPIO_LED_PIN
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led_on);
#else
    gpio_put(PICO_DEFAULT_LED_PIN, led_on);
#endif
}

int main() {
    int rc = pico_led_init();
    hard_assert(rc == PICO_OK);

    while (true) {
        pico_set_led(true);
        sleep_ms(LED_DELAY_MS);
        pico_set_led(false);
        sleep_ms(LED_DELAY_MS);
    }
}