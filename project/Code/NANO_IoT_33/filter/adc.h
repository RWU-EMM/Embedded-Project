#ifndef ADC_H
#define ADC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

  void adc_init(void);
  uint16_t adc_read(void);

#ifdef __cplusplus
}
#endif

#endif
