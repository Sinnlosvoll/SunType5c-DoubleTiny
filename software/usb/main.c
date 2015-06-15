/**
 * Project: AVR ATtiny USB Tutorial at http://codeandlife.com/
 * Author: Joonas Pihlajamaa, joonas.pihlajamaa@iki.fi
 * Base on V-USB example code by Christian Starkjohann
 * Copyright: (c) 2008 by OBJECTIVE DEVELOPMENT Software GmbH
 * License: GNU GPL v3 (see License.txt)
 */
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/wdt.h>
#include <avr/eeprom.h>
#include <util/delay.h>
#include <math.h>

#include "usbdrv.h"

// ************************
// *** USB HID ROUTINES ***
// ************************

// From Frank Zhao's USB Business Card project
// http://www.frank-zhao.com/cache/usbbusinesscard_details.php
PROGMEM const char usbHidReportDescriptor[USB_CFG_HID_REPORT_DESCRIPTOR_LENGTH] = {
	0x05, 0x01,                    // USAGE_PAGE (Generic Desktop)
	0x09, 0x06,                    // USAGE (Keyboard)
	0xa1, 0x01,                    // COLLECTION (Application)
	0x75, 0x01,                    //   REPORT_SIZE (1)
	0x95, 0x08,                    //   REPORT_COUNT (8)
	0x05, 0x07,                    //   USAGE_PAGE (Keyboard)(Key Codes)
	0x19, 0xe0,                    //   USAGE_MINIMUM (Keyboard LeftControl)(224)
	0x29, 0xe7,                    //   USAGE_MAXIMUM (Keyboard Right GUI)(231)
	0x15, 0x00,                    //   LOGICAL_MINIMUM (0)
	0x25, 0x01,                    //   LOGICAL_MAXIMUM (1)
	0x81, 0x02,                    //   INPUT (Data,Var,Abs) ; Modifier byte
	0x95, 0x01,                    //   REPORT_COUNT (1)
	0x75, 0x08,                    //   REPORT_SIZE (8)
	0x81, 0x03,                    //   INPUT (Cnst,Var,Abs) ; Reserved byte
	0x95, 0x05,                    //   REPORT_COUNT (5)
	0x75, 0x01,                    //   REPORT_SIZE (1)
	0x05, 0x08,                    //   USAGE_PAGE (LEDs)
	0x19, 0x01,                    //   USAGE_MINIMUM (Num Lock)
	0x29, 0x05,                    //   USAGE_MAXIMUM (Kana)
	0x91, 0x02,                    //   OUTPUT (Data,Var,Abs) ; LED report
	0x95, 0x01,                    //   REPORT_COUNT (1)
	0x75, 0x03,                    //   REPORT_SIZE (3)
	0x91, 0x03,                    //   OUTPUT (Cnst,Var,Abs) ; LED report padding
	0x95, 0x06,                    //   REPORT_COUNT (6)
	0x75, 0x08,                    //   REPORT_SIZE (8)
	0x15, 0x00,                    //   LOGICAL_MINIMUM (0)
	0x25, 0x65,                    //   LOGICAL_MAXIMUM (101)
	0x05, 0x07,                    //   USAGE_PAGE (Keyboard)(Key Codes)
	0x19, 0x00,                    //   USAGE_MINIMUM (Reserved (no event indicated))(0)
	0x29, 0x65,                    //   USAGE_MAXIMUM (Keyboard Application)(101)
	0x81, 0x00,                    //   INPUT (Data,Ary,Abs)
	0xc0                           // END_COLLECTION
};

/*USB defs*/
typedef struct {
	uint8_t modifier;
	uint8_t reserved;
	uint8_t keycode[6];
} keyboard_report_t;

static keyboard_report_t keyboard_report; // sent to PC
volatile static uchar LED_state = 0xff; // received from PC
static uchar idleRate; // repeat rate for keyboards


/* defs for counting and such */
typedef struct {
	uint8_t firstByte;
	uint8_t secondByte;
	uint8_t thirdByte;
} byteBuffer_t;

byteBuffer_t byteBuffer;
signed char keys_pressed = 0;
unsigned char i = 0;
unsigned counter = 0;
unsigned char globalStorage = 0;
//  globalStorage & 0x08 show currently reading state
//  globalStorage & 0x01 shows last pin Read
//  globalStorage & 0x02 indicates whether lastPinReadIsSet
//  globalStorage & 0x10 is set when rs232Counters have to be reset
unsigned char currentBitReading = 0;
unsigned char currentBitWriting = 0;


usbMsgLen_t usbFunctionSetup(uchar data[8]) {
	usbRequest_t *rq = (void *)data;

	if((rq->bmRequestType & USBRQ_TYPE_MASK) == USBRQ_TYPE_CLASS) {
		switch(rq->bRequest) {
		case USBRQ_HID_GET_REPORT: // send "no keys pressed" if asked here
			// wValue: ReportType (highbyte), ReportID (lowbyte)
			usbMsgPtr = (void *)&keyboard_report; // we only have this one
			keyboard_report.modifier = 0;
			keyboard_report.keycode[0] = 0;
			return sizeof(keyboard_report);
		case USBRQ_HID_SET_REPORT: // if wLength == 1, should be LED state
			return (rq->wLength.word == 1) ? USB_NO_MSG : 0;
		case USBRQ_HID_GET_IDLE: // send idle rate to PC as required by spec
			usbMsgPtr = &idleRate;
			return 1;
		case USBRQ_HID_SET_IDLE: // save idle rate as required by spec
			idleRate = rq->wValue.bytes[1];
			return 0;
		}
	}
	
	return 0; // by default don't return any data
}

#define CLKPIN PB3
#define DATPIN PB4
#define LEDPIN PB0
 // usb pins are defined in usbconfig.h
#define ledON() (PORTB |= 1 << LEDPIN)
#define ledOFF() (PORTB &= ~(1 << LEDPIN))

#define NUM_LOCK 1
#define CAPS_LOCK 2
#define SCROLL_LOCK 4

usbMsgLen_t usbFunctionWrite(uint8_t * data, uchar len) {
	//ledON();
	if (data[0] == LED_state)
		return 1;
	else
		LED_state = data[0];
	
	// LED state changed
	if(LED_state & CAPS_LOCK)
		PORTB |= 1 << LEDPIN; // LED on
	else
		PORTB &= ~(1 << LEDPIN); // LED off
	
	return 1; // Data read, not expecting more
}

// Now only supports letters 'a' to 'z' and 0 (NULL) to clear buttons
void buildReport(uchar send_key) {
	keyboard_report.modifier = 0;
	
	if(send_key >= 'a' && send_key <= 'z')
		keyboard_report.keycode[0] = 4+(send_key-'a');
	else
		keyboard_report.keycode[0] = 0;
}

void key_down(uchar down_key) {
	/* we only add a key to our pressed list, when we have less than 6 already pressed.
	** additional keys will be ignored  */
	if (keys_pressed < 6)
	{
		keyboard_report.keycode[keys_pressed] = down_key;
		keys_pressed++;
	}
}

void key_up(uchar up_key) {
	uint8_t n;
	for (i = 0; i < 6; i++)
	{
		if (keyboard_report.keycode[i] == up_key)
		{
			for (n = i; n <= 4; n++ )
			{
				keyboard_report.keycode[n] = keyboard_report.keycode[n + 1];
			}
			keyboard_report.keycode[5] = 0;
			keys_pressed--;
			break;
		}
	}
}

unsigned char mapReadByte(toSwitch) {
	switch(toSwitch) {
		case 0x1C: return 0x56; /* keypad -  */
		default: return 0x04; /* keyboard a  */
	}
}

uint8_t getBitsFromKeyboard(uint8_t amount) {
	uint8_t recieved = 0:

	for (i = 0; i < amount; i++	)
	{
		DDRB &= ~(1 << DATPIN); // change data pin over to input
		PORTB |= (1 << CLKPIN); // clock high, request bit
		// possible delay here, needs testing if needed
		recieved |= ((PINB &  ( 1<< DATPIN)) >> (DATPIN -1)); // TODO: is this right?
		recieved = (recieved << 1); // reading MSB first
		PORTB &= ~(1 << CLKPIN); // set clock low
	}

	DDRB |= 1 << DATPIN; // change back to output to keep it consistent
	return recieved;
}

#define STATE_WAIT 0
#define STATE_SEND_KEY 1
#define STATE_RELEASE_KEY 2

int main() {
	unsigned char button_release_counter = 0, state = STATE_WAIT;

	// Master-side pin inits

	DDRB |= 1 << DATPIN; // output first, will change over time
	DDRB |= 1 << CLKPIN; // output
	PORTB &= ~(1 << DATPIN); // low as base, external pull-down
	PORTB &= ~(1 << CLKPIN); // low as base, external pull-down 

	// LED
	
	DDRB |= 1 << LEDPIN;
	PORTB &= ~(1 << LEDPIN); // low first

	
	
	for(i=0; i<sizeof(keyboard_report); i++) // clear report initially
		((uchar *)&keyboard_report)[i] = 0;
	
	wdt_enable(WDTO_1S); // enable 1s watchdog timer

	usbInit();
	
	usbDeviceDisconnect(); // enforce re-enumeration
	for(i = 0; i<250; i++) { // wait 500 ms
		wdt_reset(); // keep the watchdog happy
		_delay_ms(2);
	}
	usbDeviceConnect();
	
	TCCR0B |= (1 << CS01); // timer 0 at clk/8 will generate randomness
	// MCUCR |= (1 << ISC00); // set interrupts at PCINT0 to logical value change
	// GIMSK |= (1 << INT0); // enable the above as interrupt

	//GIMSK |= (1 << PCIE);
	
	sei(); // Enable interrupts after re-enumeration
	
	while(1) {
		
		wdt_reset(); // keep the watchdog happy

		// TIMSK &= (0 << TOIE0);

		// if ((globalStorage & 0x20))
		// {
		// 	do
		// 	{
		// 		_delay_us(30);
		// 		wdt_reset();
		// 	} while ((globalStorage & 0x02));
		// }


		 // _delay_us(30);
		usbPoll();
		_delay_us(30);
		//TIMSK |= (1 << TOIE0); /* enable timer overflow interupts */


		// characters are sent when messageState == STATE_SEND and after receiving
		// the initial LED state from PC (good way to wait until device is recognized)
		// if (0 & usbInterruptIsReady())
		// {
		// 	PCMSK |= (1 << PCINT0);
		// 	globalStorage |= 0x20;
		// }
		if(usbInterruptIsReady() && state != STATE_WAIT && LED_state != 0xff){
			switch(state) {
			case STATE_SEND_KEY:
				buildReport('x');
				state = STATE_RELEASE_KEY; // release next
				break;
			case STATE_RELEASE_KEY:
				buildReport(NULL);
				state = STATE_WAIT; // go back to waiting
				break;
			default:
				state = STATE_WAIT; // should not happen
			}
			
			// start sending
			usbSetInterrupt((void *)&keyboard_report, sizeof(keyboard_report));
		}
	}
	
	return 0;
}

void charDetected(uchar detected) {

	if(currentBitReading == 0 || currentBitReading == 9 || currentBitReading == 18) {
		if ((detected & 0x01))
		{
			/* we should only allow valid frame delimters (1)s to advance our counting */
			currentBitReading++;
			return;       
		} else {
			/* in here we could think of some error handling if we wanted*/
			return;
		}
	}
	// if ((detected & 0x01))
	// {
	// 	 // longBlink(1);
	// } else {
	// 	longBlink(1);
	// }

	if (currentBitWriting >> 4)
	{
		byteBuffer.thirdByte  = (byteBuffer.thirdByte  << 1);
		if ((detected & 0x01))
			byteBuffer.thirdByte  += 0x01;

	} else if (currentBitWriting >> 3) 
	{
		byteBuffer.secondByte = (byteBuffer.secondByte << 1); 
		if ((detected & 0x01))
			byteBuffer.secondByte += 0x01;

	} else {
		byteBuffer.firstByte  = (byteBuffer.firstByte  << 1);
		if ((detected & 0x01))
			byteBuffer.firstByte  += 0x01;
	}

	if (currentBitWriting == 7 && (detected & 0x01))
	{
		ledON();
		key_down( mapReadByte(byteBuffer.firstByte & 0xFE) );
		globalStorage |= 0x10;
		ledOFF();
	}

	if (currentBitWriting == 15 && (detected & 0x01))
	{
		ledON();
		key_up( mapReadByte(byteBuffer.firstByte & 0xFE) );
		globalStorage |= 0x10;
		ledOFF();
	}

	currentBitReading++;
	currentBitWriting++;
	if((globalStorage & 0x10))
		resetRs232Counters();
	
	
}

ISR (PCINT0_vect)
{
	if ((globalStorage  & 0x02))
	{
		globalStorage ^= 0x01; // flip last bit to current value
	} else {
		globalStorage &= ~(0x01); // clear last bit
		globalStorage |= (PINB & (1 << RXDPIN)); // update last read
		globalStorage |= 0x02; //from now on don't read again but toggle and read from register
	}
	if (globalStorage & 0x01)
	{
		globalStorage |= 0x08; // show currently reading state 
		// TIMSK |= (1 << TOIE0);  //enable timer overflow interupts 

	} else {
		// check if byte is done
	}


}

ISR(TIMER0_OVF_vect) 
{
	counter++;
	if ((globalStorage & 0x01) ^ ((globalStorage & 0x04) >> 2) ) // current read is different from last
	{
		counter = floor(counter / 6);
		for (i = 0; i < counter; i++)
		{
			charDetected(~(globalStorage & 0x01));
		}
	}
}