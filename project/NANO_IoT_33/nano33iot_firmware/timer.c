#include "timer.h"
#include "config.h"
#include <sam.h>

#define TIMER_CLOCK_HZ (48000000UL)

typedef struct {
    uint16_t divisor;
    uint8_t ctrlb_prescaler;
} timer_prescaler_t;

static const timer_prescaler_t timer_prescalers[] = {
    { 1U,    TC_CTRLA_PRESCALER_DIV1 },
    { 2U,    TC_CTRLA_PRESCALER_DIV2 },
    { 4U,    TC_CTRLA_PRESCALER_DIV4 },
    { 8U,    TC_CTRLA_PRESCALER_DIV8 },
    { 16U,   TC_CTRLA_PRESCALER_DIV16 },
    { 64U,   TC_CTRLA_PRESCALER_DIV64 },
    { 256U,  TC_CTRLA_PRESCALER_DIV256 },
    { 1024U, TC_CTRLA_PRESCALER_DIV1024 }
};

static volatile uint32_t timer_period_us = TIMER_DEFAULT_PERIOD_US;
static volatile uint32_t timer_event_count = 0U;
static volatile bool timer_enabled = false;

static uint32_t timer_irq_save(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}

static void timer_irq_restore(uint32_t primask)
{
    if (primask == 0U) {
        __enable_irq();
    }
}

static bool timer_calculate_period(uint32_t period_us,
                                   uint8_t *prescaler,
                                   uint16_t *compare_value)
{
    uint32_t i;

    for (i = 0U; i < (sizeof(timer_prescalers) / sizeof(timer_prescalers[0])); ++i) {
        uint64_t ticks;
        uint64_t numerator = ((uint64_t)TIMER_CLOCK_HZ * (uint64_t)period_us);
        uint32_t divisor = timer_prescalers[i].divisor;

        ticks = (numerator + ((uint64_t)divisor * 1000000ULL) / 2ULL) /
                ((uint64_t)divisor * 1000000ULL);

        if (ticks >= 1ULL && ticks <= 65536ULL) {
            *prescaler = timer_prescalers[i].ctrlb_prescaler;
            *compare_value = (uint16_t)(ticks - 1ULL);
            return true;
        }
    }

    return false;
}

static bool timer_configure(uint32_t period_us)
{
    uint8_t prescaler;
    uint16_t compare_value;

    if (!timer_calculate_period(period_us, &prescaler, &compare_value)) {
        return false;
    }

    PM->APBCMASK.reg |= PM_APBCMASK_TC3;

    GCLK->CLKCTRL.reg = GCLK_CLKCTRL_ID(TC3_GCLK_ID) |
                        GCLK_CLKCTRL_GEN_GCLK0 |
                        GCLK_CLKCTRL_CLKEN;
    while (GCLK->STATUS.bit.SYNCBUSY != 0U) {
    }

    TC3->COUNT16.CTRLA.reg = TC_CTRLA_SWRST;
    while (TC3->COUNT16.STATUS.bit.SYNCBUSY != 0U ||
           TC3->COUNT16.CTRLA.bit.SWRST != 0U) {
    }

    TC3->COUNT16.CTRLA.reg = TC_CTRLA_MODE_COUNT16 |
                             prescaler |
                             TC_CTRLA_WAVEGEN_MFRQ;
    while (TC3->COUNT16.STATUS.bit.SYNCBUSY != 0U) {
    }

    TC3->COUNT16.CC[0].reg = compare_value;
    while (TC3->COUNT16.STATUS.bit.SYNCBUSY != 0U) {
    }

    TC3->COUNT16.COUNT.reg = 0U;
    while (TC3->COUNT16.STATUS.bit.SYNCBUSY != 0U) {
    }

    TC3->COUNT16.INTFLAG.reg = TC_INTFLAG_MC0;
    TC3->COUNT16.INTENCLR.reg = TC_INTENCLR_MC0;

    NVIC_DisableIRQ(TC3_IRQn);
    NVIC_ClearPendingIRQ(TC3_IRQn);
    NVIC_SetPriority(TC3_IRQn, 1U);

    return true;
}

void timer_init(void)
{
    uint32_t primask = timer_irq_save();

    timer_enabled = false;
    timer_event_count = 0U;
    timer_configure(TIMER_DEFAULT_PERIOD_US);

    timer_irq_restore(primask);
}

void timer_enable(void)
{
    uint32_t primask = timer_irq_save();

    TC3->COUNT16.INTFLAG.reg = TC_INTFLAG_MC0;
    TC3->COUNT16.INTENSET.reg = TC_INTENSET_MC0;
    NVIC_ClearPendingIRQ(TC3_IRQn);
    NVIC_EnableIRQ(TC3_IRQn);
    TC3->COUNT16.CTRLA.bit.ENABLE = 1U;
    while (TC3->COUNT16.STATUS.bit.SYNCBUSY != 0U) {
    }

    timer_enabled = true;
    timer_irq_restore(primask);
}

void timer_disable(void)
{
    uint32_t primask = timer_irq_save();

    TC3->COUNT16.CTRLA.bit.ENABLE = 0U;
    while (TC3->COUNT16.STATUS.bit.SYNCBUSY != 0U) {
    }

    TC3->COUNT16.INTENCLR.reg = TC_INTENCLR_MC0;
    NVIC_DisableIRQ(TC3_IRQn);
    NVIC_ClearPendingIRQ(TC3_IRQn);
    timer_enabled = false;

    timer_irq_restore(primask);
}

bool timer_is_enabled(void)
{
    return timer_enabled;
}

void timer_set_period_us(uint32_t period_us)
{
    uint32_t primask;
    bool was_enabled;

    if (period_us < TIMER_MIN_PERIOD_US || period_us > TIMER_MAX_PERIOD_US) {
        return;
    }

    primask = timer_irq_save();
    was_enabled = timer_enabled;

    TC3->COUNT16.CTRLA.bit.ENABLE = 0U;
    while (TC3->COUNT16.STATUS.bit.SYNCBUSY != 0U) {
    }
    TC3->COUNT16.INTENCLR.reg = TC_INTENCLR_MC0;
    NVIC_DisableIRQ(TC3_IRQn);
    NVIC_ClearPendingIRQ(TC3_IRQn);

    if (timer_configure(period_us)) {
        timer_period_us = period_us;
    }

    if (was_enabled) {
        TC3->COUNT16.INTFLAG.reg = TC_INTFLAG_MC0;
        TC3->COUNT16.INTENSET.reg = TC_INTENSET_MC0;
        NVIC_EnableIRQ(TC3_IRQn);
        TC3->COUNT16.CTRLA.bit.ENABLE = 1U;
        while (TC3->COUNT16.STATUS.bit.SYNCBUSY != 0U) {
        }
    }

    timer_irq_restore(primask);
}

uint32_t timer_get_period_us(void)
{
    return timer_period_us;
}

bool timer_take_event(void)
{
    bool result = false;
    uint32_t primask = timer_irq_save();

    if (timer_event_count != 0U) {
        --timer_event_count;
        result = true;
    }

    timer_irq_restore(primask);
    return result;
}

void TC3_Handler(void)
{
    if (TC3->COUNT16.INTFLAG.bit.MC0 != 0U) {
        TC3->COUNT16.INTFLAG.reg = TC_INTFLAG_MC0;

        if (timer_event_count != UINT32_MAX) {
            ++timer_event_count;
        }
    }
}
