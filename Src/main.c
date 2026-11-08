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
volatile uint8_t update_waveform_flag = 0;


/* ------------------------------------------------------------
   4x4 KEYPAD DRIVER
   ------------------------------------------------------------ */
const char keypad_map[4][4] = {
    {'1', '2', '3', 'A'}, // 1=Sine, 2=Triangle, 3=Sawtooth, A=Freq UP
    {'4', '5', '6', 'B'}, // 4=Square, B=Freq DOWN
    {'7', '8', '9', 'C'}, 
    {'*', '0', '#', 'D'}  
};

void Keypad_Init(void)
{
    /* 1. Set Rows (P2.0, P2.1, P2.2, P2.3) as Outputs */
    LPC_GPIO2->FIODIR |= (1<<0) | (1<<1) | (1<<2) | (1<<3);

    /* 2. Set Cols (P2.4, P2.5) and (P0.15, P0.16) as Inputs */
    LPC_GPIO2->FIODIR &= ~((1<<4) | (1<<5));
    LPC_GPIO0->FIODIR &= ~((1<<15) | (1<<16));

    /* 
     * 3. Enable Internal Pull-Up Resistors for the Columns 
     * PINMODE bits: 00 = Pull-Up (Default), 10 = No Resistor, 11 = Pull-Down
     */
    LPC_PINCON->PINMODE4 &= ~((3<<8) | (3<<10));   // P2.4, P2.5 to Pull-Up
    LPC_PINCON->PINMODE0 &= ~(3<<30);              // P0.15 to Pull-Up
    LPC_PINCON->PINMODE1 &= ~(3<<0);               // P0.16 to Pull-Up

    /* 4. Set all rows HIGH initially */
    LPC_GPIO2->FIOSET = (1<<0) | (1<<1) | (1<<2) | (1<<3);
}

char Keypad_Scan(void)
{
    int row;
    char key_pressed = 0;

    for (row = 0; row < 4; row++)
    {
        /* Set all rows HIGH */
        LPC_GPIO2->FIOSET = (1<<0) | (1<<1) | (1<<2) | (1<<3);
        
        /* Drive the current row LOW */
        LPC_GPIO2->FIOCLR = (1 << row);
        
        /* Brief delay for pin voltages to settle */
        delay_ms(1);

        /* Read the columns. If a col is LOW (0), the button is pressed */
        if ((LPC_GPIO2->FIOPIN & (1<<4)) == 0)  key_pressed = keypad_map[row][0]; // Col 1
        if ((LPC_GPIO2->FIOPIN & (1<<5)) == 0)  key_pressed = keypad_map[row][1]; // Col 2
        if ((LPC_GPIO0->FIOPIN & (1<<15)) == 0) key_pressed = keypad_map[row][2]; // Col 3
        if ((LPC_GPIO0->FIOPIN & (1<<16)) == 0) key_pressed = keypad_map[row][3]; // Col 4

        /* If we found a key, stop scanning */
        if (key_pressed != 0) break;
    }
    
    /* Reset all rows HIGH before exiting */
    LPC_GPIO2->FIOSET = (1<<0) | (1<<1) | (1<<2) | (1<<3);
    
    return key_pressed;
}

/* ------------------------------------------------------------
   MAIN ROUTINE
   ------------------------------------------------------------ */
/* Global state variables */
volatile uint32_t current_waveform = WAVE_SINE;
uint32_t current_freq = 10000;
uint8_t current_amplitude = 10; /* 10 = 100%, 5 = 50%, 1 = 10% */

/* ------------------------------------------------------------
   MAIN ROUTINE
   ------------------------------------------------------------ */
int main(void)
{
    char display_buffer[16];
    char key;
    uint8_t update_display = 1; // Flag to force an initial LCD draw

    SystemClock_Force96MHz();

    LPC_PINCON->PINSEL0 &= ~((3<<0) | (3<<2) | (3<<8) | (3<<10) | (3<<12) | (3<<14) | (3<<18));
    LPC_GPIO0->FIODIR |= (LCD_RS | LCD_EN | LCD_DATA);
    LPC_GPIO0->FIOCLR  = (LCD_RS | LCD_EN | LCD_DATA);

    lcd_init();
    DAC_Init();
    Keypad_Init();

    DAC_DMA_Start(current_waveform, 256, current_freq);

    while (1)
    {
        /* 1. Scan the keypad */
        key = Keypad_Scan();

        /* 2. Process the keypress */
        if (key != 0) 
        {
            if (key == '1') { current_waveform = WAVE_SINE;     update_display = 1; }
            if (key == '2') { current_waveform = WAVE_TRIANGLE; update_display = 1; }
            if (key == '3') { current_waveform = WAVE_SAWTOOTH; update_display = 1; }
            if (key == '4') { current_waveform = WAVE_SQUARE;   update_display = 1; }
            
            if (key == 'A') { 
                current_freq += 500; // Increase frequency by 500 Hz
                if (current_freq > 15000) current_freq = 15000; // Cap at 20 kHz
                update_display = 1; 
            }
            if (key == 'B') { 
                if (current_freq > 500) current_freq -= 500; // Decrease frequency
                else current_freq = 10; // Minimum floor
                update_display = 1; 
            }
						
						/* New Amplitude Controls */
            if (key == 'C') { 
                if (current_amplitude < 10) current_amplitude++; // Increase up to 100%
                update_display = 1; 
            }
            if (key == 'D') { 
                if (current_amplitude > 1) current_amplitude--;  // Decrease down to 10%
                update_display = 1; 
            }

            /* Update the physical hardware instantly */
            if (update_display) {
                DAC_DMA_Start(current_waveform, 256, current_freq);
            }

            /* 
             * Simple Software Debounce: 
             * Wait for the user to lift their finger before continuing 
             */
            while(Keypad_Scan() != 0) {
                delay_ms(10);
            }
        }

        /* 3. Update the LCD only if something changed */
        if (update_display) 
        {
            update_display = 0;

            /* Update Top Row (Waveform) */
            lcd_set_cursor(0, 0);
            if (current_waveform == WAVE_SINE)          lcd_string("Wave: SINE      ");
            else if (current_waveform == WAVE_TRIANGLE) lcd_string("Wave: TRIANGLE  ");
            else if (current_waveform == WAVE_SAWTOOTH) lcd_string("Wave: SAWTOOTH  ");
            else if (current_waveform == WAVE_SQUARE)   lcd_string("Wave: SQUARE    ");

            /* Update Bottom Row (Frequency) */
            sprintf(display_buffer, "F:%d A:%d0%%   ", current_freq, current_amplitude);
            lcd_set_cursor(0, 1);
            lcd_string(display_buffer);
        }

        /* Background loop delay to prevent CPU spinning */
        delay_ms(20);
    }
}
