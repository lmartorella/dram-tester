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
#define ROW_COUNT_MASK 128

typedef unsigned char BYTE;

extern void display_init();
extern void display_logTest(const char* text);
extern void display_logStatus(const char* text);

static char buf[16];

// Should require less than 2ms (max refresh perdiod): max 70 instructions per cell
static void writeRow(BYTE row, BYTE startData, BYTE deltaData) {
    BYTE data = startData;
    
    // Page mode write cycle with early write
    // Use early write to avoid contention: DI and DO are shorted
    ADDR_PORT = row;    
    NOP();  // Stabilize ADDR (long wires)
    RAS = 0;

    // Write mode for the whole row
    DATA_PORT_T = 0;
    for (BYTE col = 0; col < ROW_SIZE; col++) {
        // Prepare both addr and data
        ADDR_PORT = col;    
        DATA_PORT = data;
        // Early write
        WRITE = 0;
        NOP();

        // Strobe cas and sample data
        CAS = 0;
        NOP();

        // Deassert both lines
        WRITE = 1;
        CAS = 1;
        
        data += deltaData;
    } 

    // End write
    DATA_PORT_T = 1;
    RAS = 1;
}

// Should require less than 2ms (max refresh perdiod): max 70 instructions per cell
static void testRow(BYTE row, BYTE startData, BYTE deltaData) {    
    // Page mode read cycle
    ADDR_PORT = row;    
    NOP();  // Stabilize ADDR (long wires)
    RAS = 0;
    WRITE = 1;

    BYTE data = startData;
    for (BYTE col = 0; col < ROW_SIZE; col++) {
        ADDR_PORT = col;    
        NOP();  // Stabilize ADDR (long wires)
        CAS = 0;
        NOP();     
        BYTE d = DATA_PORT;
        CAS = 1;
        
        if (d != data) {
            sprintf(buf, "!R%x C%x ~%x", row, col, d);
            display_logStatus(buf);
            // stop
            while (1);
        }
        data += deltaData;
    } 
    RAS = 1;
}

static void refreshAll(BYTE row) {
    for (BYTE i = 0; i < ROW_COUNT; i++, row++) {
        row = row & ROW_COUNT_MASK;
        // Do refresh
        ADDR_PORT = row;   
        NOP();  // Stabilize ADDR (long wires)
        RAS = 0;
        NOP();
        RAS = 1;
        NOP();
    }
}

static void refreshAndWait(BYTE c) {
    for (BYTE i = 0; i < c; i++) {
        refreshAll(0);
        __delay_ms(2);
    }
}

static void testAll(BYTE startData, BYTE deltaData) {
    for (BYTE row = 0; row < ROW_COUNT; row++) {
        sprintf(buf, "..W %x", row);
        display_logStatus(buf);
        writeRow(row, startData, deltaData);

        // Refresh whole rows starting from the next one
        refreshAll(row + 1);
    }

    // Test memory persistence
    // Wait for 256 full refresh cycles (512ms)
    refreshAndWait(255);

    for (BYTE row = 0; row < ROW_COUNT; row++) {
        sprintf(buf, "..R %x", row);
        display_logStatus(buf);
        testRow(row, startData, deltaData);

        // Refresh whole rows starting from the next one
        refreshAll(row + 1);
    }
}

void main(void) {
    ADCON1 = 6; // Disable all PORTA and PORTE analog port
    
    WRITE_T = RAS_T = CAS_T = 0;  
    CAS = RAS = WRITE = 1;
    
    DATA_PORT_T = 0xff;
    ADDR_PORT_T = 0x00;
    OPTION_REGbits.nRBPU = 0; // Pullup to read 1 in case of no RAM

    display_init();
    
    // Recommended by Mostek at startup
    refreshAndWait(8);
        
    // Test all 0's
    display_logTest("All 0's");   
    testAll(0, 0);

    // Test all 1's
    display_logTest("All 1's");   
    testAll(0xff, 0);

    // Test alternate bit pattern 1
    display_logTest("0x55 pattern");   
    testAll(0x55, 0);

    // Test alternate bit pattern 2
    display_logTest("0xAA pattern");   
    testAll(0xAA, 0);

    // Test incremental pattern
    display_logTest("+1 pattern  ");   
    testAll(0xaa, 1);
    
    display_logStatus("Test passed!");
    while (1) { }
}
