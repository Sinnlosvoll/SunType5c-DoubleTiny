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

typedef struct {
	uint8_t numberOfOpenRequests;
	uint8_t lengths[6];
	uint8_t commands[6];
} async_queue_t;

static keyboard_report_t keyboard_report; // sent to PC
async_queue_t async_queue;
volatile static uchar LED_state = 0xff; // received from PC
static uchar idleRate; // repeat rate for keyboards

int8_t keys_pressed = 0;
uint8_t i = 0;
uint8_t keyHash = 0;

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



// not needed as the report is built by key_{up, down}
void buildReport(uchar send_key) {
	keyboard_report.modifier = 0;
	
	if(send_key >= 'a' && send_key <= 'z')
		keyboard_report.keycode[0] = 4+(send_key-'a');
	else
		keyboard_report.keycode[0] = 0;
}

uint8_t getBitsFromKeyboard(uint8_t length) {
	uint8_t recieved = 0;
	DDRB &= ~(1 << DATPIN); // change data pin over to inputh
	_delay_us(50);

	for (i = 0; i < length; i++	)
	{
		PORTB |= (1 << CLKPIN); // clock high, request bit
		// possible delay here, needs testing if needed
		_delay_us(50); // ah why not
		recieved |= ((PINB &  ( 1<< DATPIN)) >> (DATPIN -1)); // TODO: is this right?
		recieved = (recieved << 1); // reading MSB first
		PORTB &= ~(1 << CLKPIN); // set clock low
		_delay_us(50); // not set in stone
	}

	DDRB |= 1 << DATPIN; // change back to output to keep it consistent
	return recieved;
}

void sendBitsToKeyboard(uint8_t toSend, uint8_t length) {

	for (i = 0; i < length; i++	)
	{
		if (((toSend >> (length - i - 1)) & 0x01))
		{
			PORTB |= (1 << DATPIN); // send from left to right in msb-left notation
		} else {
			PORTB &= ~(1 << DATPIN); // since |= won't pull low
		}
		_delay_us(10);
		PORTB |= (1 << CLKPIN); // clock high
		// possible delay here, needs testing if needed
		_delay_us(50); // that would leave 825 cycles for the kbT to work
		PORTB &= ~(1 << CLKPIN); // set clock low
		_delay_us(50); // courtesy waiting
	}
	PORTB &= ~(1 << DATPIN);
}

usbMsgLen_t usbFunctionWrite(uint8_t * data, uchar len) {
	//ledON();
	if (data[0] == LED_state)
		return 1;
	else
		LED_state = data[0];
	
	// LED state changed
	if(LED_state & CAPS_LOCK) {

		PORTB |= 1 << LEDPIN; // LED on
		async_queue.commands[async_queue.numberOfOpenRequests] = 0b00001001;
		async_queue.lengths[async_queue.numberOfOpenRequests] = 4;
		async_queue.numberOfOpenRequests++;
	}
	else {
		PORTB &= ~(1 << LEDPIN); // LED off
		async_queue.commands[async_queue.numberOfOpenRequests] = 0b00001101;
		async_queue.lengths[async_queue.numberOfOpenRequests] = 4;
		async_queue.numberOfOpenRequests++;
	}
	
	return 1; // Data read, not expecting more
}

uint8_t updateKeysPressed() {

	uint8_t currentHash = 0;
	sendBitsToKeyboard(0b00001111, 4); // request keys pressed
	keys_pressed = getBitsFromKeyboard(4);
	return 0;
	for (i = 0; i < keys_pressed; i++)
	{
		keyboard_report.keycode[i] = getBitsFromKeyboard(8);
	}
	for (i = 0; i < 6; i++)
	{
		currentHash += keyboard_report.keycode[i];
		// this overflows and i know it
	}
	return currentHash;
}

void popSingleAsyncQueueRequest() {
	if (async_queue.numberOfOpenRequests > 0)
	{
		async_queue.numberOfOpenRequests--;
		sendBitsToKeyboard(async_queue.commands[async_queue.numberOfOpenRequests], async_queue.lengths[async_queue.numberOfOpenRequests]);
	}
}


int main() {

	// Master-side pin inits
	
	DDRB |= 1 << DATPIN; // output first, will change over time
	DDRB |= 1 << CLKPIN; // output
	PORTB &= ~(1 << DATPIN); // low as base, external pull-down
	PORTB &= ~(1 << CLKPIN); // low as base, external pull-down 

	// LED
	
	DDRB |= 1 << LEDPIN;
	PORTB &= ~(1 << LEDPIN); // low at first

	
	
	for(i=0; i<sizeof(keyboard_report); i++) // clear report initially
		((uchar *)&keyboard_report)[i] = 0;

	async_queue.numberOfOpenRequests = 0;
	for (i = 0; i < 6; i++)
	{
		async_queue.commands[i] = 0;
		async_queue.lengths[i] = 0;
	}
	
	wdt_enable(WDTO_1S); // enable 1s watchdog timer

	usbInit();
	
	usbDeviceDisconnect(); // enforce re-enumeration
	for(i = 0; i<250; i++) { // wait 500 ms
		wdt_reset(); // keep the watchdog happy
		_delay_ms(2);
	}
	usbDeviceConnect();
	
	// i don't think this is needed
	// TCCR0B |= (1 << CS01); // timer 0 at clk/8 will generate randomness
	// maybe we do
	
	sei(); // Enable interrupts after re-enumeration
	
	while(1) {
		
		wdt_reset(); // keep the watchdog happy

		usbPoll();

		popSingleAsyncQueueRequest(); 

		_delay_ms(2);
		i = updateKeysPressed(); // use i as a tmp variable
		// time that takes: 
		// 		send command to get response: 
		// 						4 * (100 us + 3 shifts + increment variable)
		// 		recieve response:
		// 						(4 + {0..6}) * (53 us + 4 shifts + 1 bit-wise and + 2 pin write + 1 pin read)
		// 		
		// 		should be less than 2 ms or so

		if(usbInterruptIsReady()){

			// keyHash = i;
			// start sending
			usbSetInterrupt((void *)&keyboard_report, sizeof(keyboard_report));
		}
	}
	
	return 0;
}
