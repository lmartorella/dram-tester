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
#define ROW_COUNT_MASK 127

typedef unsigned char BYTE;

extern void display_init();
extern void display_logTest(const char* text);
extern void display_logStatus(const char* text);

static char buf[16];
// Cycle after each reset
typedef enum {
    // Use the full suite to test DRAM, refresh, whole bits, all patterns, etc...
    MODE_FULL_TEST_BY_PAGE,
    // Same, but don't use page write
    MODE_FULL_TEST_BY_BIT,
            
    // Test a single row for continous write. 
    // Useful to check power line noise during operations.
    MODE_CONTINOUS_WRITE,
            
    // Test write/read a single cell every microsecond. Useful to trace 
    // timings and response with an oscilloscope. Using a faulty cell it is
    // possible to check which type of damage the chip has.
    MODE_ONLY_BIT_0,

    MODE_FIRST = MODE_FULL_TEST_BY_PAGE,
    MODE_LAST = MODE_ONLY_BIT_0
} TEST_PROGRAM_t;
static persistent TEST_PROGRAM_t s_testProgram;

// Write a single bit
static void writeCell(BYTE row, BYTE col, BYTE data) {    
    // Use early write to avoid contention: DI and DO are shorted
    ADDR_PORT = row;    
    NOP();  // Stabilize ADDR (long wires)
    RAS = 0;
    DATA_PORT_T = 0;

    // Prepare both addr and data
    ADDR_PORT = col;
    DATA_PORT = data;
    NOP();
    // Early write
    WRITE = 0;
    NOP();

    // Strobe cas and sample data
    CAS = 0;
    NOP();

    // Deassert both lines
    WRITE = 1;
    CAS = 1;

    // End write
    DATA_PORT_T = 0xff;
    RAS = 1;
}

// Read a single bit
static BYTE readCell(BYTE row, BYTE col) {    
    ADDR_PORT = row;    
    NOP();  // Stabilize ADDR (long wires)
    RAS = 0;

    // Prepare both addr and data
    ADDR_PORT = col;
    NOP();

    // Strobe cas and sample data
    CAS = 0;
    NOP();
    BYTE b = DATA_PORT;

    // Deassert both lines
    CAS = 1;
    NOP();
    RAS = 1;
    
    return b;
}

// Should require less than 2ms (max refresh perdiod): max 70 instructions per cell
static void writeRow_page(BYTE row, BYTE startData, BYTE deltaData) {
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
        NOP();
        // Early write
        WRITE = 0;
        NOP();

        // Strobe cas and sample data
        CAS = 0;
        NOP();

        // Deassert both lines
        CAS = 1;
        WRITE = 1;
        
        data += deltaData;
    } 

    // End write
    RAS = 1;
    DATA_PORT_T = 0xff;
}

// Should require less than 2ms (max refresh perdiod): max 70 instructions per cell
static void writeRow_bit(BYTE row, BYTE startData, BYTE deltaData) {
    BYTE data = startData;
    
    // Write mode for the whole row
    DATA_PORT_T = 0;

    for (BYTE col = 0; col < ROW_SIZE; col++) {
        // Use early write to avoid contention: DI and DO are shorted
        ADDR_PORT = row;    
        NOP();  // Stabilize ADDR (long wires)
        RAS = 0;

        // Prepare both addr and data
        ADDR_PORT = col;
        DATA_PORT = data;
        NOP();
        // Early write
        WRITE = 0;
        NOP();

        // Strobe cas and sample data
        CAS = 0;
        NOP();

        // Deassert both lines
        CAS = 1;
        WRITE = 1;
        
        data += deltaData;
        RAS = 1;
    } 

    // End write
    DATA_PORT_T = 0xff;
}

static void refreshAll(BYTE row);
static void refreshAndWait(BYTE row, short count);

// Should require less than 2ms (max refresh perdiod): max 70 instructions per cell
static void testRow_page(BYTE row, BYTE startData, BYTE deltaData) {    
    // Page mode read cycle
    ADDR_PORT = row;    
    NOP();  // Stabilize ADDR (long wires)
    RAS = 0;

    BYTE data = startData;
    for (BYTE col = 0; col < ROW_SIZE; col++) {
        ADDR_PORT = col;    
        NOP();  // Stabilize ADDR (long wires)
        CAS = 0;
        NOP();     
        BYTE d = DATA_PORT;
        CAS = 1;
        
        if ((d & ~0x2) != (data & ~0x2)) {
            // Stop row operation
            RAS = 1;
            // Report error and pause to let the error to be visible
            refreshAll(row + 1);
            sprintf(buf, "!R%2x C%2x ~%2x", row, col, d);
            refreshAll(row + 1);
            display_logStatus(buf);
            
            // Wait 0.75 second
            refreshAndWait(row + 1, 750);
            // Continue the test

            ADDR_PORT = row;    
            NOP();  // Stabilize ADDR (long wires)
            RAS = 0;
        }
        data += deltaData;
    } 
    RAS = 1;
}

static void refreshAll(BYTE row) {
    for (BYTE i = 0; i < ROW_COUNT; i++, row++) {
        // Do refresh
        ADDR_PORT = row & ROW_COUNT_MASK;   
        NOP();  // Stabilize ADDR (long wires)
        RAS = 0;
        NOP();
        NOP();
        NOP();
        RAS = 1;
    }
}

static void refreshAndWait(BYTE row, short count) {
    for (short i = 0; i < count; i++) {
        refreshAll(row);
        __delay_ms(1);
    }
}

static void testAllWithRefresh(BYTE startData, BYTE deltaData) {
    for (BYTE row = 0x38; row < ROW_COUNT; row++) {
        //sprintf(buf, "..W %x", row);
        //display_logStatus(buf);
        if (s_testProgram == MODE_FULL_TEST_BY_PAGE) {
            writeRow_page(row, startData, deltaData);
        } else {
            writeRow_bit(row, startData, deltaData);
        }

        // Refresh whole rows starting from the next one
        refreshAll(row + 1);
    }

    // Test memory persistence
    // Wait for 512 full refresh cycles (512ms)
    refreshAndWait(0, 512);

    for (BYTE row = 0x38; row < ROW_COUNT; row++) {
        //sprintf(buf, "..R %x", row);
        //display_logStatus(buf);
        testRow_page(row, startData, deltaData);

        // Refresh whole rows starting from the next one
        refreshAll(row + 1);
    }
}

static void display_testName(const char* name) {
    sprintf(buf, "%c %s", s_testProgram == MODE_FULL_TEST_BY_PAGE ? 'P' : 'B', name);
    display_logTest(buf);
}

void main(void) {
    ADCON1 = 6; // Disable all PORTA and PORTE analog port
    
    WRITE_T = RAS_T = CAS_T = 0;  
    CAS = RAS = WRITE = 1;
    
    DATA_PORT_T = 0xff; // High-Z
    ADDR_PORT_T = 0x00; // Out
    OPTION_REGbits.nRBPU = 0; // Pullup to read 1 in case of no RAM

    display_init();
    
    // Recommended by Mostek at startup
    refreshAndWait(0, 8);
    
    // Not initialized after reset
    while (1) {
        // Get and increment for next reset
        switch (++s_testProgram) {
            case MODE_FULL_TEST_BY_PAGE:
            case MODE_FULL_TEST_BY_BIT:
                // Test all 0's
                display_testName("All 0's");   
                testAllWithRefresh(0, 0);

                // Test all 1's
                display_testName("All 1's");   
                testAllWithRefresh(0xff, 0);

                // Test alternate bit pattern 1
                display_testName("0x55 pattern");   
                testAllWithRefresh(0x55, 0);

                // Test alternate bit pattern 2
                display_testName("0xAA pattern");   
                testAllWithRefresh(0xAA, 0);

                // Test incremental pattern
                display_testName("+1 pattern  ");   
                testAllWithRefresh(0xaa, 1);

                display_logStatus("Test passed!");
                while (1) { }

            case MODE_CONTINOUS_WRITE:
                display_logTest("Continuous write");
                while (1) {
                    writeRow_bit(0, 0, 1);
                }

            case MODE_ONLY_BIT_0:
                display_logTest("Test single bit");
                while (1) { 
                    writeCell(0, 0, 0xaa);
                    __delay_ms(1);
                    readCell(0, 0);
                    __delay_ms(1);
                }
        }
    }      
}
