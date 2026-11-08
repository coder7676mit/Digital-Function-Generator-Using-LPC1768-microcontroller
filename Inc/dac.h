#ifndef DAC_H
#define DAC_H

#include <stdint.h>

#define WAVE_SINE       0
#define WAVE_TRIANGLE   1
#define WAVE_SAWTOOTH   2
#define WAVE_SQUARE     3

void DAC_Init(void);
void DAC_Output(uint16_t value);
void DAC_DMA_Start(uint32_t waveform, uint32_t samples, uint32_t frequency);

/* New function to change frequency dynamically */
void DAC_SetFrequency(uint32_t frequency, uint32_t samples);

#endif