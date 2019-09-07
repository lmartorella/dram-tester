#include <xc.h>
#include <stdio.h>
#define _XTAL_FREQ 20000000ul

// CONFIG
#pragma config FOSC = HS        // Oscillator Selection bits (INTOSC oscillator: I/O function on RA6/OSC2/CLKOUT pin, I/O function on RA7/OSC1/CLKIN)
#pragma config WDTE = OFF       // Watchdog Timer Enable bit (WDT disabled)
#pragma config PWRTE = ON       // Power-up Timer Enable bit (PWRT enabled)
#pragma config BOREN = ON       // Brown-out Detect Enable bit (BOD enabled)
#pragma config LVP = OFF        // Low-Voltage Programming Enable bit (RB4/PGM pin has digital I/O function, HV on MCLR must be used for programming)
#pragma config CPD = OFF        // Data EE Memory Code Protection bit (Data memory code protection off)
#pragma config CP = OFF         // Flash Program Memory Code Protection bit (Code protection off)

#define DATA_PORT   PORTB
#define DATA_PORT_T   TRISB
#define ADDR_PORT   PORTD
#define ADDR_PORT_T   TRISD
#define RAS PORTCbits.RC0
#define RAS_T TRISCbits.TRISC0
#define CAS PORTCbits.RC1
#define CAS_T TRISCbits.TRISC1
#define WRITE PORTCbits.RC2
#define WRITE_T TRISCbits.TRISC2

#define ROW_SIZE 128
#define ROW_COUNT 128

typedef unsigned char BYTE;

extern void display_init();
extern void display_logTest(const char* text);
extern void display_logStatus(const char* text);

static void testRow(BYTE row, BYTE startData, BYTE deltaData) {
    char buf[16];
    sprintf(buf, "..R %x", row);
    display_logStatus(buf);

    BYTE data = startData;
    
    // Page mode write cycle (from 4116 datasheet)
    // Use early write to avoid contention: DI and DO are shorted
    ADDR_PORT = row;    
    RAS = 0;
    NOP();
    NOP();

    DATA_PORT_T = 0;

    for (BYTE col = 0; col < ROW_SIZE; col++) {
        ADDR_PORT = col;    
        DATA_PORT = data;
        NOP();
        NOP();        

        WRITE = 0;
        NOP();
        CAS = 0;
        NOP();
        NOP();

        WRITE = 1;
        NOP();
        NOP();
        CAS = 1;
        
        data += deltaData;
        __delay_us(1);
    } 

    DATA_PORT_T = 1;
    RAS = 1;

    __delay_us(1);
    
    // Page mode read cycle
    ADDR_PORT = row;    
    RAS = 0;
    NOP();
    NOP();

    data = startData;

    for (BYTE col = 0; col < ROW_SIZE; col++) {
        ADDR_PORT = col;    
        NOP();
        NOP();
        CAS = 0;
        NOP();
        NOP();
        
        BYTE d = DATA_PORT;
        CAS = 1;
        
        if (d != data) {
            sprintf(buf, "!R%x C%x ~%x", row, col, d);
            display_logStatus(buf);
            while (1);
        }
        data += deltaData;
        __delay_us(1);
    } 
    RAS = 1;
}

void main(void) {
    ADCON1 = 6; // Disable all PORTA and PORTE analog port
    
    WRITE_T = RAS_T = CAS_T = 0;  
    CAS = RAS = WRITE = 1;
    
    DATA_PORT_T = 0xff;
    ADDR_PORT_T = 0x00;
    OPTION_REGbits.nRBPU = 0; // Pullup to read 1 in case of no RAM

    display_init();
        
    display_logTest("All 0's");   
    // Test all 0's
    for (BYTE row = 0; row < ROW_COUNT; row++) {
        testRow(row, 0, 0);
    }

    display_logTest("All 1's");   
    // Test all 0's
    for (BYTE row = 0; row < ROW_COUNT; row++) {
        testRow(row, 0xff, 0);
    }

    display_logTest("0x55 pattern");   
    // Test all 0's
    for (BYTE row = 0; row < ROW_COUNT; row++) {
        testRow(row, 0x55, 0);
    }

    display_logTest("0xAA pattern");   
    // Test all 0's
    for (BYTE row = 0; row < ROW_COUNT; row++) {
        testRow(row, 0xAA, 0);
    }

    display_logTest("+1 pattern  ");   
    // Test all 0's
    for (BYTE row = 0; row < ROW_COUNT; row++) {
        testRow(row, 0xaa, 1);
    }
    
    while (1) {
        display_logStatus("OK!    ");
    }
}
