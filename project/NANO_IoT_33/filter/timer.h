#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void timer_init(void);
void timer_enable(void);
void timer_disable(void);
bool timer_is_enabled(void);
void timer_set_period_us(uint32_t period_us);
uint32_t timer_get_period_us(void);

/* Returns true and consumes one pending timer event. */
bool timer_take_event(void);

#ifdef __cplusplus
}
#endif

#endif // TIMER_H