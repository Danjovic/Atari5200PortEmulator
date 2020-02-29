/* 
   Atari 5200 Joystick Port Emulator
   This device was designed to allow the development of Atari 5200 controllers whithout having the console
   
   Daniel Jose Viana, Janeiro de 2020 - danjovic@hotmail.com
   
   Basic Release: 09 February 2020
   
   Version 1.0 - 29 February 2020

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.

*/

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
///                                                                                                         ///
///                                       LIBRARIES                                                         ///
///                                                                                                         ///
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <pic14regs.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>


///////////////////////////////////////////////////////////////////////////////////////////////////////////////
///                                                                                                         ///
///                                    CHIP CONFIGURATION                                                   ///
///                                                                                                         ///
///////////////////////////////////////////////////////////////////////////////////////////////////////////////



//#use delay(clock=8000000)
//#fuses INTRC_IO, NOPROTECT, NOBROWNOUT, NOWDT, PUT 
uint16_t __at _CONFIG configWord = _INTRC_OSC_NOCLKOUT & _CPD_OFF &  _CP_OFF & _LVP_OFF & _WDT_OFF & _PWRTE_ON & _MCLRE_OFF; // watchdog off

/*
   PIC16F628A
                   +--___--+                            
        VREF/RA2 --|1    18|-- RA1/AN1  POT_X                   
PIN1 ROW2    RA3 --|2    17|-- RA0/AN0  POT_Y                  
PIN2 ROW1    RA4 --|3    16|-- RA7 ROW3 PIN4                   
BOT_BTN MCLR/RA5 --|4    15|-- RA6 ROW0 PIN3                    
             GND --|5    14|-- VCC                            
CAV_CNTRL    RB0 --|6    13|-- RB7 LINE3 PIN8                     
    RXD   RX/RB1 --|7    12|-- RB6 LINE0 PIN7       
    TXD   TX/RB2 --|8    11|-- RB5 LINE1 PIN6
TOP_BTN      RB3 --|9    10|-- RB4 LINE2 PIN5
                   +-------+  
*/


///////////////////////////////////////////////////////////////////////////////////////////////////////////////
///                                                                                                         ///
///                                    DEFINITIONS AND CONSTANTS                                            ///
///                                                                                                         ///
///////////////////////////////////////////////////////////////////////////////////////////////////////////////



#define delay5us()        do { __asm__("nop\n nop\n nop\n nop\n nop"); } while (0)

#define cavOff() do {TRISB0=1; RB0=0;} while (0)  // CAV OFF
#define cavOn()  do {RB0=1; TRISB0=0;} while (0)  // CAV ON

#define TRISLIN0 TRISA6
#define TRISLIN1 TRISA4
#define TRISLIN2 TRISA3
#define TRISLIN3 TRISA7

#define RLIN0 RA6
#define RLIN1 RA4
#define RLIN2 RA3
#define RLIN3 RA7



//                - - - - 3 2 1 0 Bit position
// rows[] format  0 0 0 0 3 0 1 2 matrix bit
#define COL0 2 // bit 2
#define COL1 1 // bit 1
#define COL2 0 // bit 0
#define COL3 3 // bit 3


static uint8_t rows[4];
static uint8_t hline = 0; // 
static uint8_t potx=0,poty=0;
 
 

///////////////////////////////////////////////////////////////////////////////////////////////////////////////
///                                                                                                         ///
///                                        FUNCTION PROTOTYPES                                              ///
///                                                                                                         ///
///////////////////////////////////////////////////////////////////////////////////////////////////////////////


void _putc (uint8_t c);
void _puts (char *ptr);

void measurePotentimeters(void);
void scanKeyboard(void);
void printResults(void);
void _puts (char *ptr);
void printNumber( uint8_t n);
void _delayms(uint8_t n);


///////////////////////////////////////////////////////////////////////////////////////////////////////////////
///                                                                                                         ///
///                                          MAIN PROGRAM                                                   ///
///                                                                                                         ///
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

void main (void)
{

//
// SETUP
//

// Setup comparators
CMCON = (_CM1 | _CM0); // CM<2:0> = 011  Two Common Reference Comparators

/* Voltage reference, ViH min = 1.9V, ViH max = 2.6V, average ViH = 2.25V

VRR=1        VDD=5V          +----VREF----+
VR3  VR2  VR1  VR0  VR[3..0] VRR=1    VRR=0
 0    0    0    0       0     1,25     0,00
 0    0    0    1       1     1,41     0,21
 0    0    1    0       2     1,56     0,42
 0    0    1    1       3     1,72     0,63
 0    1    0    0       4     1,88     0,83
 0    1    0    1       5     2,03     1,04
 0    1    1    0       6     2,19     1,25
 0    1    1    1       7     2,34     1,46
 1    0    0    0       8     2,50     1,67
 1    0    0    1       9     2,66     1,88
 1    0    1    0       10    2,81     2,08
 1    0    1    1       11    2,97     2,29 <--
 1    1    0    0       12    3,13     2,50
 1    1    0    1       13    3,28     2,71
 1    1    1    0       14    3,44     2,92
 1    1    1    1       15    3,59     3,13  */
VRCON = _VREN | _VROE | _VRR | _VR3 | _VR1 | _VR0; // Vref = (11/24) * 5 = 2.29 Volts 


// Setup I/O pins
TRISA = 0xFF;  // All pins as inputs, initially
TRISB = (uint8_t) ~(_TRISB0);  // Pin RB0 (CAV_CNTRL) as output

// Turn on Pullups on port B
NOT_RBPU=0;
PORTB = (uint8_t) (_RB0 |  _RB3 | _RB4 | _RB5 | _RB6 |_RB7 ); 

// Setup Serial Port
BRGH=1;
TXEN=1;
SYNC=0;
SPEN=1;
SPBRG = 25; // 9600 bps @ 4MHz


// Setup Timer0 
// reload time 192 for 15,75KHz
__asm__("clrwdt");
T0CS=0;   // Timer 0 clocked by internal CPU clock (4MHz)
PSA=1;    // prescaler assigned to WDT (timer0 clocked at 1:1)
TMR0 = 0; // Clear Timer 0


//
// Main loop
//
  for (;;) {  // TODO check RXbuffer for new commands

	scanKeyboard();

    cavOff(); // TRISB0=1; RB0=0; // CAV OFF
	_delayms(16);
	measurePotentimeters(); 
	
	if ( (potx>220) && (poty>220) ) { // Normal joystick
       _puts("[Joystick]");	   	
	} else { // trackball connected
       _puts("[TrackBall]");	   
	}
	
    cavOn(); //RB0=1; TRISB0=0; // CAV ON
	_delayms(16);	
	measurePotentimeters(); 
	
	scanKeyboard();
	printResults(); 
	
  } // for  
} // main loop


///////////////////////////////////////////////////////////////////////////////////////////////////////////////
///                                                                                                         ///
///                                               FUNCTIONS                                                 ///
///                                                                                                         ///
///////////////////////////////////////////////////////////////////////////////////////////////////////////////

void measurePotentimeters(void) {
	uint8_t j;
	// Release capacitors to charge
	TRISA0 = 1;
	TRISA1 = 1;
	

	// timed loop, trimmed to 64us  
	for (hline=0;hline<228;hline++) {  // 7 cycles
	  if (C1OUT) poty=hline; else __asm__("nop\n nop\n nop\n nop\n nop"); // 9 cycles
	  if (C2OUT) potx=hline; else __asm__("nop\n nop\n nop\n nop\n nop"); // 9 cycles
	   
	  for (j=0;j<3;j++); // 2+8*3 = 26 cycles 
	  __asm__("nop\n nop\n nop\n nop\n nop"); // 5 cycles
	}
	
	// Hold capacitors on discharge
	TRISA0=0; RA0=0;
	TRISA1=0; RA1=0;
}



void scanKeyboard(void) {
	uint8_t j;
      
     
	 
//    8  5  6  7
//	3 *  7  4  1  LIN0
//	2 0  8  5  2  LIN1
//	1 #  9  6  3  LIN2
//	4    R  P  S  LIN3

//    3  2  1  0  <- COL
	
	// Each row remains active by two horizontal lines (128us)
	
	// Select first line  4+4+50+4+23+42+1 = 128 cycles
	TRISLIN3 = 1; RLIN3 = 1;          // 4 cycles
	TRISLIN0 = 0; RLIN0 = 0;          // 4 cycles
	for (j=0;j<6;j++);               // 2+8*6 = 50 cycles	
	__asm__("nop\n nop\n nop\n nop"); // 4 cycles
	rows[0] = (PORTB & 0xF0)>>4;     // 23 cycles
	for (j=0;j<5;j++);               // 2+8*5 = 42 cycles
	__asm__("nop");                   // 1 cycle
	
	
	// Select 2nd line  4+4+50+4+23+42+1 = 128 cycles
	TRISLIN0 = 1; RLIN0 = 1;          // 4 cycles
	TRISLIN1 = 0; RLIN1 = 0;          // 4 cycles
	for (j=0;j<6;j++);               // 2+8*6 = 50 cycles	
	__asm__("nop\n nop\n nop\n nop"); // 4 cycles
	rows[1] = (PORTB & 0xF0)>>4;     // 23 cycles
	for (j=0;j<5;j++);               // 2+8*5 = 42 cycles
	__asm__("nop");                   // 1 cycle	
	
	

	// Select third line  4+4+50+4+23+42+1 = 128 cycles
	TRISLIN1 = 1; RLIN1 = 1;          // 4 cycles
	TRISLIN2 = 0; RLIN2 = 0;          // 4 cycles
	for (j=0;j<6;j++);               // 2+8*6 = 50 cycles	
	__asm__("nop\n nop\n nop\n nop"); // 4 cycles
	rows[2] = (PORTB & 0xF0)>>4;     // 23 cycles
	for (j=0;j<5;j++);               // 2+8*5 = 42 cycles
	__asm__("nop");                   // 1 cycle
	
	// Select fourth line  4+4+50+4+23+42+1 = 128 cycles
	TRISLIN2 = 1; RLIN2 = 1;          // 4 cycles
	TRISLIN3 = 0; RLIN3 = 0;          // 4 cycles
	for (j=0;j<6;j++);               // 2+8*6 = 50 cycles	
	__asm__("nop\n nop\n nop\n nop"); // 4 cycles
	rows[3] = (PORTB & 0xF0)>>4;     // 23 cycles
	for (j=0;j<5;j++);               // 2+8*5 = 42 cycles
	__asm__("nop");                   // 1 cycle
	

}

	
void _putc (uint8_t c) {
	while (!TRMT); // wait for transmit buffer to be empty
	TXREG = c;     // send character 
}


void _puts (char *ptr) {
  while (*ptr) {
    _putc (*ptr);
	if (*ptr == '\n') _putc('\r');
	ptr++;
  }
}


void printNumber( uint8_t n) {
   uint8_t digit;
   
   digit='0';
   while (n>=100) {
     digit++;
	 n=n-100;
   }
   _putc(digit);
   
   digit='0';
   while (n>=10) {
     digit++;
	 n=n-10;
   }
   _putc(digit);
   
    digit='0';
   while (n>=1) {
     digit++;
	 n=n-1;
   }
   _putc(digit);
}


/*
        | 3 | 2 | 1 |   4   | pin
Pin row | 0 | 1 | 2 |   3   | COL
--------+---+---+---+-------+
 7    0 | 1 | 2 | 3 | Start |
 6    1 | 4 | 5 | 6 | Pause |
 5    2 | 7 | 8 | 9 | Reset |
 8    3 | * | 0 | # |  None |

*/
void printResults(void){
  // print axes information
  _puts("PotX:");
  printNumber(potx);
  _puts(" PotY:");
  printNumber(poty);
  
  // print buttons
  _puts(" Top:");
  if (RB3==0) _putc('1'); else _putc('0');
  _puts(" Bot:");
  if (RA5==0) _putc('1'); else _putc('0');
  
  // print Keys
  _puts(" Keys:");
  //                7 6 5 4 3 2 1 0 Bit
  // rows[] format  0 0 0 0 3 0 1 2 matrix bit
  
  if ((rows[2] & (1<<COL3))==0) _putc('#');
  if ((rows[2] & (1<<COL0))==0) _putc('3');
  if ((rows[2] & (1<<COL1))==0) _putc('6');
  if ((rows[2] & (1<<COL2))==0) _putc('9');
  if ((rows[1] & (1<<COL3))==0) _putc('0');
  if ((rows[1] & (1<<COL0))==0) _putc('2');
  if ((rows[1] & (1<<COL1))==0) _putc('5');
  if ((rows[1] & (1<<COL2))==0) _putc('8');
  if ((rows[0] & (1<<COL3))==0) _putc('*');
  if ((rows[0] & (1<<COL0))==0) _putc('1');
  if ((rows[0] & (1<<COL1))==0) _putc('4');  
  if ((rows[0] & (1<<COL2))==0) _putc('7');

  if ((rows[3] & (1<<COL0))==0) _putc('S');
  if ((rows[3] & (1<<COL1))==0) _putc('P');
  if ((rows[3] & (1<<COL2))==0) _putc('R');

  _puts("\n");  
}


void _delayms(uint8_t n) {
uint8_t j;
 do {                     // total of = (10+10*j) *n  
//    __asm__("nop\n");
    j=99;
    do { 
       __asm__("nop\n nop\n"); 
    } while (--j);
 } while (--n);
}
