#include "LPC17xx.h"
#include "dac.h"
#include "waveforms.h"
#include <stdio.h> // Required for sprintf()

/* ------------------------------------------------------------
   Hardware Defines
   ------------------------------------------------------------ */
#define LCD_RS     (1 << 0)     // P0.0
#define LCD_EN     (1 << 7)     // P0.7
#define LCD_D4     (1 << 4)     // P0.4
#define LCD_D5     (1 << 5)     // P0.5
#define LCD_D6     (1 << 6)     // P0.6
#define LCD_D7     (1 << 9)     // P0.9
#define LCD_DATA   (LCD_D4 | LCD_D5 | LCD_D6 | LCD_D7)

/* ------------------------------------------------------------
   System Clock Configuration
   ------------------------------------------------------------ */
void SystemInit(void) { } // Dummy to satisfy startup file

void SystemClock_Force96MHz(void) 
{
    LPC_SC->PLL0CON = 0x00;
    LPC_SC->PLL0FEED = 0xAA;
    LPC_SC->PLL0FEED = 0x55;

    LPC_SC->SCS = 0x20;
    while ((LPC_SC->SCS & 0x40) == 0); 

    LPC_SC->CLKSRCSEL = 0x01;
    LPC_SC->PLL0CFG = 15 | (0 << 16);
    LPC_SC->PLL0FEED = 0xAA;
    LPC_SC->PLL0FEED = 0x55;

    LPC_SC->PLL0CON = 0x01;
    LPC_SC->PLL0FEED = 0xAA;
    LPC_SC->PLL0FEED = 0x55;

    while ((LPC_SC->PLL0STAT & (1<<26)) == 0);

    LPC_SC->CCLKCFG = 3; 

    LPC_SC->PLL0CON = 0x03;
    LPC_SC->PLL0FEED = 0xAA;
    LPC_SC->PLL0FEED = 0x55;
}

/* ------------------------------------------------------------
   LCD Driver (Calibrated for 96 MHz)
   ------------------------------------------------------------ */
void delay_ms(unsigned int ms)
{
    unsigned int i, j;
    for (i = 0; i < ms; i++) {
        /* Roughly 1ms at 96 MHz */
        for (j = 0; j < 24000; j++) {
            __NOP();
        }
    }
}

void lcd_enable(void)
{
    LPC_GPIO0->FIOSET = LCD_EN;
    delay_ms(1);
    LPC_GPIO0->FIOCLR = LCD_EN;
    delay_ms(1);
}

void lcd_send_nibble(unsigned char data)
{
    LPC_GPIO0->FIOCLR = LCD_DATA;
    if (data & 0x01) LPC_GPIO0->FIOSET = LCD_D4;
    if (data & 0x02) LPC_GPIO0->FIOSET = LCD_D5;
    if (data & 0x04) LPC_GPIO0->FIOSET = LCD_D6;
    if (data & 0x08) LPC_GPIO0->FIOSET = LCD_D7;
    lcd_enable();
}

void lcd_cmd(unsigned char cmd)
{
    LPC_GPIO0->FIOCLR = LCD_RS;
    lcd_send_nibble(cmd >> 4);
    lcd_send_nibble(cmd & 0x0F);
    delay_ms(2);
}

void lcd_data(unsigned char data)
{
    LPC_GPIO0->FIOSET = LCD_RS;
    lcd_send_nibble(data >> 4);
    lcd_send_nibble(data & 0x0F);
    delay_ms(1);
}

void lcd_init(void)
{
    delay_ms(20);
    LPC_GPIO0->FIOCLR = LCD_RS;
    lcd_send_nibble(0x03); delay_ms(5);
    lcd_send_nibble(0x03); delay_ms(1);
    lcd_send_nibble(0x03); delay_ms(1);
    lcd_send_nibble(0x02); delay_ms(1);

    lcd_cmd(0x28); /* 4-bit, 2 lines, 5x8 font */
    lcd_cmd(0x0C); /* Display ON, cursor OFF */
    lcd_cmd(0x01); /* Clear display */
    delay_ms(2);
    lcd_cmd(0x06); /* Entry mode */
}

void lcd_string(char *str)
{
    while (*str) {
        lcd_data(*str);
        str++;
    }
}

/* NEW: Move cursor to row 0 (top) or row 1 (bottom), col 0-15 */
void lcd_set_cursor(unsigned char col, unsigned char row)
{
    unsigned char addr = col;
    if (row == 1) {
        addr += 0x40; 
    }
    lcd_cmd(0x80 | addr);
}

// sub-routine

/* Global variables to track the current state */
/* Global variables to track the current state */
volatile uint32_t current_waveform = WAVE_SINE;
volatile uint8_t update_waveform_flag = 0;

/* Interrupt Handler for physical mbed pin 26 (P2.0) */
void EINT3_IRQHandler(void)
{
    /* Check if P2.0 (mbed p26) caused the interrupt */
    if (LPC_GPIOINT->IO2IntStatR & (1 << 0))
    {
        /* 1. Clear the hardware interrupt flag */
        LPC_GPIOINT->IO2IntClr = (1 << 0);

        /* 2. Disable further interrupts to ignore wire bouncing */
        NVIC_DisableIRQ(EINT3_IRQn);

        /* 3. Cycle to the next waveform */
        current_waveform++;
        if (current_waveform > WAVE_SQUARE) {
            current_waveform = WAVE_SINE;
        }

        /* 4. Trigger the update flag */
        update_waveform_flag = 1;
    }
}

/* Initialization for physical mbed pin 26 (P2.0) */
void External_Interrupt_Init(void)
{
    /* 1. Configure P2.0 (mbed pin 26) as a GPIO Input */
    LPC_GPIO2->FIODIR &= ~(1 << 0);

    /* 2. Enable internal Pull-Down resistor on P2.0 to prevent floating */
    LPC_PINCON->PINMODE4 |= (3 << 0);

    /* 3. Enable Rising Edge interrupt for P2.0 */
    LPC_GPIOINT->IO2IntEnR |= (1 << 0);

    /* 4. Clear any pending interrupts */
    LPC_GPIOINT->IO2IntClr = (1 << 0);

    /* 5. Enable the shared GPIO interrupt vector */
    NVIC_EnableIRQ(EINT3_IRQn);
}

/* ------------------------------------------------------------
   MAIN ROUTINE
   ------------------------------------------------------------ */
int main(void)
{
    uint32_t current_freq = 15000;
    char display_buffer[16];

    SystemClock_Force96MHz();

    LPC_PINCON->PINSEL0 &= ~((3<<0) | (3<<2) | (3<<8) | (3<<10) | (3<<12) | (3<<14) | (3<<18));
    LPC_GPIO0->FIODIR |= (LCD_RS | LCD_EN | LCD_DATA);
    LPC_GPIO0->FIOCLR  = (LCD_RS | LCD_EN | LCD_DATA);

    lcd_init();
    DAC_Init();
    
    /* Initialize the wire interrupt on p26 */
    External_Interrupt_Init();

    DAC_DMA_Start(current_waveform, 256, current_freq);

    /* Print initial text to LCD */
    lcd_set_cursor(0, 0);
    lcd_string("Wave: SINE      "); 
    
    sprintf(display_buffer, "Freq: %d Hz    ", current_freq);
    lcd_set_cursor(0, 1);
    lcd_string(display_buffer);

    while (1)
    {
        /* --- INTERRUPT HANDLING --- */
        if (update_waveform_flag) {
            update_waveform_flag = 0;
            
            /* Restart DMA with the new waveform shape */
            DAC_DMA_Start(current_waveform, 256, current_freq);

            /* Update LCD Top Row */
            lcd_set_cursor(0, 0);
            if (current_waveform == WAVE_SINE)          lcd_string("Wave: SINE      ");
            else if (current_waveform == WAVE_TRIANGLE) lcd_string("Wave: TRIANGLE  ");
            else if (current_waveform == WAVE_SAWTOOTH) lcd_string("Wave: SAWTOOTH  ");
            else if (current_waveform == WAVE_SQUARE)   lcd_string("Wave: SQUARE    ");

            /* DEBOUNCE: Ignore all wire sparks for 500ms */
            delay_ms(500); 

            /* Clear any bounces that happened during the delay and re-enable p26 */
            LPC_GPIOINT->IO2IntClr = (1 << 0);
            NVIC_EnableIRQ(EINT3_IRQn);
        }

        /* 
         * CRITICAL: A short delay so the CPU isn't spinning at 
         * maximum speed doing nothing. This keeps the system stable. 
         */
        delay_ms(50);
    }
}
