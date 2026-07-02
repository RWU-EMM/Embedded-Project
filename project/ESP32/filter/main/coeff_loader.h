#ifndef COEFF_LOADER_H
#define COEFF_LOADER_H

#include "digital_filters.h"
#include <stdbool.h>

typedef enum
{
    CL_IDLE = 0,
    CL_WAITING, // stopped, waiting for utx lines
    CL_DONE_OK,
    CL_DONE_FAIL,
} coeff_load_state_e;

void coeff_loader_begin(filter_type_e target); // call on "scoeff:fmode"
void coeff_loader_feed_line(const char *line); // call per UART line while CL_WAITING
coeff_load_state_e coeff_loader_state(void);
bool coeff_loader_commit(filter_type_e *out_type, const void **out_coeff); // returns ptr to validated static buffer
void coeff_loader_abort(void);

#endif // COEFF_LOADER_H