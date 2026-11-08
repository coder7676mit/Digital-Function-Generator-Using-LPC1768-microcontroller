#include "LPC17xx.h"
#include "dac.h"
#include "waveforms.h"
#include <stdio.h>  /* Required for sprintf() */
#include <stdlib.h> /* Required for atoi() */

/* ------------------------------------------------------------
   Hardware Defines & Global Variables
   ------------------------------------------------------------ */
#define LCD_RS     (1 << 0)     // P0.0
#define LCD_EN     (1 << 7)     // P0.7
#define LCD_D4     (1 << 4)     // P0.4
#define LCD_D5     (1 << 5)     // P0.5
#define LCD_D6     (1 << 6)     // P0.6
#define LCD_D7     (1 << 9)     // P0.9
#define LCD_DATA   (LCD_D4 | LCD_D5 | LCD_D6 | LCD_D7)

/* DAC State Variables */
volatile uint32_t current_waveform = WAVE_SINE;
uint32_t current_freq = 10000;
uint8_t current_amplitude = 10; /* 10 = 100%, 5 = 50%, 1 = 10% */

/* UART Rx Buffers */
volatile char rx_buffer[32];
volatile uint8_t rx_index = 0;
volatile uint8_t command_ready = 0;

/* ------------------------------------------------------------
   Clock & LCD Drivers
   ------------------------------------------------------------ */
void SystemInit(void) { } 

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

void delay_ms(unsigned int ms)
{
    unsigned int i, j;
    for (i = 0; i < ms; i++) {
        for (j = 0; j < 24000; j++) { __NOP(); }
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

void lcd_set_cursor(unsigned char col, unsigned char row)
{
    unsigned char addr = col;
    if (row == 1) addr += 0x40; 
    lcd_cmd(0x80 | addr);
}

/* ------------------------------------------------------------
   Keypad Driver
   ------------------------------------------------------------ */
const char keypad_map[4][4] = {
    {'1', '2', '3', 'A'}, // 1=Sine, 2=Tri, 3=Saw, A=Freq UP
    {'4', '5', '6', 'B'}, // 4=Square, B=Freq DOWN
    {'7', '8', '9', 'C'}, // C=Amp UP
    {'*', '0', '#', 'D'}  // D=Amp DOWN
};

void Keypad_Init(void)
{
    LPC_GPIO2->FIODIR |= (1<<0) | (1<<1) | (1<<2) | (1<<3);
    LPC_GPIO2->FIODIR &= ~((1<<4) | (1<<5));
    LPC_GPIO0->FIODIR &= ~((1<<15) | (1<<16));
    LPC_PINCON->PINMODE4 &= ~((3<<8) | (3<<10));   
    LPC_PINCON->PINMODE0 &= ~(3<<30);              
    LPC_PINCON->PINMODE1 &= ~(3<<0);               
    LPC_GPIO2->FIOSET = (1<<0) | (1<<1) | (1<<2) | (1<<3);
}

char Keypad_Scan(void)
{
    int row;
    char key_pressed = 0;
    for (row = 0; row < 4; row++) {
        LPC_GPIO2->FIOSET = (1<<0) | (1<<1) | (1<<2) | (1<<3);
        LPC_GPIO2->FIOCLR = (1 << row);
        delay_ms(1);
        if ((LPC_GPIO2->FIOPIN & (1<<4)) == 0)  key_pressed = keypad_map[row][0];
        if ((LPC_GPIO2->FIOPIN & (1<<5)) == 0)  key_pressed = keypad_map[row][1];
        if ((LPC_GPIO0->FIOPIN & (1<<15)) == 0) key_pressed = keypad_map[row][2];
        if ((LPC_GPIO0->FIOPIN & (1<<16)) == 0) key_pressed = keypad_map[row][3];
        if (key_pressed != 0) break;
    }
    LPC_GPIO2->FIOSET = (1<<0) | (1<<1) | (1<<2) | (1<<3);
    return key_pressed;
}

/* ------------------------------------------------------------
   UART0 Driver & Interrupt (115200 Baud @ 96 MHz)
   ------------------------------------------------------------ */
void UART0_Init(void) 
{
    LPC_PINCON->PINSEL0 &= ~(0xF << 4);
    LPC_PINCON->PINSEL0 |=  (0x5 << 4);
    LPC_SC->PCONP |= (1 << 3);
    LPC_SC->PCLKSEL0 &= ~(3 << 6);
    LPC_SC->PCLKSEL0 |=  (1 << 6);
    LPC_UART0->LCR = 0x83; 
    LPC_UART0->DLL = 52;
    LPC_UART0->DLM = 0;
    LPC_UART0->LCR = 0x03; 
    LPC_UART0->IER = 0x01; 
    NVIC_EnableIRQ(UART0_IRQn);
}

void UART0_IRQHandler(void) 
{
    uint32_t int_id = LPC_UART0->IIR;
    char c;
    
    if ((int_id & 0x0E) == 0x04) 
    {
        c = LPC_UART0->RBR; 
        if (c == '\r' || c == '\n') 
        {
            if (rx_index > 0) {
                rx_buffer[rx_index] = '\0'; 
                command_ready = 1;          
                rx_index = 0;               
            }
        } 
        else if (rx_index < 30) 
        {
            rx_buffer[rx_index++] = c;
        }
    }
}

/* ------------------------------------------------------------
   MAIN ROUTINE
   ------------------------------------------------------------ */
int main(void)
{
    /* C89 Strict: All variables declared at the very top */
    char display_buffer[16];
    char key;
    uint8_t update_display = 1; 
    uint32_t new_freq;
    uint32_t new_amp;
    char w_type;

    /* Initialize Hardware */
    SystemClock_Force96MHz();
    LPC_PINCON->PINSEL0 &= ~((3<<0) | (3<<2) | (3<<8) | (3<<10) | (3<<12) | (3<<14) | (3<<18));
    LPC_GPIO0->FIODIR |= (LCD_RS | LCD_EN | LCD_DATA);
    LPC_GPIO0->FIOCLR  = (LCD_RS | LCD_EN | LCD_DATA);

    lcd_init();
    DAC_Init();
    Keypad_Init();
    UART0_Init(); /* Start the Serial Port */

    DAC_DMA_Start(current_waveform, 256, current_freq);

    while (1)
    {
        /* 1. PROCESS KEYPAD COMMANDS */
        key = Keypad_Scan();
        if (key != 0) 
        {
            if (key == '1') { current_waveform = WAVE_SINE;     update_display = 1; }
            if (key == '2') { current_waveform = WAVE_TRIANGLE; update_display = 1; }
            if (key == '3') { current_waveform = WAVE_SAWTOOTH; update_display = 1; }
            if (key == '4') { current_waveform = WAVE_SQUARE;   update_display = 1; }
            
            if (key == 'A') { 
                current_freq += 500; 
                if (current_freq > 15000) current_freq = 15000; 
                update_display = 1; 
            }
            if (key == 'B') { 
                if (current_freq > 500) current_freq -= 500; 
                else current_freq = 10; 
                update_display = 1; 
            }
            if (key == 'C') { 
                if (current_amplitude < 10) current_amplitude++; 
                update_display = 1; 
            }
            if (key == 'D') { 
                if (current_amplitude > 1) current_amplitude--;  
                update_display = 1; 
            }

            if (update_display) {
                DAC_DMA_Start(current_waveform, 256, current_freq);
            }
            while(Keypad_Scan() != 0) delay_ms(10); /* Debounce */
        }

        /* 2. PROCESS UART SERIAL COMMANDS */
        if (command_ready) 
        {
            command_ready = 0; 
            
            /* Format: F15000 (Set Freq to 15000) */
            if (rx_buffer[0] == 'F' || rx_buffer[0] == 'f') 
            {
                new_freq = atoi((const char *)&rx_buffer[1]);
                if (new_freq >= 10 && new_freq <= 15000) {
                    current_freq = new_freq;
                    update_display = 1;
                }
            } 
            /* Format: W1 (Set Wave to Sine) */
            else if (rx_buffer[0] == 'W' || rx_buffer[0] == 'w') 
            {
                w_type = rx_buffer[1];
                if (w_type == '1')      current_waveform = WAVE_SINE;
                else if (w_type == '2') current_waveform = WAVE_TRIANGLE;
                else if (w_type == '3') current_waveform = WAVE_SAWTOOTH;
                else if (w_type == '4') current_waveform = WAVE_SQUARE;
                update_display = 1;
            }
            /* Format: A5 (Set Amplitude to 50%) */
            else if (rx_buffer[0] == 'A' || rx_buffer[0] == 'a') 
            {
                new_amp = atoi((const char *)&rx_buffer[1]);
                if (new_amp >= 1 && new_amp <= 10) {
                    current_amplitude = new_amp;
                    update_display = 1;
                }
            }

            if (update_display) {
                DAC_DMA_Start(current_waveform, 256, current_freq);
            }
        }

        /* 3. UPDATE LCD DISPLAY */
        if (update_display) 
        {
            update_display = 0;

            lcd_set_cursor(0, 0);
            if (current_waveform == WAVE_SINE)          lcd_string("Wave: SINE      ");
            else if (current_waveform == WAVE_TRIANGLE) lcd_string("Wave: TRIANGLE  ");
            else if (current_waveform == WAVE_SAWTOOTH) lcd_string("Wave: SAWTOOTH  ");
            else if (current_waveform == WAVE_SQUARE)   lcd_string("Wave: SQUARE    ");

            sprintf(display_buffer, "F:%d A:%d0%%   ", current_freq, current_amplitude);
            lcd_set_cursor(0, 1);
            lcd_string(display_buffer);
        }

        delay_ms(20); /* Background loop delay */
    }
}