#define F_CPU 8000000UL

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/signature.h>
#include <util/delay.h>

FUSES =
{
	.low = 0xE2, //Clock NOT divided by 8 ==> 8MHz
	.high = 0xDD, //BOD=2.7V
	.extended = 0xFF //Default
};

//Region Definition
#define REGION_PAL 0b11101010 //SCEE
#define REGION_US 0b11111010 //SCEA
#define REGION_JP 0b11011010 //SCEI

#define REGION_SELECTED REGION_PAL //Change here according to your Playstation's region
//End Region Definition

#define SCEData0 0b00000010
#define SCEData2 0b01011101
#define SCEData3 0b01001011
#define SCEData4 0b11001001
#define SCEData5 0b01011001

//#define SCEEData1 0b11101010
//#define SCEAData1 0b11111010
//#define SCEIData1 0b11011010

//Pin definitions
#define SQCK DDB0
#define SUBQ DDB1
#define DATA DDB2
#define WFCK DDB4

#define NUM_WFCK_SAMPLES 5000 //number of times WFCK is sampled

#define SET(x,y) x |= (1 << y)
#define CLEAR(x,y) x &= ~(1<< y)
#define READ(x,y) ((0u == (x & (1<<y)))?0u:1u)

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
uint8_t regionCodeBit(uint8_t index)//, uint8_t regionData)
{
	//regionCode[1] = regionData;
	uint8_t bits = regionCode[index >> 3];	//read current byte
	uint8_t mask = (1 << (index%8));		//shift 1 by position in byte
	return (0 != (bits & mask));			//read bit in current byte and return it
}

void inject_SCEX(void)
{
	// pinMode(data, OUTPUT) is used more than it has to be but that's fine.
	for (uint8_t bit_counter = 0; bit_counter < 44; bit_counter++)
	{
		if (regionCodeBit(bit_counter) == 0)//, region == 'e' ? SCEEData1 : region == 'a' ? SCEAData1 : SCEIData1) == 0)
		{
			//pinMode(data, OUTPUT);
			//DDRB |= (1<<data); //Set data as output
			SET(DDRB,DATA);
			CLEAR(PORTB, DATA); // data low
		}
		else
		{
			if (pu22mode) {
				//pinMode(data, OUTPUT);
				//DDRB |= (1<<data); //Set data as output
				SET(DDRB,DATA);
				//uint32_t now = micros();
				//uint8_t wfck_sample = READ(PINB, WFCK);
				//bitWrite(DATAPORT, DATABIT, wfck_sample); // output wfck signal on data pin
				if(READ(PINB, WFCK)){
					SET(PORTB, DATA);
				}else{
					CLEAR(PORTB, DATA);
				}

				//while ((micros() - now) < delay_between_bits);
				
			}
			else { // PU-18 or lower mode
				//pinMode(data, INPUT);
				CLEAR(DDRB,DATA);
			}
		}
		_delay_us(DELAY_US_BETWEEN_BITS);
	}

	//pinMode(data, OUTPUT);
	//DDRB |= (1<<data); //Set data as output
	SET(DDRB,DATA);
	CLEAR(PORTB, DATA); // pull data low
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
		CLKPR = (0<<CLKPS3)|(0<<CLKPS2)|(0<<CLKPS1)|(0<<CLKPS0); //Prescaler 1 ==> 8 MHz
	
		//Power reduction setup
		PRR = (1<<PRTIM1)|(1<<PRTIM0)|(1<<PRUSI)|(1<<PRADC); //Disable Timer1, Timer0, USI, ADC
			
		//Analog comparator setup
		ACSR = (1<<ACD); //Disable analog comparator
	sei(); //Enable interrupts globally
	
	//pinMode(data, INPUT);
	//CLEAR(DDRB,DATA);
	//pinMode(gate_wfck, INPUT);
	//CLEAR(DDRB,WFCK);
	//pinMode(subq, INPUT); // PSX subchannel bits
	//CLEAR(DDRB,SUBQ);
	//pinMode(sqck, INPUT); // PSX subchannel clock
	//CLEAR(DDRB,SQCK);

	// wait for console power on and stable signals
	while (!READ(PINB,SQCK));
	while (!READ(PINB,WFCK));
	//while (!READ(PINB,WFCK));	
	//while (!digitalRead(gate_wfck));
	
	// Board detection
	//
	// GATE: __-----------------------  // this is a PU-7 .. PU-20 board!
	//
	// WFCK: __-_-_-_-_-_-_-_-_-_-_-_-  // this is a PU-22 or newer board!
	
	uint8_t lows = 0;
	for(uint16_t i = 0; i < NUM_WFCK_SAMPLES; i++) {
		if (!READ(PINB,WFCK)){
			lows++;
			if(lows > 200){
				lows = 200;
			}
		}
		_delay_us(199);   // good for 5000 reads in ~1s
	}
	// typical readouts
	// PU-22: highs: 2449 lows: 2377
	pu22mode = (lows > 100); //pu22mode if lows >100

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
			while (READ(PINB, SQCK)) {
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
			while (!READ(PINB, SQCK));

			//sample = READ(PINB, SUBQ);
			bitbuf |= (READ(PINB, SUBQ) << bitpos);

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
		uint8_t isDataSector = ((scbuf[0] & 0xD0) == 0x40);
	
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

			//pinMode(data, OUTPUT);
			//DDRB |= (1<<data); //Set data as output
			SET(DDRB,DATA);
			//digitalWrite(data, 0); // pull data low
			CLEAR(PORTB, DATA);
			if (!pu22mode) {
				//pinMode(gate_wfck, OUTPUT);
				SET(DDRB,WFCK);
				//digitalWrite(gate_wfck, 0);
				CLEAR(PORTB, WFCK);
			}

			// HC-05 waits for a bit of silence (pin low) before it begins decoding.
			_delay_ms(DELAY_MS_BETWEEN_INJECTIONS);
			// inject symbols now. 6 runs seems optimal to cover all boards
			for (uint8_t loop_counter = 0; loop_counter < 6; loop_counter++)
			{
				inject_SCEX(); // e = SCEE, a = SCEA, i = SCEI
			}

			if (!pu22mode) {
				//pinMode(gate_wfck, INPUT);
				CLEAR(DDRB,WFCK);
			}
			//pinMode(data, INPUT); // high-z the line, we're done
			CLEAR(DDRB,DATA);
		}
		// keep catching SUBQ packets forever
	}
	return 0;
}
