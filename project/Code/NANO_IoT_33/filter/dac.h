#ifndef DAC_H
#define DAC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void dac_init(void);
void dac_write(uint16_t value);

#ifdef __cplusplus
}
#endif

#endif
