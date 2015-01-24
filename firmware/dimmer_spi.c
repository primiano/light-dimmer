#include <pic.h>
#include <xc.h>
#include <pic16f88.h>
#include <stdint.h>
#include <stdbool.h>

#define ENABLE_WDOG

#pragma config FOSC = INTOSCIO
#ifdef ENABLE_WDOG
#pragma config WDTE = ON
#else
#pragma config WDTE = OFF
#endif
#pragma config MCLRE = OFF
#pragma config LVP = OFF


#define _XTAL_FREQ 4000000

#define LED  PORTAbits.RA2

#define SCR0 PORTBbits.RB3
#define SCR1 PORTAbits.RA4
#define SCR2 PORTAbits.RA3
#define SCR3 PORTBbits.RB0

#define SMOOTHNESS_RATE 50
#define SER_VALUE(X) ((X) ? (((uint16_t) (X + 50)) << 6) : 0)

// 12 ms. should be enough to guarantee no SCR triggers for both 50Hz and 60Hz.
#define CERTAINLY_OFF 12000

uint8_t led_status = 0;

union {
	struct {
        unsigned delay    : 6;
		unsigned index    : 2;
	};
	struct {
		unsigned raw : 8;
	};
} ser_data;

static uint16_t setpoint0;
static uint16_t setpoint1;
static uint16_t setpoint2;
static uint16_t setpoint3;

static uint16_t scr_delay0;
static uint16_t scr_delay1;
static uint16_t scr_delay2;
static uint16_t scr_delay3;



void interrupt int_handler(void) {
    if (PIR1bits.RCIF) {
        if (RCSTAbits.FERR || RCSTAbits.OERR) {
            LED = 1;
            (void) RCREG;
            RCSTAbits.CREN = 0;
            RCSTAbits.CREN = 1;
            ser_data.raw = 0;
        } else {
        	ser_data.raw = RCREG;
            uint16_t value = SER_VALUE(ser_data.delay);
            switch (ser_data.index) {
                case 0: setpoint0 = value; break;
                case 1: setpoint1 = value; break;
                case 2: setpoint2 = value; break;
                case 3: setpoint3 = value; break;
            }
            CLRWDT();
        }
        PIR1bits.RCIF = 0;
    }
}


void main() {
    PORTA = 0;
    PORTB = 0;
    TRISA = 0xE3;  // 1110 0011
    TRISB = 0xf6;  // 1111 0110

    OSCCON = 0x62; // Int. 4Mhz.
#ifdef ENABLE_WDOG
    OPTION_REGbits.PSA = 0; // WDT uses WTDCON prescaler.
    WDTCON = 0x17; //WDT on, prescaler = 65536.
#endif

    T1CONbits.T1RUN = 0;  // System clock is derived from another source.
    T1CONbits.T1CKPS1 = 0;
    T1CONbits.T1CKPS0 = 0;  // 1:1 Pre-scaler.
    T1CONbits.T1OSCEN = 0;
    T1CONbits.TMR1CS = 0;  // Use internal clock (Fosc / 4).
    T1CONbits.TMR1ON = 1;

    // USART
    TXSTA = 0;
    RCSTA = 0x90;
    //SPBRG = 12; // 4800 bps
    SPBRG = 25; // 2400 bps


    ANSEL = 1;
    ADCON0 = 0xc9;  // Frc, Ch1, AD ON.
    //ADCON1 = 0x80;  // Right justified (full ADRESL, leading-zero ADRESH).
    ADCON1 = 0; /////// LEFT JUSTIFIED

    // Setup SPI interrupt.
    PIE1 = 0;
    PIE1bits.RCIE = 1;

    INTCON = 0;
    INTCONbits.PEIE = 1;
    INTCONbits.GIE = 1;

	LED = 0;
    ser_data.raw = 0;

    scr_delay0 = CERTAINLY_OFF;
    scr_delay1 = CERTAINLY_OFF;
    scr_delay2 = CERTAINLY_OFF;
    scr_delay3 = CERTAINLY_OFF;

    setpoint0 = CERTAINLY_OFF;
    setpoint1 = CERTAINLY_OFF;
    setpoint2 = CERTAINLY_OFF;
    setpoint3 = CERTAINLY_OFF;

    for (;;) {
        for (ADCON0bits.GO_DONE = 1; ADCON0bits.GO_DONE; ) {}
        uint8_t adc_value = 0xff - ADRESH;
        if ((TMR1 > 6000 && adc_value < 50) || (TMR1 > 60000)) {
            TMR1 = 0;
            SCR0 = 0;
            SCR1 = 0;
            SCR2 = 0;
            SCR3 = 0;
            led_status++; LED = (led_status < 4) ? 1 : 0;

#define SMOOTH_SETPOINT(X)                                       \
            if (scr_delay##X > setpoint##X  &&                   \
                scr_delay##X - setpoint##X > SMOOTHNESS_RATE) {  \
                scr_delay##X -= SMOOTHNESS_RATE;                 \
            } else if (scr_delay##X < setpoint##X  &&            \
                setpoint##X - scr_delay##X > SMOOTHNESS_RATE) {  \
                scr_delay##X += SMOOTHNESS_RATE;                 \
            } else {                                             \
                scr_delay##X = setpoint##X;                      \
            }

            SMOOTH_SETPOINT(0)
            SMOOTH_SETPOINT(1)
            SMOOTH_SETPOINT(2)
            SMOOTH_SETPOINT(3)
        }

#define FIRE_SCR_IF_EXPIRED(X) \
        if (TMR1 >= scr_delay##X && setpoint##X != SER_VALUE(0x3f)) SCR##X = 1;

        FIRE_SCR_IF_EXPIRED(0)
        FIRE_SCR_IF_EXPIRED(1)
        FIRE_SCR_IF_EXPIRED(2)
        FIRE_SCR_IF_EXPIRED(3)
    }  // for(;;)
}
