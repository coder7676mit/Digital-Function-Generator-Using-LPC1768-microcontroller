#include "LPC17xx.h"
#include "dac.h"
#include "waveforms.h"

/* Tell dac.c to look for this variable in main.c */
extern uint8_t current_amplitude; 

static uint32_t dma_lut[LUT_SIZE];

/* CRITICAL FIX 1: Force 4-byte memory alignment for the DMA LLI hardware */
typedef struct __attribute__((aligned(4)))
{
    uint32_t SrcAddr;
    uint32_t DstAddr;
    uint32_t NextLLI;
    uint32_t Control;
} DMA_LLI;

static volatile DMA_LLI dma_lli;

void DAC_Init(void)
{
    /* P0.26 = AOUT (Function 2 = DAC) */
    LPC_PINCON->PINSEL1 &= ~(3U << 20);
    LPC_PINCON->PINSEL1 |= (2U << 20);

    /* PCLK_DAC = CCLK (96 MHz) */
    LPC_SC->PCLKSEL0 &= ~(3U << 22);
    LPC_SC->PCLKSEL0 |= (1U << 22);

    /* Enable GPDMA peripheral clock */
    LPC_SC->PCONP |= (1U << 29);

    /* Disable DAC DMA while configuring */
    LPC_DAC->DACCTRL = 0;
    LPC_DAC->DACR = 0;

    /* Enable GPDMA controller */
    LPC_GPDMA->DMACConfig = 1;
    while ((LPC_GPDMA->DMACConfig & 1) == 0) { }
}

void DAC_Output(uint16_t value)
{
    if (value > 1023) value = 1023;
    LPC_DAC->DACR = ((uint32_t)value << 6);
}

void DAC_SetFrequency(uint32_t frequency, uint32_t samples)
{
    uint32_t sample_rate = frequency * samples;
    uint32_t dac_count = 96000000UL / sample_rate;

    if (dac_count > 0) dac_count--;
    if (dac_count > 65535) dac_count = 65535;

    /* Instantly update the DAC timer reload value */
    LPC_DAC->DACCNTVAL = (uint16_t)dac_count;
}

void DAC_DMA_Start(uint32_t waveform, uint32_t samples, uint32_t frequency)
{
    uint32_t i;
    const uint16_t *source_lut;
    /* Move declarations to the top of the block to satisfy C89 standard */
    int32_t centered;
    uint32_t final_val;

    if (waveform == WAVE_SINE)           source_lut = sine_lut;
    else if (waveform == WAVE_SAWTOOTH)  source_lut = sawtooth_lut;
    else if (waveform == WAVE_SQUARE)    source_lut = square_lut;
    else                                 source_lut = triangle_lut;

    /* Convert 10-bit LUT to 32-bit DMA format (shift left by 6) */
    for (i = 0; i < samples; i++) {
        /* 1. Remove the DC offset to center the wave at 0 */
        centered = (int32_t)source_lut[i] - 512;
        
        /* 2. Scale the amplitude (current_amplitude is 1 to 10) */
        centered = (centered * current_amplitude) / 10;
        
        /* 3. Restore the DC offset */
        final_val = (uint32_t)(centered + 512);
        
        /* 4. Shift left by 6 to fit the DACR hardware register */
        dma_lut[i] = (final_val << 6);
    }

    /* Stop DMA channel 0 and DAC before reconfiguration */
    LPC_GPDMACH0->DMACCConfig = 0;
    LPC_DAC->DACCTRL = 0;
    

    LPC_GPDMA->DMACIntTCClear = (1 << 0);
    LPC_GPDMA->DMACIntErrClr = (1 << 0);

    /* Configure Linked List Item */
    dma_lli.SrcAddr = (uint32_t)&dma_lut[0];
    dma_lli.DstAddr = (uint32_t)&LPC_DAC->DACR;
    dma_lli.NextLLI = (uint32_t)&dma_lli; // Loop back to self
    
    dma_lli.Control =
        (samples & 0x0FFF) |
        (0 << 12) | /* Source burst = 1 */
        (0 << 15) | /* Destination burst = 1 */
        (2 << 18) | /* Source width = 32-bit */
        (2 << 21) | /* Destination width = 32-bit */
        (1 << 26);  /* Source increment enabled */

    LPC_GPDMACH0->DMACCControl = dma_lli.Control;
    LPC_GPDMACH0->DMACCSrcAddr = dma_lli.SrcAddr;
    LPC_GPDMACH0->DMACCDestAddr = dma_lli.DstAddr;
    LPC_GPDMACH0->DMACCLLI = dma_lli.NextLLI;

    /* Channel Config: DAC DMA request number = 7, Memory to Peripheral */
    LPC_GPDMACH0->DMACCConfig = (7 << 6) | (1 << 11);

    /* Set Initial Frequency */
    DAC_SetFrequency(frequency, samples);

    /* CRITICAL FIX 2: Enable INT_CTRL (Bit 0) so DAC uses the timeout counter */
    LPC_DAC->DACCTRL =
        (1 << 0) | /* INT_CTRL: 1 = DMA request on Timer timeout */
        (1 << 1) | /* DBLBUF_ENA */
        (1 << 2) | /* CNT_ENA */
        (1 << 3);  /* DMA_ENA */

    /* Enable DMA Channel 0 */
    LPC_GPDMACH0->DMACCConfig |= 1;
}