#include <stdio.h>
#include <math.h>

#include "fgen_sim.h"

#define FGEN_PHASE_BITS 30U
#define FGEN_PHASE_MAX (1UL << FGEN_PHASE_BITS)


void fgen_sim_init(fgen_sim_t *g, uint32_t fs_hz)
{
    g->phase = 0;
    g->dphase = 0;
    g->amp = 2000;
    g->mode = _FGEN_SIM_SIN;
    g->fs_hz = fs_hz;
}

void fgen_sim_set_freq(fgen_sim_t *g, float freq_hz)
{
    g->dphase = (uint32_t)((freq_hz / (float)g->fs_hz) * (float)FGEN_PHASE_MAX);
}

void fgen_sim_set_amp(fgen_sim_t *g, int16_t amp)
{
    g->amp = amp;
}
void fgen_sim_set_mode(fgen_sim_t *g, fgen_sim_mode_e m)
{
    g->mode = m;
}

int16_t fgen_sim_tic(fgen_sim_t *g, float *phase_norm_out)
{
    uint32_t prev_phase = g->phase;
    g->phase = (g->phase + g->dphase) & (FGEN_PHASE_MAX - 1);
    int full_rot = (g->phase < prev_phase) ? 1 : 0;

    float phase_norm = (float)g->phase / (float)FGEN_PHASE_MAX;
    if (phase_norm_out) *phase_norm_out = phase_norm;

    switch (g->mode) {
    case _FGEN_SIM_SIN:
        return (int16_t)((float)g->amp * sinf(2.0f * (float)M_PI * phase_norm));
    case _FGEN_SIM_IMP:
        return full_rot ? g->amp : 0;
    default:
        return 0;
    }
}