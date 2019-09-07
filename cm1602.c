#include <xc.h>
#define _XTAL_FREQ 20000000ul

// Low-level commands
enum CM1602_ENTRYMODE
{
	// RAM PTR increment after each read/write
	MODE_INCREMENT = 0x2,
	// RAM PTR decrement after each read/write
	MODE_DECREMENT = 0,
	// Display will not shift left/right after each read/write
	MODE_SHIFTOFF = 0,
	// Display will shift left/right after each read/write
	MODE_SHIFTON = 1,
};

enum CM1602_ENABLE
{
	ENABLE_DISPLAY = 0x4,
	ENABLE_CURSOR = 0x2,
	ENABLE_CURSORBLINK = 0x1
};

enum CM1602_SHIFT
{
	// Shift cursor
	SHIFT_CURSOR = 0,
	// Shift display
	SHIFT_DISPLAY = 0x8,
	// Shift to left
	SHIFT_LEFT = 0,
	// Shift to right
	SHIFT_RIGHT = 0x4
};

#define CMD_CLEAR 		0x1
#define CMD_HOME 		0x2
#define CMD_ENTRY 		0x4
#define CMD_ENABLE 		0x8
#define CMD_SHIFT 		0x10
#define CMD_FUNCSET		0x20
#define CMD_SETCGRAM	0x40
#define CMD_SETDDRAM	0x80

#define CMD_FUNCSET_DL_8	0x10
#define CMD_FUNCSET_DL_4	0x00
#define CMD_FUNCSET_LN_2	0x08
#define CMD_FUNCSET_LN_1	0x00
#define CMD_FUNCSET_FS_11	0x04
#define CMD_FUNCSET_FS_7	0x00

#define CM1602_PORT PORTA
#define CM1602_TRIS TRISA
#define CM1602_IF_BIT_RW PORTEbits.RE0
#define CM1602_IF_BIT_EN PORTEbits.RE1
#define CM1602_IF_BIT_RS PORTEbits.RE2
#define CM1602_IF_TRIS_RW TRISEbits.TRISE0
#define CM1602_IF_TRIS_EN TRISEbits.TRISE1
#define CM1602_IF_TRIS_RS TRISEbits.TRISE2

typedef unsigned char BYTE;

// Clock the control bits in order to push the 4/8 bits to the display.
// In case of 4-bit, the lower data is sent and the HIGH part should be ZERO
static void pulsePort(BYTE v) {
    CM1602_IF_BIT_EN = 1;
    NOP();
    CM1602_PORT = v & 0xF;
    NOP();
    CM1602_IF_BIT_EN = 0;
}

// write a byte to the port
static void writeByte(BYTE data) {
	// 4-bit interface. Send high first
	pulsePort(data >> 4);
	pulsePort(data & 0xf);
}

static void writeCmd(BYTE data) {
	CM1602_IF_BIT_RS = 0;
	writeByte(data);
}

static void writeData(BYTE data) {
	CM1602_IF_BIT_RS = 1;
	writeByte(data);
}

// From http://elm-chan.org/docs/lcd/hd44780_e.html
/*
 The HD44780 does not have an external reset signal. It has an 
 * integrated power-on reset circuit and can be initialized to the 
 * 8-bit mode by proper power-on condition. However the reset circuit 
 * can not work properly if the supply voltage rises too slowly or fast. 
 * Therefore the state of the host interface can be an unknown state, 
 * 8-bit mode, 4-bit mode or half of 4-bit cycle at program started. 
 * To initialize the HD44780 correctly even if it is unknown state, 
 * the software reset procedure shown in Figure 4 is recommended 
 * prior to initialize the HD44780 to the desired function.*/

static void cm1602_reset(void) {
    // Enable all PORTE as output (display)
    // During tIOL, the I/O ports of the interface (control and data signals) should be kept at ?Low?. see ST7066U datasheet
    CM1602_IF_TRIS_RW = CM1602_IF_TRIS_RS = CM1602_IF_TRIS_EN = 0;
    CM1602_TRIS = CM1602_TRIS & 0xF0;

    CM1602_IF_BIT_RW = CM1602_IF_BIT_RS = CM1602_IF_BIT_EN = 0;
    CM1602_PORT = 0;

    // max 100ms @5v , see ST7066U datasheet
    __delay_ms(100);

    // Push fake go-to-8bit state 3 times
    BYTE cmd = (CMD_FUNCSET | CMD_FUNCSET_DL_8) >> 4;
    pulsePort(cmd); 
    __delay_ms(30);
    pulsePort(cmd);
    __delay_us(100);
    pulsePort(cmd);

    // Now the device is proper reset, and the mode is 8-bit

    cmd = CMD_FUNCSET | CMD_FUNCSET_LN_2 | CMD_FUNCSET_DL_4 | CMD_FUNCSET_FS_7;

    __delay_us(100);

    // Translating to 8-bit (default at power-up) to 4-bit
    // requires a dummy 8-bit command to be given that contains
    // the MSB of the go-to-4bit command
    // After a soft reset, this is not needed again, hence the persistent flag
    pulsePort(cmd >> 4);		// Enables the 4-bit mode

    writeCmd(cmd);

    __delay_ms(100);
}

static void cm1602_clear(void) {
	writeCmd(CMD_CLEAR);
    __delay_ms(2);
}

static void cm1602_home(void) {
	writeCmd(CMD_HOME);
    __delay_ms(2);
}

static void cm1602_setEntryMode(enum CM1602_ENTRYMODE mode) {
	writeCmd(CMD_ENTRY | mode);
    __delay_us(40);
}

static void cm1602_enable(enum CM1602_ENABLE enable) {
	writeCmd(CMD_ENABLE | enable);
    __delay_us(40);
}

static void cm1602_shift(enum CM1602_SHIFT data) {
	writeCmd(CMD_SHIFT | data);
    __delay_us(40);
}

static void cm1602_setCgramAddr(BYTE address) {
	writeCmd(CMD_SETCGRAM | address);
    __delay_us(40);
}

static void cm1602_setDdramAddr(BYTE address) {
	writeCmd(CMD_SETDDRAM | address);
    __delay_us(40);
}

static void cm1602_write(BYTE data) {
	writeData(data);
    __delay_us(40);
}

static void cm1602_writeStr(const char* data) {
	while (*data != 0) {
		cm1602_write(*data);
		data++;
	}
}

// =====
// =====
// =====

void display_init() {
    cm1602_reset();
    cm1602_clear();
    cm1602_setEntryMode(MODE_INCREMENT | MODE_SHIFTOFF);
    cm1602_enable(ENABLE_DISPLAY | ENABLE_CURSOR | ENABLE_CURSORBLINK);
}

void display_logTest(const char* text) {
    cm1602_setDdramAddr(0);
    cm1602_writeStr(text);
}

void display_logStatus(const char* text) {
    cm1602_setDdramAddr(0x40);
    cm1602_writeStr(text);
}