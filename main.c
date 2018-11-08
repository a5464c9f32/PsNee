#define F_CPU 4166667UL

#include <avr/signature.h>
#include <avr/fuse.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/wdt.h>
#include <util/delay.h>

FUSES =
{
	.low = 0xE0,		//External clock, slow start-up, clock NOT divided
	.high = 0xCD,		//BOD=2.7V, Watchdog always on //value=0x4D for BOD=2.7V, Watchdog always on AND RESET DISABLE
	.extended = 0xFF,	//Default, Self Programming Disabled
};

//Region Definition
#define REGION_EU	0b11101010		//SCEE - EU
#define REGION_US	0b11111010		//SCEA - US
#define REGION_JP	0b11011010		//SCEI - JP
#define SCEData0	0b00000010
#define SCEData2	0b01011101
#define SCEData3	0b01001011
#define SCEData4	0b11001001
#define SCEData5	0b01011001

#define REGION_SELECTED REGION_EU	//Change here according to your region

//regionCode array stores data that is injected to fake an original disk
uint8_t regionCode[] = {SCEData5, SCEData4, SCEData3, SCEData2, REGION_SELECTED, SCEData0};

//Pin Definition
#define DATA	0
#define SUBQ	1
#define SQCK	2
#define CLKI	3
#define WFCK	4
#define RESET	5

//Macros for port manipulation
#define SBI(port,bit) 	asm("sbi %0, %1" : : "I" (_SFR_IO_ADDR(port)), "I" (bit))
#define CBI(port,bit) 	asm("cbi %0, %1" : : "I" (_SFR_IO_ADDR(port)), "I" (bit))

#define OUTPUT(bit)		SBI(DDRB,bit)
#define INPUT(bit)		CBI(DDRB,bit)
#define HIGH(bit)		SBI(PORTB,bit)
#define LOW(bit)		CBI(PORTB,bit)
#define READ(bit)		((PINB >> bit)&1)

//Timing
#define MICROSECONDS_TO_TIMER_TICKS(MICROS) ((MICROS*(F_CPU/256))/1000000)
#define US_BETWEEN_BITS 4000						//Time for each bit that is injected
#define US_BETWEEN_INJECTIONS 90000					//72ms in oldcrow. PU-22+ work best with 80 to 100ms
#define US_SUBQ_TIMEOUT 320							//Inactivity time period after which packet counter is reset
#define NUMBER_OF_INJECTIONS 3						//If we start to inject, how often?
#define NUMBER_OF_BYTES_IN_PACKET 12				//How many bytes make up a packet?

uint8_t pu22mode = 0;								//Variable to keep track of if board is either PU-22 or older revision

uint8_t scbuf[NUMBER_OF_BYTES_IN_PACKET] = { 0 };	//Will be storing SUBQ packets
uint8_t bitbuf[8] = { 0 };							//SUBQ bit storage
uint8_t scpos = 0;									//scbuf position
uint8_t hysteresis = 0;								//Hysteresis counter for injecting region codes

//--------------------------------------------------
//     Returns the bit inside "regionCode"
//     at position "bitIndex", across bytes
//--------------------------------------------------

uint8_t regionCodeBit(uint8_t bitIndex)
{
	uint8_t thisByte = regionCode[bitIndex >> 3];	//bitIndex divided by 8 returns the byte in which bitIndex is located
	uint8_t mask = (1 << (bitIndex%8));				//bitIndex modulo 8 returns the location of bitIndex inside the current byte
	return (0 != (thisByte & mask));				//Read only the bit selected by bitIndex and return a "boolean"
}

//--------------------------------------------------
//     Function to inject SCEX data
//--------------------------------------------------

void inject_SCEX(void)
{
	for (uint8_t bit_counter = 0; bit_counter < 44; bit_counter++)
	{
		TCNT0 = 0;			//Reset counter to measure time
		while(TCNT0 <= MICROSECONDS_TO_TIMER_TICKS(US_BETWEEN_BITS)){
			if (!regionCodeBit(bit_counter)){
				OUTPUT(DATA); //Set DATA as output
				LOW(DATA); //Set DATA low
			}else{
				if (pu22mode){
					OUTPUT(DATA); //Set DATA as output and copy WFCK to DATA
					if(READ(WFCK)){
						HIGH(DATA);
					}else{
						LOW(DATA);
					}
				}else{ //PU-18 or lower mode
					INPUT(DATA);
				}
			}
		}
	}
	OUTPUT(DATA); //Set DATA as output
	LOW(DATA);
}

//--------------------------------------------------
//     Main Function
//--------------------------------------------------

int main(void)
{
	cli(); //Disable interrupts globally
	
	//Clock speed is defined by CLKI

	//Watchdog setup
	wdt_enable(WDTO_2S);	//Timout=2s, Action=reset
	
	//Power reduction setup
	PRR = (1<<PRTIM1)|(1<<PRUSI)|(1<<PRADC);	//Disable Timer1, USI, ADC
	ACSR = (1<<ACD);							//Disable analog comparator
	
	//Timer0 setup
	//"Normal"-Mode counter without interrupts
	TCCR0A = 0;				//Clear registers
	TCCR0B = (1 << CS02);	//Prescaler 256, one timer tick every 256 clock cycles, clear other registers
	TCNT0  = 0;				//Count = 0

	sei(); //Enable interrupts globally

	//Port setup, redefining inputs just to be sure
	INPUT(SQCK);	//make input
	INPUT(SUBQ);	//make input
	INPUT(DATA);	//make input
	INPUT(WFCK);	//make input
	INPUT(RESET);	//make input
	HIGH(RESET);	//enable pullup

	//Wait for console power on and stable signals
	while (!READ(SQCK));	//Is normally pulled high after WFCK, so wait for that to enlarge bootup delay
	while (!READ(WFCK));	//Either high if board <PU-20 or oscillating if board >=PU-20

	// Board detection
	// WFCK: __-----------------------  // this is a PU-7 .. PU-20 board and WFCK is GATE!
	// WFCK: __-_-_-_-_-_-_-_-_-_-_-_-  // this is a PU-22 or newer board!
	uint16_t lows = 0;
	uint16_t highs = 0;
	while(highs < (F_CPU/320)) {		//If WFCK was high a lot, assume not PU-22
		if (READ(WFCK)){
			highs++;					//Increment high counter
			pu22mode = 0;
		}else{
			lows++;						//Increment low counter
			if(highs > 0){
				highs--;				//Just to be sure that a low weighs more than a high, decrement high counter
			}
			if(lows >= (F_CPU/3200)){	//If WFCK was low often enough, assume PU-22 and break loop
				pu22mode = 1;
				break;
			}
		}
	}
	//At this point, pu22mode is 1 for a PU-22 board and 0 otherwise
	
	//Main loop
	while(1){
		
		start:
		wdt_reset();	//Reset Watchdog, to show that we are still alive
		scpos = 0;		//reset SUBQ packet position
		//Capture bytes without larger gap in between ==> complete SUBQ transmission
		while(scpos < NUMBER_OF_BYTES_IN_PACKET){
			for (uint8_t bitpos = 0; bitpos < 8; bitpos++) {
				
				TCNT0 = 0;
				while (READ(SQCK)) {	//Wait for clock to go low
					//Timeout resets capture buring bootup and in between packages
					if (TCNT0 >= MICROSECONDS_TO_TIMER_TICKS(US_SUBQ_TIMEOUT)){
						goto start;
					}
				}
				while (!READ(SQCK));	//Wait for clock to go high
				
				bitbuf[bitpos] = READ(SUBQ);
			}
			scbuf[scpos] = bitbuf[0];
			for (uint8_t bitpos = 1; bitpos < 8; bitpos++) {
				scbuf[scpos] |= (bitbuf[bitpos] << bitpos);
			}
			//byte done
			scpos++;
		}
		
		// check if read head is in wobble area
		// We only want to unlock game discs (0x41) and only if the read head is in the outer TOC area.
		// We want to see a TOC sector repeatedly before injecting (helps with timing and marginal lasers).
		// All this logic is because we don't know if the HC-05 is actually processing a getSCEX() command.
		// Hysteresis is used because older drives exhibit more variation in read head positioning.
		// While the laser lens moves to correct for the error, they can pick up a few TOC sectors.
		uint8_t isDataSector = ((scbuf[0] & 0xD0) == 0x40);		//Indicating if CD is reading in data-sector

		if (
		(!scbuf[1] &&  !scbuf[6])
		&&
		(( hysteresis > 0 && (scbuf[0] == 0x01 || isDataSector))//Wobble area(started at 0x41, then went into 0x01)
		||
		(isDataSector && ((scbuf[2] >= 0xA0 && scbuf[2] <= 0xA2) || (scbuf[2] == 0x01 && (scbuf[3] >= 0x98 || scbuf[3] <= 0x02))))) // [0] = 41 means psx game disk. the other 2 checks are garbage protection | if [2] = A0, A1, A2 .. or = 01 but then [3] is either > 98 or < 02
		){
			hysteresis++;
		}
		else if (hysteresis > 0) {
			hysteresis--; //Not a valid packet. Decrease the counter.
		}

		// hysteresis value "optimized" using very worn but working drive on ATmega328 @ 16Mhz
		// should be fine on other MCUs and speeds, as the PSX dictates SUBQ rate
		if(hysteresis >= 14){
			// If the read head is still here after injection, resending should be quick.
			// Hysteresis naturally goes to 0 otherwise (the read head moved).
			hysteresis = 11;

			OUTPUT(DATA);		//Set DATA as output
			LOW(DATA);			//Set DATA low
			
			if(!pu22mode){
				OUTPUT(WFCK);	//Set WFCK as output
				LOW(WFCK);		//Set WFCK low
			}

			// inject symbols now
			for(uint8_t injection_counter = 0; injection_counter < NUMBER_OF_INJECTIONS; injection_counter++)
			{
				//wdt_reset();						//Reset Watchdog, to show that we are still alive
				inject_SCEX();						//Injection
				_delay_us(US_BETWEEN_INJECTIONS);	//Delay between injections
			}

			if(!pu22mode){
				INPUT(WFCK); 	//Set WFCK as input
			}

			INPUT(DATA); 		//Set DATA as input
		}else{
			//small delay, which can be necessary in case the MCU loops too quickly
			//and picks up SUBQ trailing end
			//INVESTIGATE!
			_delay_us(1000);
		}
	}
	return 0;
}