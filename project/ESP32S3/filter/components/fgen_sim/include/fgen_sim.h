#ifndef FGEN_SIM_H
#define FGEN_SIM_H

#include<stdint.h>

typedef enum{
    _FGEN_SIM_OFF=0,
    _FGEN_SIM_SIN,
    _FGEN_SIM_IMP,
}fgen_sim_mode_e;

typedef struct 
{
    uint32_t phase; // Q30 phase Accumulator
    uint32_t dphase; // phase increment per tick
    int16_t amp;
    uint32_t fs_hz; // sampling frequency in hz
    fgen_sim_mode_e mode;
}fgen_sim_t;


void    fgen_sim_init(fgen_sim_t *g, uint32_t fs_hz);
void    fgen_sim_set_freq(fgen_sim_t *g, float freq_hz);
void    fgen_sim_set_amp(fgen_sim_t *g, int16_t amp);
void    fgen_sim_set_mode(fgen_sim_t *g, fgen_sim_mode_e m);
int16_t fgen_sim_tic(fgen_sim_t *g, float *phase_norm_out);

#endif // FGEN_SIM_H