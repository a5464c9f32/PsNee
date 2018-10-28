#define F_CPU 8000000UL
//#define F_CPU 4166667UL

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/signature.h>
#include <util/delay.h>

FUSES =
{
	.low = 0xE2, //Clock NOT divided by 8 ==> F_CPU = 8MHz
	.high = 0xDD, //BOD=2.7V
	.extended = 0xFF //Default
};

//Region Definition
#define REGION_PAL 0b11101010 //SCEE
#define REGION_US 0b11111010 //SCEA
#define REGION_JP 0b11011010 //SCEI

#define REGION_SELECTED REGION_PAL //Change here according to your region
//End Region Definition

#define SCEData0 0b00000010
#define SCEData2 0b01011101
#define SCEData3 0b01001011
#define SCEData4 0b11001001
#define SCEData5 0b01011001

#define SQCK B,1
#define SUBQ B,0
#define DATA B,2
#define WFCK B,3
#define UNUSED1 B,4
#define UNUSED2 B,5

#define NUM_WFCK_SAMPLES 5000 //number of times WFCK is sampled

// MACROS FOR EASY PIN HANDLING FOR ATMEL GCC-AVR
#define _SET(type,name,bit)          type ## name  |= _BV(bit)
#define _CLEAR(type,name,bit)        type ## name  &= ~ _BV(bit)
#define _TOGGLE(type,name,bit)       type ## name  ^= _BV(bit)
#define _GET(type,name,bit)          ((type ## name >> bit) &  1)
#define _PUT(type,name,bit,value)    type ## name = ( type ## name & ( ~ _BV(bit)) ) | ( ( 1 & (uint8_t)value ) << bit )
#define OUTPUT(pin)		_SET(DDR,pin)
#define INPUT(pin)		_CLEAR(DDR,pin)
#define HIGH(pin)		_SET(PORT,pin)
#define LOW(pin)		_CLEAR(PORT,pin)
#define TOGGLE(pin)		_TOGGLE(PORT,pin)
#define PUT(pin, val)	_PUT(PORT,pin, val)
#define READ(pin)		_GET(PIN,pin)

// Setup() detects which (of 2) injection methods this PSX board requires, then stores it in pu22mode.
uint8_t pu22mode;
uint8_t regionCode[] = {SCEData5, SCEData4, SCEData3, SCEData2, REGION_SELECTED, SCEData0};
	
uint8_t scbuf [12] = { 0 }; // We will be capturing PSX "SUBQ" packets, there are 12 bytes per valid read.
uint16_t timeout_clock_counter = 0;
uint8_t bitbuf = 0;   // SUBQ bit storage
uint8_t bitpos = 0;
uint8_t scpos = 0;           // scbuf position

//Timing    
#define DELAY_US_BETWEEN_BITS 4000 //250 bits/s (microseconds) (ATtiny 8Mhz works from 3950 to 4100)
#define DELAY_MS_BETWEEN_INJECTIONS 90 //72 in oldcrow. PU-22+ work best with 80 to 100 (milliseconds)

// borrowed from AttyNee. Bitmagic to get to the SCEX strings stored in flash (because Harvard architecture)
uint8_t regionCodeBit(uint8_t bitIndex)//, uint8_t regionData)
{
	uint8_t thisByte = regionCode[bitIndex >> 3];	//read current byte
	uint8_t mask = (1 << (bitIndex%8));		//shift 1 by position in byte
	return (0 != (thisByte & mask));			//read bit in current byte and return it
}

void inject_SCEX(void)
{
	for (uint8_t bit_counter = 0; bit_counter < 44; bit_counter++)
	{
		if (regionCodeBit(bit_counter) == 0)
		{
			OUTPUT(DATA); //Set DATA as output
			LOW(DATA); //Set DATA low
			_delay_us(DELAY_US_BETWEEN_BITS);
		}
		else
		{
			if (pu22mode) {
				OUTPUT(DATA); //Set DATA as output and copy WFCK to DATA for DELAY_US_BETWEEN_BITS
				for(uint16_t repeater = 0; repeater <= ((uint16_t)(DELAY_US_BETWEEN_BITS/1.14)); repeater++){
					if(READ(WFCK)){
						HIGH(DATA);
					}else{
						LOW(DATA);
					}
				}
			}
			else { //PU-18 or lower mode
				INPUT(DATA);
				_delay_us(DELAY_US_BETWEEN_BITS);
			}
		}
	}
	OUTPUT(DATA); //Set DATA as output
	LOW(DATA);

	_delay_ms(DELAY_MS_BETWEEN_INJECTIONS);
}

//--------------------------------------------------
//     Setup
//--------------------------------------------------

int main(void)
{
	cli(); //Disable interrupts globally
		//Setting clock to 8 MHz, regardless of CKDIV8 fuse setting
		CLKPR = (1<<CLKPCE); //Enable changing of clock prescaler
		CLKPR = (0<<CLKPS3)|(0<<CLKPS2)|(0<<CLKPS1)|(0<<CLKPS0); //Prescaler 1
	
		//Power reduction setup
		PRR = (1<<PRTIM1)|(1<<PRTIM0)|(1<<PRUSI)|(1<<PRADC); //Disable Timer1, Timer0, USI, ADC
			
		//Analog comparator setup
		ACSR = (1<<ACD); //Disable analog comparator
	sei(); //Enable interrupts globally
	
	
	//DDRB &= ~(63); //All pins inputs
	//PORTB |= (63); //All pins pullups
	INPUT(SQCK);	//make input
	INPUT(SUBQ);	//make input
	INPUT(DATA);	//make input
	INPUT(WFCK);	//make input
	INPUT(UNUSED1);
	HIGH(UNUSED1);
	INPUT(UNUSED2);
	HIGH(UNUSED2);

	// wait for console power on and stable signals
	while (!READ(WFCK));
	while (!READ(SQCK));
	
	// Board detection
	// GATE: __-----------------------  // this is a PU-7 .. PU-20 board!
	// WFCK: __-_-_-_-_-_-_-_-_-_-_-_-  // this is a PU-22 or newer board!
	
	uint8_t lows = 0;
	pu22mode = 0;
	for(uint16_t i = 0; i < NUM_WFCK_SAMPLES; i++) { //Sample up to NUM_WFCK_SAMPLES times
		//if (!READ(PINB,WFCK)){
		if (!READ(WFCK)){
			lows++;
			if(lows > 250){ //If WFCK was low more than 250 times, assume PU-22 and break loop
				pu22mode = 1;
				break;
			}
		}
		_delay_us(198);   // good for 5000 reads in ~1s
	}
	// typical readouts
	// PU-22: highs: 2449 lows: 2377
	//pu22mode = (lows > 100); //pu22mode if lows >100

	while (1){
		scpos = 0;           // scbuf position

		// start with a small delay, which can be necessary in cases where the MCU loops too quickly
		// and picks up the laster SUBQ trailing end
		_delay_ms(1);
	
		//noInterrupts(); // start critical section
		cli();
		start:
		// Capture 8 bits for 12 runs > complete SUBQ transmission
		bitpos = 0;
		for (; bitpos < 8; bitpos++) {
			//while (READ(PINB, SQCK)) {
			while (READ(SQCK)) {
				// wait for clock to go low..
				// a timeout resets the 12 byte stream in case the PSX sends malformatted clock pulses, as happens on bootup
				
				//waiting for, in total, 1000 repetitions
				timeout_clock_counter++;
				if (timeout_clock_counter >= 1000) {
					scpos = 0;  // reset SUBQ packet stream
					timeout_clock_counter = 0;
					bitbuf = 0;
					goto start;
				}
			}

			// wait for clock to go high..
			while (!READ(SQCK));

			bitbuf |= (READ(SUBQ) << bitpos);
			timeout_clock_counter = 0; // no problem with this bit
		}

		// one byte done
		scbuf[scpos] = bitbuf;
		scpos++;
		bitbuf = 0;

		// repeat for all 12 bytes
		if (scpos < 12) {
			goto start;
		}
		//interrupts(); // end critical section
		sei();

		// check if read head is in wobble area
		// We only want to unlock game discs (0x41) and only if the read head is in the outer TOC area.
		// We want to see a TOC sector repeatedly before injecting (helps with timing and marginal lasers).
		// All this logic is because we don't know if the HC-05 is actually processing a getSCEX() command.
		// Hysteresis is used because older drives exhibit more variation in read head positioning.
		// While the laser lens moves to correct for the error, they can pick up a few TOC sectors.
		static uint8_t hysteresis  = 0;
		//uint8_t isDataSector = (((scbuf[0] & 0x40) == 0x40) && (((scbuf[0] & 0x10) == 0) && ((scbuf[0] & 0x80) == 0)));
		uint8_t isDataSector = ((scbuf[0] & 0xD0) == 0x40); //should be the same as line above
	
		//if (
		//(isDataSector &&  !scbuf[1] &&  !scbuf[6]) &&   // [0] = 41 means psx game disk. the other 2 checks are garbage protection
		//(scbuf[2] == 0xA0 || scbuf[2] == 0xA1 || scbuf[2] == 0xA2 ||      // if [2] = A0, A1, A2 ..
		//(scbuf[2] == 0x01 && (scbuf[3] >= 0x98 || scbuf[3] <= 0x02) ) )   // .. or = 01 but then [3] is either > 98 or < 02
		//) {
			//hysteresis++;
		//}
		//else if ( hysteresis > 0 && ((scbuf[0] == 0x01 || isDataSector) && !scbuf[1] &&  !scbuf[6])) {
			//// This CD has the wobble into CD-DA space. (started at 0x41, then went into 0x01)
			//hysteresis++;
		//}
		if (
		(!scbuf[1] &&  !scbuf[6])
		&& 
			(( hysteresis > 0 && (scbuf[0] == 0x01 || isDataSector)) // This CD has the wobble into CD-DA space. (started at 0x41, then went into 0x01)
			||
			(isDataSector && (scbuf[2] == 0xA0 || scbuf[2] == 0xA1 || scbuf[2] == 0xA2 || (scbuf[2] == 0x01 && (scbuf[3] >= 0x98 || scbuf[3] <= 0x02))))) // [0] = 41 means psx game disk. the other 2 checks are garbage protection | if [2] = A0, A1, A2 .. or = 01 but then [3] is either > 98 or < 02
		){
			hysteresis++;
		}
		else if (hysteresis > 0) {
			hysteresis--; // None of the above. Initial detection was noise. Decrease the counter.
		}

		// hysteresis value "optimized" using very worn but working drive on ATmega328 @ 16Mhz
		// should be fine on other MCUs and speeds, as the PSX dictates SUBQ rate
		if (hysteresis >= 14) {
			// If the read head is still here after injection, resending should be quick.
			// Hysteresis naturally goes to 0 otherwise (the read head moved).
			hysteresis = 11;

			OUTPUT(DATA); //Set DATA as output
			LOW(DATA); //Set DATA low
			if (!pu22mode) {
				OUTPUT(WFCK); //Set WFCK as output
				LOW(WFCK); //Set WFCK low
			}

			// HC-05 waits for a bit of silence (pin low) before it begins decoding.
			_delay_ms(DELAY_MS_BETWEEN_INJECTIONS);
			// inject symbols now. 6 runs seems optimal to cover all boards
			for (uint8_t loop_counter = 0; loop_counter < 6; loop_counter++)
			{
				inject_SCEX(); // e = SCEE, a = SCEA, i = SCEI
			}

			if (!pu22mode) {
				INPUT(WFCK); //Set WFCK as input
			}
			INPUT(DATA); //Set DATA as input
		}
		// keep catching SUBQ packets forever
	}
	return 0;
}
