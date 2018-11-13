#define F_CPU 8000000UL

#include <avr/signature.h>
#include <avr/fuse.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/wdt.h>
#include <util/delay.h>

FUSES =
{
	.low = 0xE2,		//Internal 8MHz clock, slow start-up, clock NOT divided
	.high = 0xCD,		//BOD=2.7V, Watchdog always on AND value=0xED for RESET DISABLE and SERIAL PROGRAM DOWNLOAD
	.extended = 0xFF,	//Default
};

//Region Definition
#define REGION_EU 0
#define REGION_US 1
#define REGION_JP 2

#define REGION_SELECTED REGION_EU	//Change here according to your region

#if REGION_SELECTED == REGION_EU
	#define REGION_BIT_0	0
	#define REGION_BIT_1	1
#elif REGION_SELECTED == REGION_US
	#define REGION_BIT_0	1
	#define REGION_BIT_1	1
#elif REGION_SELECTED == REGION_JP
	#define REGION_BIT_0	1
	#define REGION_BIT_1	0
#endif

//regionCode array stores data that is injected to fake an original disk
uint8_t regionCodeArray[44] = {
	1,0,0,1,1,0,1,0,
	1,0,0,1,0,0,1,1,
	1,1,0,1,0,0,1,0,
	1,0,1,1,1,0,1,0,
	0,1,0,1,REGION_BIT_0,REGION_BIT_1,1,1,
	0,1,0,0
};

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
#define MICROSECONDS_TO_TIMER_TICKS(MICROS)	((MICROS*(F_CPU/256))/1000000)
#define US_BETWEEN_BITS 4000								//Time for each bit that is injected
#define US_SUBQ_TIMEOUT 320									//Inactivity time period after which packet counter is reset
#define US_BETWEEN_INJECTIONS 90000							//72ms in oldcrow. PU-22+ work best with 80 to 100ms
#define NUMBER_OF_INJECTIONS 3								//If we start to inject, how often?
#define NUMBER_OF_BYTES_IN_PACKET 12						//How many bytes make up a packet?
#define NUMBER_OF_LOW_SAMPLES F_CPU/3200					//How often to sample the high state of WFCK before stopping
#define NUMBER_OF_HIGH_SAMPLES (NUMBER_OF_LOW_SAMPLES*10)	//How often to sample the high state of WFCK before stopping
#define HYSTERESIS_THRESHOLD 14								//How often do we have to parse a correct packet before starting the first injection

#if MICROSECONDS_TO_TIMER_TICKS(US_BETWEEN_BITS) > 255
	#error Value of US_BETWEEN_BITS too large
#endif
#if MICROSECONDS_TO_TIMER_TICKS(US_SUBQ_TIMEOUT) > 255
	#error Value of US_SUBQ_TIMEOUT too large
#endif
#if NUMBER_OF_LOW_SAMPLES > 65535
	#error Value of NUMBER_OF_LOW_SAMPLES too large
#endif
#if NUMBER_OF_HIGH_SAMPLES > 65535
	#error Value of NUMBER_OF_HIGH_SAMPLES too large
#endif

uint8_t pu22mode = 0;								//Variable to keep track of if board is either PU-22 or older revision
uint8_t scbuf[NUMBER_OF_BYTES_IN_PACKET] = { 0 };	//Will be storing SUBQ packets
uint8_t bitbuf[8] = { 0 };							//SUBQ bit storage
uint8_t scpos = 0;									//scbuf position
uint8_t hysteresis = 0;								//Hysteresis counter for injecting region codes

//--------------------------------------------------
//     Function to inject SCEX data
//--------------------------------------------------
void inject_SCEX(void)
{
	for (uint8_t bit_counter = 0; bit_counter < 44; bit_counter++)
	{
		TCNT0 = 0;												//Reset counter to measure time
		while(TCNT0 <= MICROSECONDS_TO_TIMER_TICKS(US_BETWEEN_BITS))
		{
			if (regionCodeArray[bit_counter])
			{
				if (pu22mode)
				{
					OUTPUT(DATA);								//Set DATA as output and copy WFCK state onto DATA line
					if(READ(WFCK))
					{
						HIGH(DATA);
					}
					else
					{
						LOW(DATA);
					}
				}
				else
				{												//PU-18 or lower mode
					INPUT(DATA);								//Set DATA as input
				}
			}
			else
			{
				OUTPUT(DATA);									//Set DATA as output
				LOW(DATA);										//Set DATA low
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
	cli();	//Disable interrupts globally
	
	//Clock setup, not needed if DIV8 fuse is (correctly) disabled
	CLKPR = (1<<CLKPCE);										//Enable changing of clock prescaler
	CLKPR = (0<<CLKPS3)|(0<<CLKPS2)|(0<<CLKPS1)|(0<<CLKPS0);	//Prescaler 1 =>8MHz

	//Watchdog setup
	wdt_enable(WDTO_2S);										//Timout=2s, Action=reset
	
	//Power reduction setup
	PRR = (1<<PRTIM1)|(1<<PRUSI)|(1<<PRADC);					//Disable Timer1, USI, ADC
	ACSR = (1<<ACD);											//Disable analog comparator
	
	//Timer0 setup
																//"Normal"-Mode counter without interrupts
	TCCR0A = 0;													//Clear registers
	TCCR0B = (1 << CS02);										//Prescaler 256, one timer tick every 256 clock cycles, clear other registers
	TCNT0  = 0;													//Count = 0

	sei();	//Enable interrupts globally

	//Port setup, redefining inputs just to be sure
	INPUT(SQCK);	//make input
	INPUT(SUBQ);	//make input
	INPUT(DATA);	//make input
	INPUT(WFCK);	//make input
	INPUT(CLKI);	//make input
	HIGH(CLKI);		//enable pullup
	INPUT(RESET);	//make input
	HIGH(RESET);	//enable pullup

	//Wait for console power on and stable signals
	while (!READ(SQCK));	//Is normally pulled high after WFCK, so wait for that to enlarge bootup delay
	while (!READ(WFCK));	//Either high if board <PU-20 or oscillating if board >=PU-20

	//Board revision detection
	//WFCK: __-----------------------  //This is a PU-7 .. PU-20 board and WFCK is GATE!
	//WFCK: __-_-_-_-_-_-_-_-_-_-_-_-  //This is a PU-22 or newer board!
	uint16_t lows = 0;
	uint16_t highs = 0;
	while(highs < NUMBER_OF_HIGH_SAMPLES)		//If WFCK was high a lot, assume not PU-22
	{
		if (READ(WFCK))							//If WFCK is high
		{
			highs++;							//Increment high counter
		}
		else									//If WFCK is high
		{
			lows++;								//Increment low counter
			if(lows >= NUMBER_OF_LOW_SAMPLES)	//If WFCK was low often enough, assume PU-22 and break loop
			{
				pu22mode = 1;
				break;
			}
		}
	}
	//At this point, pu22mode is 1 for a >=PU-22 board and 0 otherwise
	
	//Main loop
	while(1)
	{
		//--------------------------------------------------
		//     SUBQ capture
		//--------------------------------------------------
		start:																		//(Re-)Start of SUBQ packet capture
		wdt_reset();																//Reset Watchdog, to show that we are still alive
		scpos = 0;																	//reset SUBQ packet position
		while(scpos < NUMBER_OF_BYTES_IN_PACKET)									//Capture NUMBER_OF_BYTES_IN_PACKET bytes (without larger gap in between)
		{
			for (uint8_t bitpos = 0; bitpos < 8; bitpos++)							//Each byte consists of 8 bits
			{	
				TCNT0 = 0;															//Reset Timeout
				while (READ(SQCK))													//Wait for clock to go low
				{
					if (TCNT0 >= MICROSECONDS_TO_TIMER_TICKS(US_SUBQ_TIMEOUT))		//Timeout resets capture during boot-up and in between packets
					{
						goto start;													//If timeout happens because SQCK was high for US_SUBQ_TIMEOUT, restart from scratch
					}
				}
				while (!READ(SQCK));												//Wait for clock to go high
				bitbuf[bitpos] = READ(SUBQ);										//Read SUBQ state into array for each bit in current byte
			}
																					//8 bits read, now combining them into byte storage		
			scbuf[scpos] = bitbuf[0];												//Copy bit 0, not OR'ed to also reinitialize scbuf[scpos] 
			for (uint8_t bitpos = 1; bitpos < 8; bitpos++)							//Now OR'ing in the remaining 7 bits from the current byte into scbuf[scpos]
			{
				scbuf[scpos] |= (bitbuf[bitpos] << bitpos);
			}
			scpos++;																//Increment scpos for next byte
		}
		
		//--------------------------------------------------
		//     Checking last SUBQ capture for content
		//--------------------------------------------------
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


		//--------------------------------------------------
		//     Injection of SCEX code
		//--------------------------------------------------
		// hysteresis value "optimized" using very worn but working drive on ATmega328 @ 16Mhz
		// should be fine on other MCUs and speeds, as the PSX dictates SUBQ rate
		if(hysteresis >= HYSTERESIS_THRESHOLD)
		{
			// If the read head is still here after injection, resending should be quick.
			// Hysteresis naturally goes to 0 otherwise (the read head moved).
			hysteresis = HYSTERESIS_THRESHOLD-3;

			OUTPUT(DATA);							//Set DATA as output
			LOW(DATA);								//Set DATA low
			
			if(!pu22mode)							//If board revision <PU-22
			{
				OUTPUT(WFCK);						//Set WFCK as output
				LOW(WFCK);							//Set WFCK low
			}

			for(uint8_t injection_counter = 0; injection_counter < NUMBER_OF_INJECTIONS; injection_counter++)
			{										// inject symbols now
				inject_SCEX();						//Injection
				_delay_us(US_BETWEEN_INJECTIONS);	//Delay between injections
			}

			if(!pu22mode)							//If board revision <PU-22
			{
				INPUT(WFCK);						//Set WFCK as input
			}

			INPUT(DATA); 							//Set DATA as input
		}
		else
		{
			_delay_us(1000);	//small delay, which can be necessary in case the MCU loops too quickly and picks up SUBQ trailing end
		}
	}
	return 0;
}