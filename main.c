#define F_CPU 8000000UL

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/signature.h>
#include <avr/fuse.h>
#include <util/delay.h>
#include <stdbool.h>

FUSES =
{
	.low = 0xE2,		//Clock NOT divided by 8 ==> F_CPU = 8MHz
	.high = 0xDD,		//BOD=2.7V
	.extended = 0xFF	//Default
};

//Region Definition
#define REGION_PAL	0b11101010	//SCEE - EU
#define REGION_US	0b11111010	//SCEA - US
#define REGION_JP	0b11011010	//SCEI - JP
#define SCEData0	0b00000010
#define SCEData2	0b01011101
#define SCEData3	0b01001011
#define SCEData4	0b11001001
#define SCEData5	0b01011001

#define REGION_SELECTED REGION_PAL //Change here according to your region

//regionCode array stores data that is injected to fake an original disk
uint8_t regionCode[] = {SCEData5, SCEData4, SCEData3, SCEData2, REGION_SELECTED, SCEData0};

//Pin Definition
#define DATA	0
#define SUBQ	1
#define SQCK	2
#define WFCK	3
#define UNUSED1	4
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
#define DELAY_US_BETWEEN_BITS (4000/32)		//4000us, one timer0 tick takes 32us
#define DELAY_MS_BETWEEN_INJECTIONS 90	//72 in oldcrow. PU-22+ work best with 80 to 100 (milliseconds)
#define NUM_WFCK_SAMPLES 5000			//number of times WFCK is sampled
#define MILLISECONDS_1 (1000/32)		//definition for a 1ms delay

bool pu22mode = false;		//Variable to keep track of if board is either PU-22 or older revision

uint8_t scbuf [12] = { 0 };	// We will be capturing PSX "SUBQ" packets, there are 12 bytes per valid read.
uint8_t bitbuf = 0;			//SUBQ bit storage
uint8_t scpos = 0;			//scbuf position
uint8_t hysteresis  = 0;	//Hysteresis counter for injecting region codes
bool isDataSector = false;	//Indicating if CD is reading in data-sector

int8_t EEPROM_read(uint8_t ucAddress)
{
	/* Wait for completion of previous write */
	while(EECR & (1<<EEPE))
	;
	/* Set up address register */
	EEAR = ucAddress;
	/* Start eeprom read by writing EERE */
	EECR |= (1<<EERE);
	/* Return data from data register */
	return EEDR;
}

//--------------------------------------------------
//     Returns the bit inside "regionCode"
//     at position "bitIndex", across bytes
//--------------------------------------------------

bool regionCodeBit(uint8_t bitIndex)
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
		while(TCNT0 <= DELAY_US_BETWEEN_BITS){//Takes 4ms
			if (!regionCodeBit(bit_counter))
			{
				OUTPUT(DATA); //Set DATA as output
				LOW(DATA); //Set DATA low
				//_delay_us(DELAY_US_BETWEEN_BITS);
			}
			else
			{
				if (pu22mode) {
					OUTPUT(DATA); //Set DATA as output and copy WFCK to DATA for DELAY_US_BETWEEN_BITS
					//for(uint16_t repeater = 0; repeater <= ((uint16_t)(DELAY_US_BETWEEN_BITS*1.03)); repeater++){
					if(READ(WFCK)){
						HIGH(DATA);
					}else{
						LOW(DATA);
					}
					//data = 0, wfck = 3 OUTPUT(DATA)
					//PORTB ^= (PINB >> 3)&1;
					//}
				}
				else { //PU-18 or lower mode
					INPUT(DATA);
					//_delay_us(DELAY_US_BETWEEN_BITS);
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
		OSCCAL -= 2;		
		//Clock is 8MHz because of CKDIV8 fuse
		//Power reduction setup
		PRR = (1<<PRTIM1)|(1<<PRUSI)|(1<<PRADC);	//Disable Timer1, USI, ADC
		ACSR = (1<<ACD);							//Disable analog comparator
	
		//Timer0 setup for counting up every 100us
		TCCR0A = 0;				//Clear registers
		TCCR0B = 0;				//Clear registers
		TCNT0 = 0;				//Count = 0
		OCR0A = 255;			//Count every 32us (256/8000000)
		TCCR0A |= (1 << WGM01);	//CTC
		TCCR0B |= (1 << CS02);	//Prescaler 256
								//No Interrupt on overflow etc
								
	sei(); //Enable interrupts globally

	//Port setup, redefining inputs just to be sure
	INPUT(SQCK);	//make input
	INPUT(SUBQ);	//make input
	INPUT(DATA);	//make input
	INPUT(WFCK);	//make input
	INPUT(UNUSED1);	//make input
	HIGH(UNUSED1);	//enable pullup
	INPUT(RESET);	//make input
	HIGH(RESET);	//enable pullup

	//Wait for console power on and stable signals
	while (!READ(SQCK));
	while (!READ(WFCK));

	// Board detection
	// WFCK: __-----------------------  // this is a PU-7 .. PU-20 board and WFCK is GATE!
	// WFCK: __-_-_-_-_-_-_-_-_-_-_-_-  // this is a PU-22 or newer board!
	uint16_t lows = 0;
	uint16_t highs = 0;
	while(1) { //Sample up to NUM_WFCK_SAMPLES times
		if (READ(WFCK)){
			highs++;
			if(highs > 25000){//If WFCK was high more than 25000 times, assume not PU-22 and break loop
				pu22mode = false;
				break;
			}
		}else{
			lows++;
			if(highs > 1){
				highs--;	//Just to be sure that a low weighs more than a high
			}
			if(lows > 2500){	//If WFCK was low more than 50 times, assume PU-22 and break loop
				pu22mode = true;
				break;
			}
		}
	}
	
	//Main loop
	while (1){
		scpos = 0;           // scbuf position

		start:
		TCNT0 = 0;
		// Capture 8 bits for 12 runs > complete SUBQ transmission
		for (uint8_t bitpos = 0; bitpos < 8; bitpos++) {
			while (READ(SQCK)) {	//Wait for clock to go low
				//Timeout resets capture buring bootup and in between packages
				if (TCNT0 >= 10){	//320us
					scpos = 0;		//reset SUBQ packet stream
					bitbuf = 0;
					goto start;
				}
			}

			while (!READ(SQCK));	//Wait for clock to go high

			bitbuf |= (READ(SUBQ) << bitpos);
			TCNT0 = 0;				//no problem with this bit
		}

		//byte done
		scbuf[scpos] = bitbuf;
		scpos++;
		bitbuf = 0;

		// repeat for all 12 bytes
		if (scpos < 12) {
			goto start;
		}

		// check if read head is in wobble area
		// We only want to unlock game discs (0x41) and only if the read head is in the outer TOC area.
		// We want to see a TOC sector repeatedly before injecting (helps with timing and marginal lasers).
		// All this logic is because we don't know if the HC-05 is actually processing a getSCEX() command.
		// Hysteresis is used because older drives exhibit more variation in read head positioning.
		// While the laser lens moves to correct for the error, they can pick up a few TOC sectors.
		isDataSector = ((scbuf[0] & 0xD0) == 0x40);

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
			hysteresis--; //Not a valid packet. Decrease the counter.
		}

		// hysteresis value "optimized" using very worn but working drive on ATmega328 @ 16Mhz
		// should be fine on other MCUs and speeds, as the PSX dictates SUBQ rate
		if (hysteresis >= 14) {
			// If the read head is still here after injection, resending should be quick.
			// Hysteresis naturally goes to 0 otherwise (the read head moved).
			hysteresis = 11;

			OUTPUT(DATA);	//Set DATA as output
			LOW(DATA);		//Set DATA low
			
			if (!pu22mode) {
				OUTPUT(WFCK);	//Set WFCK as output
				LOW(WFCK);		//Set WFCK low
			}

			// inject symbols now
			for (uint8_t loop_counter = 0; loop_counter < 3; loop_counter++)
			{
				inject_SCEX(); // e = SCEE, a = SCEA, i = SCEI
				for(uint8_t cnt = 0; cnt < 22; cnt++){ //88ms delay
					TCNT0 = 0;				//Reset counter to measure time
					while(TCNT0 <= DELAY_US_BETWEEN_BITS);	//4ms delay
				}
			}

			if (!pu22mode) {
				INPUT(WFCK); //Set WFCK as input
			}
			INPUT(DATA); //Set DATA as input
		}else{
			//start with a small delay, which can be necessary in cases where the MCU loops too quickly
			//and picks up the laster SUBQ trailing end
			//_delay_ms(1);
			TCNT0 = 0;
			while(TCNT0 <= MILLISECONDS_1);
		}
	}
	return 0;
}