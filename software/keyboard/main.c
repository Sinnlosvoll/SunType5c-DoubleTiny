/**
 * Original-Project: AVR ATtiny USB Tutorial at http://codeandlife.com/
 * Original-Author: Joonas Pihlajamaa, joonas.pihlajamaa@iki.fi
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
unsigned char keyboard_keys_down[6];
unsigned char command = 0;
uint64_t USBSendBuffer = 0;
unsigned char haveToSendBackCounter = 0;
unsigned char bitsReadFromUSB = 0;
uint8_t clkPinState = 0; // kinda broad assumption but delays everywhere and the clock shouldn't start 
 // right away since usb has to be initialised and such


#define RXDPIN PB4
#define CLKPIN PB3
#define LEDPIN PB0
#define KRXPIN PB1
#define KTXPIN PB2

#define ledON() (PORTB |= 1 << LEDPIN)
#define ledOFF() (PORTB &= ~(1 << LEDPIN))
#define setRecieve() (DDRB = 0 << RXDPIN)
#define setSend() (DDRB = 1 << RXDPIN)


void key_down(uint8_t down_key) {
	/* we only add a key to our pressed list, when we have less than 6 already pressed.
	** additional keys will be ignored  */
	if (keys_pressed < 6)
	{
		keyboard_keys_down[keys_pressed] = down_key;
		keys_pressed++;
	}
}

void key_up(uint8_t up_key) {
	uint8_t n;
	for (i = 0; i < 6; i++)
	{
		if (keyboard_keys_down[i] == up_key)
		{
			for (n = i; n <= 4; n++ )
			{
				keyboard_keys_down[n] = keyboard_keys_down[n + 1];
			}
			keyboard_keys_down[5] = 0;
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

void resetRs232Counters() {

	byteBuffer.firstByte  = 0;
	byteBuffer.secondByte = 0;
	byteBuffer.thirdByte  = 0;
	counter               = 0;
	globalStorage         &= ~(0x10);
	currentBitReading     = 0;
	currentBitWriting     = 0;
	TIMSK                 &= ~(1 << TOIE0); 
	// disable timer div 8 interrupt, since we only need them when the keyboard sends stuff

}

void reportKeysDown() {

	uint8_t n;
	setSend();
	haveToSendBackCounter = 4 + (keys_pressed * 8);
	bitsReadFromUSB = 0;
	USBSendBuffer = 0;
	for (i = 3; i >= 0; i--)
	{
		// add the number of keys in the report to be sent out first in 4 bits
		USBSendBuffer |= ((((uint64_t)keys_pressed >> i) & 0x01) << 63);
		USBSendBuffer = (USBSendBuffer >> 1);
	}
	for (i = 0; i < keys_pressed; i++)
	{
		for (n = 0; n < 8; n++)
		{
			// TODO: check if this works
			USBSendBuffer |=  ((((uint64_t)keyboard_keys_down[i] >> n) & 0x01) << 63);
			USBSendBuffer = (USBSendBuffer >> 1);
		}
		
	}
	USBSendBuffer = (USBSendBuffer >> ((63 - 1) - haveToSendBackCounter));
	// probably this amount ( 63-1 due to the last shift in the n-loop), TODO: test this assumption

}

void turnOnCapsLock() {
	// for testing only
	ledON();
}

void turnOffCapsLock() {
	ledOFF();
}

void checkCommand() {
	switch (command) {
		case 0b00001111: reportKeysDown(); break;
		case 0b00001001: turnOnCapsLock(); break;
		case 0b00001101: turnOffCapsLock(); break;
		default: ledON(); break; // maybe blink led somehow here
	}
}

int main() {
	
	// Master-side pin inits
	DDRB |= 0 << RXDPIN; // input
	DDRB |= 0 << CLKPIN; // input
	PORTB |= 0 << RXDPIN; // deactivate pull-up resistor
	PORTB |= 0 << CLKPIN; // deactivate pull-up resistor

	// Keyboard-side pin inits
	DDRB |= 0 << KRXPIN; // input 
	DDRB |= 1 << KTXPIN; // output
	PORTB |= 0 << KRXPIN; // deactivate pull-up resistor
	PORTB |= 0 << KTXPIN; // set low by default, so no weird stuff happens

	// LED
	DDRB |= 1 << LEDPIN;
	PORTB |= 0 << LEDPIN;
	
	TCCR0B |= (1 << CS01); // timer 0 at clk/8 will generate randomness and also serves as a 
	GIMSK |= (1 << PCIE); // allow pin change interupts for PCINT[0-5]
	PCMSK = (1 << PCINT1); // enable pin change interrupt for the KRXPIN
	PCMSK |= (1 << PCINT3); // enable pin change interrupt for the CLKPIN
	

	for(i=0; i<sizeof(keyboard_keys_down); i++) // clear report initially
		keyboard_keys_down[i] = 0;
	
	wdt_enable(WDTO_1S); // enable 1s watchdog timer
	sei(); // Enable interrupts after re-enumeration
	
	while(1) {
		
		wdt_reset(); // keep the watchdog happy
		if ((bitsReadFromUSB & 0x04))
		{
			// 4 bits read, therefore command is done
			checkCommand();
		}

		// TIMSK &= (0 << TOIE0);

		if ((globalStorage & 0x08)) // currently reading
		{
			do
			{
				_delay_us(200);
				wdt_reset();
			} while ((globalStorage & 0x08));
		}

		// TIMSK |= (1 << TOIE0); /* enable timer overflow interupts */
	}
	
	return 0;
}

void charDetected(uint8_t detected) {

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










void pinChange3() {
	// usbT side

	// interupt handler for a clk signal in PB3 which either means that we recieve a bit or are asked to send one
	// so it is: check if send(1) or recieve(2); (1): push next bit to RXDPIN; (2): read RXDPIN and add to 
	// interpreterbuffer 
	// 
	if (PINB << CLKPIN)
	{
		// only when changed to high

		if (haveToSendBackCounter != 0)
		{
			// send the next byte out of RXDPIN
			if (USBSendBuffer & 0x01)
			{
				PORTB |= (1 << RXDPIN);
			} else {
				PORTB &= ~(1 << RXDPIN);
			}
			USBSendBuffer = (USBSendBuffer >> 1);
			haveToSendBackCounter--;
			if (haveToSendBackCounter == 0)
			{
				// set Pin back as input so we can recieve new messages
				// as we are finished woth sending all bits
				setRecieve();
			}

		} else {
			// when there is a clock signal and we have nothing left in our queue
			// we'll hopefully recieve a new command
			command = (command << 1);
			command |= (PINB << RXDPIN);
			bitsReadFromUSB++;
		}
	}
}

void pinChange1() {
	// Keyboard site
	// each 
	
	if ((globalStorage  & 0x02)) // lastPinRead is set, so to save time reading the pin (again(interrupt from change)) we just toggle the 'current' read value
	{
		globalStorage ^= 0x01; // flip last bit to current value

	} else {
		globalStorage &= ~(0x01); // clear last bit
		globalStorage |= (PINB & (1 << RXDPIN)); // set last read
		globalStorage |= 0x02; //from now on don't read again but toggle and read from register
	}
	if (globalStorage & 0x01) // last bit read is 1
	{
		// this is only done once per byte sent by the keyboard
		globalStorage |= 0x08; // show currently reading state 
		TIMSK |= (1 << TOIE0);  //enable timer overflow interupts 

	} else {
		// check if byte is done
		// or not, since the interpreter does that already
	}


}


ISR(PCINT0_vect) {

	if ((PINB & (1 << CLKPIN)) != clkPinState) // clock changed
	{
		if (clkPinState)
		{
			ledON();
		} else {
			ledOFF();
		}
		pinChange3();
		clkPinState ^= 0x01; // flip last state to current one

	} else {
		pinChange1(); 
		// we only have 2 possible interrupts so this should work fine
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
			// the counter increments about 6 times per bit sent by the kb
			// so when it changes there were floor(counter/6) bits of the ~(now) state
		}
	}
}