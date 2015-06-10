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


#define RXDPIN PB4
#define CLKPIN PB3
#define LEDPIN PB0
#define KRXPIN PB1
#define KTXPIN PB2

#define ledON() (PORTB |= 1 << LEDPIN)
#define ledOFF() (PORTB &= ~(1 << LEDPIN))
#define setRecieve() (DDRB = 0 << RXDPIN)
#define setSend() (DDRB = 1 << RXDPIN)


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

void resetRs232Counters() {

	byteBuffer.firstByte  = 0;
	byteBuffer.secondByte = 0;
	byteBuffer.thirdByte  = 0;
	counter      = 0;
	globalStorage &= ~(0x10);
	currentBitReading = 0;
	currentBitWriting  = 0;
	TIMSK &= (0 << TOIE0);

}

#define STATE_WAIT 0
#define STATE_SEND_KEY 1
#define STATE_RELEASE_KEY 2

int main() {
	unsigned char button_release_counter = 0, state = STATE_WAIT;

	// Master-side

	DDRB = 0 << RXDPIN; // input
	DDRB = 0 << CLKPIN; // input
	PORTB = 0 << RXDPIN; // deactivate pull-up resistor
	PORTB = 0 << CLKPIN; // deactivate pull-up resistor

	// Keyboard-side

	DDRB = 0 << KRXPIN; // input 
	DDRB = 0 << KTXPIN; // input
	PORTB = 0 << KRXPIN; // deactivate pull-up resistor
	PORTB = 0 << KTXPIN; // deactivate pull-up resistor

	for(i=0; i<sizeof(keyboard_report); i++) // clear report initially
		((uchar *)&keyboard_report)[i] = 0;
	
	wdt_enable(WDTO_1S); // enable 1s watchdog timer

	
	TCCR0B |= (1 << CS01); // timer 0 at clk/8 will generate randomness and also serves as a 
	// MCUCR |= (1 << ISC00); // set interrupts at PCINT0 to logical value change
	// GIMSK |= (1 << INT0); // enable the above as interrupt

	GIMSK |= (1 << PCIE); // allow pin change interupts for PCINT[0-5]
	PCMSK |= (1 << PCINT1); // enable pin change interrupt for the KRXPIN
	PCMSK |= (1 << PCINT3); // enable pin change interrupt for the CLKPIN
	
	sei(); // Enable interrupts after re-enumeration
	
	while(1) {
		
		wdt_reset(); // keep the watchdog happy

		// TIMSK &= (0 << TOIE0);

		if ((globalStorage & 0x20))
		{
			do
			{
				_delay_us(30);
				wdt_reset();
			} while ((globalStorage & 0x02));
		}

		TIMSK |= (1 << TOIE0); /* enable timer overflow interupts */


		// characters are sent when messageState == STATE_SEND and after receiving
		// the initial LED state from PC (good way to wait until device is recognized)
		
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
		blinkOn();
		key_down( mapReadByte(byteBuffer.firstByte & 0xFE) );
		globalStorage |= 0x10;
		blinkOFF();
	}

	if (currentBitWriting == 15 && (detected & 0x01))
	{
		blinkOn();
		key_up( mapReadByte(byteBuffer.firstByte & 0xFE) );
		globalStorage |= 0x10;
		blinkOFF();
	}

	currentBitReading++;
	currentBitWriting++;
	if((globalStorage & 0x10))
		resetRs232Counters();
	
	
}

ISR (PCINT1_vect)
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

ISR(PCINT3_vect){
	// interupt handler for a clk signal in PB3 which either means that we recieve a bit or are asked to send one
	// so it is: check if send(1) or recieve(2); (1): push next bit to RXDPIN; (2): read RXDPIN and add to 
	// interpreterbuffer 
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