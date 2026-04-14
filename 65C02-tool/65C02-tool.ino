#include <Arduino.h>
#include <errno.h>

/**************************************************************************************
 * CONSTANTS - I/O PIN MAPPING
 **************************************************************************************
 *
 * A total of 35 digital pins are required to use all of the features of this tool.
 * 
 * You can use any digital pin for any input/output, EXCEPT, the clock pin (PHI0).
 * PHI0 must be on an interrupt pin to use the monitor or measure clock commands,
 * and PHI0 must be on timer pin to generate a clock.
 * 
 * Only pins 2 and 3 on the Arduino Mega meet both criteria
 * 
 * The default mapping puts the pins (including GND) on the edge connector in the 
 * same order they appear on the chip (with the exception of PHI0)
 * 
 * AB0-AB15 are the address bus
 * DB0-DB15 are the data bus
 * 
 * The control inputs (READY, IRQ, NMI, BE, SOB), should be pulled high with a 1K+
 * resistor (not tied high with a jumper) so that the arduino can pull them low when
 * needed and they still have a signal when the arduino is disconnected.
 * 
 * This also applies to the two PROM inputs (WE, OE) used when writing to the prom in place
 *
 **************************************************************************************/

#define PHI0                24

#define READY               26
#define IRQ                 30
#define NMI                 31
#define SYNC                27

#define AB0                 0
#define AB1                 1
#define AB2                 2
#define AB3                 3
#define AB4                 4
#define AB5                 5
#define AB6                 6
#define AB7                 7

#define AB8                 8
#define AB9                 9
#define AB10                10
#define AB11                11
#define AB12                12
#define AB13                13
#define AB14                14
#define AB15                15

#define DB0                 16
#define DB1                 17
#define DB2                 18
#define DB3                 19
#define DB4                 20
#define DB5                 21
#define DB6                 22
#define DB7                 23

#define RW_READ             25
#define BUS_ENABLE          28
#define SOB                 32
#define RESET               29

#define PROM_WRITE_DISABLE  32
#define PROM_OUTPUT_DISABLE 31

/*********************************************
 * ERROR Codes returned by various functions *
 *********************************************/

#define E_OK                0
#define E_MISSING           1
#define E_INVALID           2
#define E_NOT_SUPPORTED     3
#define E_CLOCK_DETECTED    4
#define E_OUT_OF_RANGE      5

/*****************************************************
 * Addressing modes used by the 65c02 microprocessor *
 *****************************************************/

#define AM_ABS              0     // Absolute                             a
#define AM_ABS_X_ID         1     // Absolute Indexed Indirect            (a,x)
#define AM_ABS_X            2     // Absolute Indexed with X              a,x
#define AM_ABS_Y            3     // Absolute Indexed with Y              a,y
#define AM_ABS_IDR          4     // Absolute Indirect                    (a)
#define AM_ACC              5     // Accumulator                          A
#define AM_IMM              6     // Immediate Addressing                 #
#define AM_IMP              7     // Implied or Stack                     i  
#define AM_REL              8     // Program Counter Relative             r
#define AM_ZP               9     // Zero Page                            zp
#define AM_ZP_X_IDR         10    // Zero Page Indexed Indirect           (zp,x)
#define AM_ZP_X             11    // Zero Page Indexed with X             zp,x
#define AM_ZP_Y             12    // Zero Page Indexed with Y             zp,y
#define AM_ZP_IDR           13    // Zero Page Indirect                   (zp)  
#define AM_ZP_IDR_Y         14    // Zero Page Indirect Indexed with Y    (zp),y

/***************************
 * Custom Type Definitions *
 ***************************/

typedef struct  {
  const char *name;
  void (*execute)(void);
} command;

typedef struct 
{
  char    mnemonic[5];
  byte    addressMode;
} OPCODE;

typedef struct
{
  char    form[14];
  byte    operandSize;
} ADDRESSMODE;


struct BusSample {
  uint16_t address;
  uint8_t data;
  uint8_t flags;   // bit0 = reading, bit1 = opcode fetch
};

/**********************
 * Run-time constants *
 **********************/

const char DELIMS[] = " \t";

const byte ADDR[] = { AB0, AB1, AB2, AB3, AB4, AB5, AB6, AB7, AB8, AB9, AB10, AB11, AB12, AB13, AB14, AB15 };
const byte DATA[] = { DB0, DB1, DB2, DB3, DB4, DB5, DB6, DB7 };

byte ADDRMASK[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
byte ADDRDATAMASK[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

// 65C02 opcode table for decoding instructions while monitoring the bus

const OPCODE OPCODES[] = 
{
  { "BRK ", AM_IMP }, { "ORA ", AM_ZP_X_IDR },  { "??? ", AM_IMP },     { "??? ", AM_IMP },     { "TSB ", AM_ZP },      { "ORA ", AM_ZP },    { "ASL ", AM_ZP },    { "RMB0", AM_ZP },    // $00 - $07 
  { "PHP ", AM_IMP }, { "ORA ", AM_IMM },       { "ASL ", AM_ACC },     { "??? ", AM_IMP },     { "TSB ", AM_ABS },     { "ORA ", AM_ABS },   { "ASL ", AM_ABS },   { "BBR0", AM_REL },   // $08 - $0F
  { "BPL ", AM_REL }, { "ORA ", AM_ZP_IDR_Y },  { "ORA ", AM_ZP_IDR },  { "??? ", AM_IMP },     { "TRB ", AM_ZP },      { "ORA ", AM_ZP_X },  { "ASL ", AM_ZP_X },  { "RMB1", AM_ZP },    // $10 - $17 
  { "CLC ", AM_IMP }, { "ORA ", AM_ABS_Y },     { "INC ", AM_ACC },     { "??? ", AM_IMP },     { "TRB ", AM_ABS },     { "ORA ", AM_ABS_X }, { "ASL ", AM_ABS_X }, { "BBR1", AM_REL },   // $18 - $1F
  { "JSR ", AM_ABS }, { "AND ", AM_ZP_X_IDR },  { "??? ", AM_IMP },     { "??? ", AM_IMP },     { "BIT ", AM_ZP },      { "AND ", AM_ZP },    { "ROL ", AM_ZP },    { "RMB2", AM_ZP },    // $20 - $27 
  { "PLP ", AM_IMP }, { "AND ", AM_IMM },       { "ROL ", AM_ACC },     { "??? ", AM_IMP },     { "BIT ", AM_ABS },     { "AND ", AM_ABS },   { "ROL ", AM_ABS },   { "BBR2", AM_REL },   // $28 - $2F
  { "BMI ", AM_REL }, { "AND ", AM_ZP_IDR_Y },  { "AND ", AM_ZP_IDR },  { "??? ", AM_IMP },     { "BIT ", AM_ZP_X },    { "AND ", AM_ZP_X },  { "ROL ", AM_ZP_X },  { "RMB3", AM_ZP },    // $30 - $37 
  { "SEC ", AM_IMP }, { "AND ", AM_ABS_Y },     { "DEC ", AM_ACC },     { "??? ", AM_IMP },     { "BIT ", AM_ABS_X },   { "AND ", AM_ABS_X }, { "ROL ", AM_ABS_X }, { "BBR3", AM_REL },   // $38 - $3F
  { "RTI ", AM_IMP }, { "EOR ", AM_ZP_X_IDR },  { "??? ", AM_IMP },     { "??? ", AM_IMP },     { "??? ", AM_IMP },     { "EOR ", AM_ZP },    { "LSR ", AM_ZP },    { "RMB4", AM_ZP },    // $40 - $47 
  { "PHA ", AM_IMP }, { "EOR ", AM_IMM },       { "LSR ", AM_ACC },     { "??? ", AM_IMP },     { "JMP ", AM_ABS },     { "EOR ", AM_ABS },   { "LSR ", AM_ABS },   { "BBR4", AM_REL },   // $48 - $4F
  { "BVC ", AM_REL }, { "EOR ", AM_ZP_IDR_Y },  { "EOR ", AM_ZP_IDR },  { "??? ", AM_IMP },     { "??? ", AM_IMP },     { "EOR ", AM_ZP_X },  { "LSR ", AM_ZP_X },  { "RMB5", AM_ZP },    // $50 - $57 
  { "CLI ", AM_IMP }, { "EOR ", AM_ABS_Y },     { "PHY ", AM_IMP },     { "??? ", AM_IMP },     { "??? ", AM_IMP },     { "EOR ", AM_ABS_X }, { "LSR ", AM_ABS_X }, { "BBR5", AM_REL },   // $58 - $5F
  { "RTS ", AM_IMP }, { "ADC ", AM_ZP_X_IDR },  { "??? ", AM_IMP },     { "??? ", AM_IMP },     { "STZ ", AM_ZP },      { "ADC ", AM_ZP },    { "ROR ", AM_ZP },    { "RMB6", AM_ZP },    // $60 - $67 
  { "PLA ", AM_IMP }, { "ADC ", AM_IMM },       { "ROR ", AM_ACC },     { "??? ", AM_IMP },     { "JMP ", AM_ABS_IDR }, { "ADC ", AM_ABS },   { "ROR ", AM_ABS },   { "BBR6", AM_REL },   // $68 - $6F
  { "BVS ", AM_REL }, { "ADC ", AM_ZP_IDR_Y },  { "ADC ", AM_ZP_IDR },  { "??? ", AM_IMP },     { "STZ ", AM_ZP_X },    { "ADC ", AM_ZP_X },  { "ROR ", AM_ZP_X },  { "RMB7", AM_ZP },    // $70 - $77 
  { "SEI ", AM_IMP }, { "ADC ", AM_ABS_Y },     { "PLY ", AM_IMP },     { "??? ", AM_IMP },     { "JMP ", AM_ABS_X_ID },{ "ADC ", AM_ABS_X }, { "ROR ", AM_ABS_X }, { "BBR7", AM_REL },   // $78 - $7F
  { "BRA ", AM_REL }, { "STA ", AM_ZP_X_IDR },  { "??? ", AM_IMP },     { "??? ", AM_IMP },     { "STY ", AM_ZP },      { "STA ", AM_ZP },    { "STX ", AM_ZP },    { "SMB0", AM_ZP },    // $80 - $87 
  { "DEY ", AM_IMP }, { "BIT ", AM_IMM },       { "TXA ", AM_IMP },     { "??? ", AM_IMP },     { "STY ", AM_ABS },     { "STA ", AM_ABS },   { "STX ", AM_ABS },   { "BBS0", AM_REL },   // $88 - $8F
  { "BCC ", AM_REL }, { "STA ", AM_ZP_IDR_Y },  { "STA ", AM_ZP_IDR },  { "??? ", AM_IMP },     { "STY ", AM_ZP_X },    { "STA ", AM_ZP_X },  { "STX ", AM_ZP_Y },  { "SMB1", AM_ZP },    // $90 - $97 
  { "TYA ", AM_IMP }, { "STA ", AM_ABS_Y },     { "TXS ", AM_IMP },     { "??? ", AM_IMP },     { "STZ ", AM_ABS },     { "STA ", AM_ABS_X }, { "STZ ", AM_ABS_X }, { "BBS1", AM_REL },   // $98 - $9F
  { "LDY ", AM_IMM }, { "LDA ", AM_ZP_X_IDR },  { "LDX ", AM_IMM },     { "??? ", AM_IMP },     { "LDY ", AM_ZP },      { "LDA ", AM_ZP },    { "LDX ", AM_ZP },    { "SMB2", AM_ZP },    // $A0 - $A7 
  { "TAY ", AM_IMP }, { "LDA ", AM_IMM },       { "TAX ", AM_IMP },     { "??? ", AM_IMP },     { "LDY ", AM_ABS },     { "LDA ", AM_ABS },   { "LDX ", AM_ABS },   { "BBS2", AM_REL },   // $A8 - $AF
  { "BCS ", AM_REL }, { "LDA ", AM_ZP_IDR_Y },  { "LDA ", AM_ZP_IDR },  { "??? ", AM_IMP },     { "LDY ", AM_ZP_Y },    { "LDA ", AM_ZP_X },  { "LDX ", AM_ZP_Y },  { "SMB3", AM_ZP },    // $B0 - $B7 
  { "CLV ", AM_IMP }, { "LDA ", AM_ABS_Y },     { "TSX ", AM_IMP },     { "??? ", AM_IMP },     { "LDY ", AM_ABS_Y },   { "LDA ", AM_ABS_X }, { "LDX ", AM_ABS_Y }, { "BBS3", AM_REL },   // $B8 - $BF
  { "CPY ", AM_IMM }, { "CMP ", AM_ZP_X_IDR },  { "??? ", AM_IMP },     { "??? ", AM_IMP },     { "CPY ", AM_ZP },      { "CMP ", AM_ZP },    { "DEC ", AM_ZP },    { "SMB4", AM_ZP },    // $C0 - $C7 
  { "INY ", AM_IMP }, { "CMP ", AM_IMM },       { "DEX ", AM_IMP },     { "WAI ", AM_IMP },     { "CPY ", AM_ABS },     { "CMP ", AM_ABS },   { "DEC ", AM_ABS },   { "BBS4", AM_REL },   // $C8 - $CF
  { "BNE ", AM_REL }, { "CMP ", AM_ZP_IDR_Y },  { "CMP ", AM_ZP_IDR },  { "??? ", AM_IMP },     { "??? ", AM_IMP },     { "CMP ", AM_ZP_X },  { "DEC ", AM_ZP_X },  { "SMB5", AM_ZP },    // $D0 - $D7 
  { "CLD ", AM_IMP }, { "CMP ", AM_ABS_Y },     { "PHX ", AM_IMP },     { "STP ", AM_IMP },     { "??? ", AM_IMP },     { "CMP ", AM_ABS_X }, { "DEC ", AM_ABS_X }, { "BBS5", AM_REL },   // $D8 - $DF
  { "CPX ", AM_IMM }, { "SBC ", AM_ZP_X_IDR },  { "??? ", AM_IMP },     { "??? ", AM_IMP },     { "CPX ", AM_ZP },      { "SBC ", AM_ZP },    { "INC ", AM_ZP },    { "SMB6", AM_ZP },    // $E0 - $E7 
  { "INX ", AM_IMP }, { "SBC ", AM_IMM },       { "NOP ", AM_IMP },     { "??? ", AM_IMP },     { "CPX ", AM_ABS },     { "SBC ", AM_ABS },   { "INC ", AM_ABS },   { "BBS6", AM_REL },   // $E8 - $EF
  { "BEQ ", AM_REL }, { "SBC ", AM_ZP_IDR_Y },  { "SBC ", AM_ZP_IDR },  { "??? ", AM_IMP },     { "??? ", AM_IMP },     { "SBC ", AM_ZP_X },  { "INC ", AM_ZP_X },  { "SMB7", AM_ZP },    // $F0 - $F7 
  { "SED ", AM_IMP }, { "SBC ", AM_ABS_Y },     { "PLX ", AM_IMP },     { "??? ", AM_IMP },     { "??? ", AM_IMP },     { "SBC ", AM_ABS_X }, { "INC ", AM_ABS_X }, { "BBS7", AM_REL }    // $F8 - $FF
};

// Address mode operand formatting

const ADDRESSMODE ADDRESSMODES[] = 
{
  { " $%04x", 2 },         // AM_ABS         Absolute                    a
  { " ($%04x,X)", 2 },     // AM_ABS_X_ID    Absolute Indexed Indirect   (a,x)
  { " $%04x,X", 2 },       // AM_ABS_X       Absolute Indexed with X     a,x
  { " $%04x,Y", 2 },       // AM_ABS_Y       Absolute Indexed with Y     a,y
  { " ($%04x)", 2 },       // AM_ABS_IDR     Absolute Indirect           (a)
  { " A", 0 },             // AM_ACC         Accumulator                 A
  { " #$%02hx", 1 },       // AM_IMM         Immediate Addressing        #
  { "", 0 },               // AM_IMP         Implied                     i
  { " $%02hx", 1 },        // AM_REL         Program Counter Relative    r
  { " $00%02hx", 1 },      // AM_ZP          Zero Page                   zp
  { " ($00%02hx,X)", 1 },  // AM_ZP_X_IDR    Zero Page Indexed Indirect  (zp,x)
  { " $00%02hx,X", 1 },    // AM_ZP_X        Zero Page Indexed with X    zp,x
  { " $00%02hx,Y", 1 },    // AM_ZP_Y        Zero Page Indexed with Y    zp,y
  { " ($00%02hx)", 1 },    // AM_ZP_IDR      Zero Page Indirect          (zp)  
  { " ($00%02hx),Y", 1 }   // AM_ZP_IDR_Y    Zero Page Indirect Indexed  (zp),y
};

const int OPERAND_BITMASK[] =
{
  0x00,
  0x01,
  0x03
};

/********************
 * Global Variables *
 ********************/

bool internalClock = false;
int processorDisables = 0;

/******************
 * Setup Function *
 ******************/

void setup()
{
  pinMode(PHI0, INPUT);
  pinMode(READY, INPUT);
  pinMode(RW_READ, INPUT);
  pinMode(BUS_ENABLE, INPUT);
  pinMode(SYNC, INPUT);
  pinMode(RESET, INPUT);
  pinMode(SOB, INPUT);
  pinMode(IRQ, INPUT);
  pinMode(NMI, INPUT);
  
  for (int i = 0; i < 16; i++)
  {
    byte port = digitalPinToPort(ADDR[i]);
    ADDRMASK[port] |= digitalPinToBitMask(ADDR[i]);
    ADDRDATAMASK[port] |= digitalPinToBitMask(ADDR[i]);
  }

  for (int i = 0; i < 8; i++)
  {
    byte port = digitalPinToPort(DATA[i]);
    ADDRDATAMASK[port] |= digitalPinToBitMask(DATA[i]);
  }

  clearAddressData();
  
  digitalWriteFast(PROM_WRITE_DISABLE, HIGH);
  pinMode(PROM_WRITE_DISABLE, OUTPUT);

  digitalWriteFast(PROM_OUTPUT_DISABLE, LOW);
  pinMode(PROM_OUTPUT_DISABLE, OUTPUT);
  
  Serial.begin(115200);

  while (!Serial)
  {
  }

  Serial.println("\x0c\x1b[2J");
  Serial.println("65C02 Tool Ready");
  Serial.println();  
}

/***********************
   SERIAL I/O ROUTINES
 ***********************/

char getChar()
{
  int c = -1;

  while (c < 0)
  {
    c = Serial.read();
  }

  return c;
}

int getLine(char *pcBuffer, int maxLength)
{
  int i = 0;

  while (true)
  {
    char c = getChar();

    switch (c)
    {
      case 3:       // Control+C
        Serial.println("^C");
        return -1;

      case 8:       // Backspace
      case 127:     // Delete
        if (i > 0)
        {
          Serial.print("\x08 \x08");
          i--;
        }
        else
        {
          Serial.print((char) 7); // Beep
        }
        break;

      case 27:      // Escape - Start of special codes on VT family of protocols, this just reads the code and discards it.
        {
          int x = 0, y = -1;

          while (x++ < 4096 && y < 0)
          {
            y = Serial.read();
          }

          if (y == '[')
          {
            while (x++ < 4096)
            {
              y = Serial.read();

              if (0x30 <= y && y <= 0x7E)
                break;
            }
          }
          break;
        }

      case '\0':  // NUL  
      case '\r':  // CR
      case '\n':  // LF
        pcBuffer[i] = '\0';

        Serial.println();

        return i;

      default:    // All other chars just add them to our keybaord buffer
        if (i + 1 == maxLength)
        {
          Serial.print("\\");
          Serial.print((char) 7);
          Serial.println();
          return -1;
        }

        Serial.print((char) c);

        pcBuffer[i++] = c;

        break;
    }
  }
}

void writef(const char *format, ...)
{
  char buffer[256];

  va_list args;

  va_start(args, format);

  int bytes = vsnprintf(buffer, sizeof(buffer), format, args);

  va_end(args);

  Serial.write(buffer, bytes);
}

void writelnf(const char *format, ...)
{
  char buffer[256];
  
  va_list args;

  va_start(args, format);

  vsnprintf(buffer, sizeof(buffer), format, args);

  va_end(args);

  Serial.println(buffer);
}

/********************
   String Functions
 ********************/

bool isNullOrEmpty(char const *x)
{
  return x == NULL || x[0] == '\0';
}

int stricmp(char const *a, char const *b)
{
  for (;; a++, b++)
  {
    int d = tolower((unsigned char) * a) - tolower((unsigned char) * b);

    if (d != 0 || !*a)
      return d;
  }
}

/************************************************
   Get or Set Address, Data, or Control bus pins
 ************************************************/

inline uint8_t getData()
{
  uint8_t data = 0;
  for (int i = 0; i < 8; i++)
  {
    data |= digitalReadFast(DATA[i]) << i;
  }
  return data;
}

inline uint16_t getAddress()
{
  uint16_t address = 0;
  for (int i = 0; i < 16; i++) {
    address |= digitalReadFast(ADDR[i]) << i;
  }
  return address;
}

void setAddress(uint16_t address)
{
  noInterrupts();
  // Drive address pins as outputs and write the bits
  for (uint8_t i = 0; i < 16; i++)
  {
    const uint8_t pin = ADDR[i];
    pinMode(pin, OUTPUT);
    digitalWriteFast(pin, (address >> i) & 0x01);
  }

  interrupts();
}

void setAddressData(uint16_t address, uint8_t data)
{
  noInterrupts();

  // Address bus
  for (uint8_t i = 0; i < 16; i++)
  {
    const uint8_t pin = ADDR[i];
    pinMode(pin, OUTPUT);
    digitalWriteFast(pin, (address >> i) & 0x01);
  }

  // Data bus
  for (uint8_t i = 0; i < 8; i++)
  {
    const uint8_t pin = DATA[i];
    pinMode(pin, OUTPUT);
    digitalWriteFast(pin, (data >> i) & 0x01);
  }

  interrupts();
}

void clearAddressData()
{
  noInterrupts();

  // Return all address pins to high impedance, no pullup
  for (uint8_t i = 0; i < 16; i++)
  {
    const uint8_t pin = ADDR[i];
    digitalWriteFast(pin, LOW);   // ensures no pullup when INPUT
    pinMode(pin, INPUT);
  }

  // Return all data pins to high impedance, no pullup
  for (uint8_t i = 0; i < 8; i++)
  {
    const uint8_t pin = DATA[i];
    digitalWriteFast(pin, LOW);   // ensures no pullup when INPUT
    pinMode(pin, INPUT);
  }

  interrupts();
}

bool isReading()
{
  return digitalReadFast(RW_READ);
}

void setRead()
{
  pinMode(RW_READ, OUTPUT);

  digitalWriteFast(RW_READ, HIGH);
}

void setWrite()
{
  pinMode(RW_READ, OUTPUT);

  digitalWriteFast(RW_READ, LOW);

}

void clearReadWrite()
{
  pinMode(RW_READ, INPUT);
}

bool isOpCodeFetch()
{
  return digitalReadFast(SYNC);
}

/*********
   Reset
 *********/
 
bool tryReset()
{
  if (!waitForClock(HIGH, 1000) || !waitForClock(LOW, 1000))
    return false;
        
  pinMode(RESET, OUTPUT);
  
  digitalWriteFast(RESET, LOW);

  // Reset needs to be held low for at least 2 clock cycles, we'll do 4 just to be safe
  for (int i = 0; i < 4; i++)
  {
    if (!waitForClock(HIGH, 1000) || !waitForClock(LOW, 1000))
      return false;
  }
  
  pinMode(RESET, INPUT);
  
  return true;
}

/**************
   Clock Sync
 **************/

const long clockSelectPrescaler[] = { 0, 1, 8, 64, 256, 1024 };
static bool monitorEnabled = false;

IntervalTimer phi0Timer;

volatile BusSample sampleBuffer[256];
volatile uint16_t sampleWriteIndex = 0;
volatile uint16_t sampleReadIndex = 0;
volatile bool sampleOverflow = false;

inline static void phi0Sample()
{
  uint16_t next = (sampleWriteIndex + 1) & 0xFF;
  if (next == sampleReadIndex) {
    sampleOverflow = true;
    return;
  }
  sampleBuffer[sampleWriteIndex] = readSample();
  sampleWriteIndex = next;
}

inline static void phi0ClockISR()
{
  // Toggle the clock pin each interrupt.
  // Full square-wave frequency = interrupt frequency / 2
  if (monitorEnabled && !digitalReadFast(PHI0)) {
    phi0Sample();
  }
  digitalToggleFast(PHI0);
}

int startInternalClock(long frequencyInHz)
{
  if (frequencyInHz == 0)
  {
    stopInternalClock();
    return E_OK;
  }

  if (frequencyInHz < 1)
    return E_OUT_OF_RANGE;

  // If external clock already detected, preserve original behavior
  if (!internalClock && waitForClock(HIGH, 2000) && waitForClock(LOW, 2000))
    return E_CLOCK_DETECTED;

  pinMode(PHI0, OUTPUT);
  digitalWriteFast(PHI0, LOW);

  // We toggle the pin in the ISR, so ISR rate is 2x desired clock frequency
  const double interval_us = 1000000.0 / (2.0 * frequencyInHz);

  // IntervalTimer runs in microseconds
  if (!phi0Timer.begin(phi0ClockISR, interval_us))
    return E_NOT_SUPPORTED;

  internalClock = true;
  return E_OK;
}

void stopInternalClock()
{
  phi0Timer.end();
  digitalWriteFast(PHI0, LOW);
  pinMode(PHI0, INPUT);
  internalClock = false;
}

bool waitForClock(byte value, unsigned long timeout)
{
  unsigned long deadline = millis() + timeout;

  while (digitalReadFast(PHI0) != value)
  {
    if ((long)(millis() - deadline) > 0)
      return false;
  }

  return true;
}

/*************
   Processor
 *************/

void disableProcessor()
{
  if (processorDisables++ > 0)
    return;
    
  digitalWrite(READY, LOW);
  digitalWrite(BUS_ENABLE, LOW);    

  pinMode(READY, OUTPUT);
  pinMode(BUS_ENABLE, OUTPUT);
}

void enableProcessor()
{
  if (--processorDisables > 0)
    return;

  digitalWrite(BUS_ENABLE, HIGH);
  digitalWrite(READY, HIGH);
  
  pinMode(BUS_ENABLE, INPUT);
  pinMode(READY, INPUT);
}

/***************************************
   Peek & Poke a single memory address
 ***************************************/

bool peek(unsigned int address, byte &data)
{
  bool result = false;
  
  disableProcessor();

  if (waitForClock(LOW, 1000))
  {
    setRead();

    setAddress(address);

    if (waitForClock(HIGH, 1000) && waitForClock(LOW, 1000))
    {
      data = getData();
      result = true;
    }
    
    clearAddressData();

    clearReadWrite();
  }
 
  enableProcessor();

  return result;
}

bool poke(unsigned int address, byte data)
{
  bool result = false;
  
  disableProcessor();
  
  if (waitForClock(LOW, 1000))
  {
    setWrite();

    setAddressData(address, data);

    if (waitForClock(HIGH, 1000) && waitForClock(LOW, 1000))
      result = true;

    clearReadWrite();

    clearAddressData();
  }

  enableProcessor();

  return result;
}

/****************************************************************************************************************

   ISR for watching the bus on each clock tick, decoding instructions along the way.

   The limiting factor for this is how fast it can output to the serial port. Although there can be up to 25
   bytes to write in a single clock cycle of the 6502, there shouldn't be more that 38 bytes written for any 2
   consecutive clock cycles. The RS232 serial protocal requires 10 bits per byte (1 start bit, 8 data bits, 1
   stop bit) so that equates to 380 bits per two clock cycles or roughly 190 bits/cycle in the worst case.

   At the default 115200 baud we'll start to miss clock cycles around 600 Hz

   At the maximum 2000000 baud we hit that limit with a clock around 10 kHz

 ****************************************************************************************************************/

unsigned int currentOpAddress = 0;
byte currentOpCode = 0;
byte currentAddressMode = 0;

unsigned int operand = 0;
int operandSize = 0;
int operandBytesNeeded = -1;

int consecutiveActionCount = 0;
unsigned int lastAddress;
byte lastAction = -1;

inline BusSample readSample() {
  BusSample s;
  s.address = getAddress();
  s.data = getData();
  s.flags = 0;
  if (isReading()) s.flags |= 0x01;
  if (isOpCodeFetch()) s.flags |= 0x02;
  return s;
}



inline void processSample(size_t i)
{
  noInterrupts();
  uint16_t address = sampleBuffer[sampleReadIndex].address;
  uint8_t data = sampleBuffer[sampleReadIndex].data;
  uint8_t flags = sampleBuffer[sampleReadIndex].flags;
  sampleReadIndex = (sampleReadIndex + 1) & 0xFF;
  interrupts();

  bool reading = (flags & 0x01) != 0;
  bool opCodeFetch = (flags & 0x02) != 0;

  byte currentAction = reading ? 0 : 1;

  if (opCodeFetch || lastAction != currentAction || address != lastAddress + 1)
  {
    Serial.println();
    consecutiveActionCount = 1;
    writef("%08x : %c %04x %c %02hx", i, opCodeFetch ? '*' : ' ', address, reading ? 'r' : 'W', data);
  }
  else
  {
    consecutiveActionCount++;
    writef(" %02hx", data);
  }

  lastAction = currentAction;
  lastAddress = address;

  // If this was the fetching of a new opcode, make a note of the opcode
    
  if (opCodeFetch)
  {
    currentOpAddress = address;
    currentAddressMode = OPCODES[data].addressMode;
    currentOpCode = data;

    operand = 0;
    operandSize = ADDRESSMODES[currentAddressMode].operandSize;
    operandBytesNeeded = OPERAND_BITMASK[operandSize];
  }

  // If we're reading a byte that's part of the operand record that.

  if (reading && operandSize > 0 && currentOpAddress < address && address <= (currentOpAddress + operandSize))
  {
    switch (address - currentOpAddress)
    {
      case 1:
        operandBytesNeeded &= ~1;
        operand |= data;
        break;
  
      case 2:
        operandBytesNeeded &= ~2;
        operand |= ((unsigned int)data << 8);
        break;
    }
  }

  // If we have the full instruction, decode it and print it

  if (operandBytesNeeded == 0)
  {
    Serial.write("           ", 11 - consecutiveActionCount * 3);
    Serial.write(OPCODES[currentOpCode].mnemonic);
    
    if (currentAddressMode == AM_REL)
      operand = currentOpAddress + ((char)operand) + 2;
    
    writef(ADDRESSMODES[currentAddressMode].form, operand);
    
    operandBytesNeeded = -1;
    lastAction = -1;
  }
}

/*******************************
   Measure the clock frequency

   This is limited by the clock on the Arduino's ability to handle the interrupt on each clock tick.

   On an Arduino with a clock of 16 MHz, this will accurately measure the speed up to  100 kHz
   
 *******************************/

volatile unsigned long tickCount = 0;

void countTick()
{
  tickCount++;
}

void measureClock()
{
  attachInterrupt(digitalPinToInterrupt(PHI0), countTick, RISING);
  
  tickCount = 0;
  unsigned long startTime = micros();

  delay(2000);

  noInterrupts();
  unsigned long elapsedCount = tickCount;
  interrupts();

  unsigned long elapsedTime = micros() - startTime;

  detachInterrupt(digitalPinToInterrupt(PHI0));

  if (elapsedTime < 1980000UL || elapsedCount > 202000UL)
  {
    Serial.println("500 INTERNAL ERROR - The clock is too fast to accurately measure");
  }
  else
  {
    double frequency = 1000000.0 * (double)elapsedCount / (double)elapsedTime;

    char str_frequency[16];
    dtostrf(frequency, 1, 0, str_frequency);
    writelnf("200 OK - Counted %lu clock pulses in %lu ms for a clock frequency of %s Hz",
             elapsedCount, elapsedTime / 1000UL, str_frequency);
  }
}

/*********************
   EEPROM Programmer
 *********************/

void readProm(unsigned int address, byte data[], int dataLength)
{
  disableProcessor();

  for (int i = 0; i < dataLength; i++)
  {
    setAddress(address + i);

    data[i] = getData();
  }

  clearAddressData();

  enableProcessor(); 
}

void writeProm(unsigned int address, byte data[], int dataLength)
{
  unsigned int page = address >> 5;

  disableProcessor();

  digitalWriteFast(PROM_OUTPUT_DISABLE, HIGH);

  for (int i = 0; i < dataLength; i++)
  {
    if (((address + i)  >> 5) != page)
    {
      // crossed a page boundary, wait 11 ms for the write cycle to complete.
      delay(11);

      page = address >> 5;
    }

    writeByteToProm(address + i, data[i]);
  }

  clearAddressData();

  delay(11);

  digitalWriteFast(PROM_OUTPUT_DISABLE, LOW);

  enableProcessor();
}

void writeByteToProm(unsigned int address, byte data)
{
    setAddressData(address, data);

    digitalWriteFast(PROM_WRITE_DISABLE, LOW);

    delayMicroseconds(1);

    digitalWriteFast(PROM_WRITE_DISABLE, HIGH);

    delayMicroseconds(1);
}

void lockProm(unsigned int addressOffset)
{
  disableProcessor();

  digitalWriteFast(PROM_OUTPUT_DISABLE, HIGH);

  writeByteToProm(addressOffset + 0x5555, 0xAA);
  writeByteToProm(addressOffset + 0x2AAA, 0x55);
  writeByteToProm(addressOffset + 0x5555, 0xA0);

  clearAddressData();

  delay(11);

  digitalWriteFast(PROM_OUTPUT_DISABLE, LOW);

  enableProcessor();
}

void unlockProm(unsigned int addressOffset)
{
  disableProcessor();

  digitalWriteFast(PROM_OUTPUT_DISABLE, HIGH);

  writeByteToProm(addressOffset + 0x5555, 0xAA);
  writeByteToProm(addressOffset + 0x2AAA, 0x55);
  writeByteToProm(addressOffset + 0x5555, 0x80);
  writeByteToProm(addressOffset + 0x5555, 0xAA);
  writeByteToProm(addressOffset + 0x2AAA, 0x55);
  writeByteToProm(addressOffset + 0x5555, 0x20);

  clearAddressData();

  delay(11);

  digitalWriteFast(PROM_OUTPUT_DISABLE, LOW);

  enableProcessor();
}

/****************************
   Command argument parsing
 ****************************/

int TryParseUInt8(byte &result)
{
  char *pcArg = strtok(NULL, DELIMS);

  if (pcArg == NULL || strlen(pcArg) == 0)
    return E_MISSING;

  errno = 0;

  long value = strtol(pcArg, NULL, 0);

  if (errno != 0 || value < 0x00 || value > 0xFF || !isDigit(pcArg[0]))
    return E_INVALID;

  result = value;

  return E_OK;
}

int TryParseUInt16(unsigned int &result)
{
  char *pcArg = strtok(NULL, DELIMS);

  if (pcArg == NULL || strlen(pcArg) == 0)
    return E_MISSING;

  errno = 0;

  long value = strtol(pcArg, NULL, 0);

  if (errno != 0 || value < 0x0000 || value > 0xFFFF || !isDigit(pcArg[0]))
    return E_INVALID;

  result = value;

  return E_OK;
}

int TryParseInt32(long &result)
{
  char *pcArg = strtok(NULL, DELIMS);

  if (pcArg == NULL || strlen(pcArg) == 0)
    return E_MISSING;

  errno = 0;

  long value = strtol(pcArg, NULL, 0);

  if (errno != 0 || !isDigit(pcArg[0]))
    return E_INVALID;

  result = value;

  return E_OK;
}

int TryParseByteArray(byte *pBuffer, int &length)
{
  if (pBuffer == NULL || length == 0)
    return E_INVALID;

  char *pcArg = strtok(NULL, DELIMS);

  if (pcArg == NULL || strlen(pcArg) == 0)
    return E_MISSING;

  int stringLength = strlen(pcArg);

  if (stringLength % 2 > 0 || stringLength > length * 2)
    return E_INVALID;

  length = stringLength >> 1;

  for (int i = 0; i < length; i++)
  {
    byte nibble = ntob(pcArg[i * 2]);

    if (nibble == 0xFF)
      return E_INVALID;

    pBuffer[i] = nibble << 4;

    nibble = ntob(pcArg[i * 2 + 1]);

    if (nibble == 0xFF)
      return E_INVALID;

    pBuffer[i] |= nibble;
  }

  return E_OK;
}

byte ntob(char c)
{
  if ('0' <= c && c <= '9')
    return c - '0';

  if ('A' <= c && c <= 'F')
    return c - 'A' + 10;

  if ('a' <= c && c <= 'f')
    return c - 'a' + 10;

  return 0xff;
}

/************
   Commands
 ************/

const command COMMANDS[] = {
  { "help", help },
  
  { "reset", reset },

  { "disableProcessor", disableProcessor },
  { "enableProcessor", enableProcessor },

  { "setAddress", setAddress },

  { "measureClock", measureClock },
  { "startClock", startClock },
  { "stopClock", stopClock },

  { "peek", peek },
  { "poke", poke },

  { "monitor", monitor },

  { "readProm", readProm },
  { "writeProm", writeProm },
  { "lockProm", lockProm },
  { "unlockProm", unlockProm },

  { NULL, NULL }
};

void help()
{
  Serial.println("200 OK - Available Commands:");

  for (int i = 0; COMMANDS[i].name != NULL; i++)
  {
    Serial.print('\t');
    Serial.println(COMMANDS[i].name);
  }
}

void reset()
{
  if (tryReset())
  {
    Serial.println("200 OK");
  }
  else
  {
    Serial.println("500 INTERNAL ERROR - Timeout while waiting for clock");
  }
}

void startClock()
{
  long frequency = 0;

  if (TryParseInt32(frequency) != E_OK || frequency <= 0)
  {
    Serial.println("400 BAD REQUEST - Frequency was missing or invalid");
    return;
  }

  switch (startInternalClock(frequency))
    {
      case E_OK:
        writelnf("200 OK - Generating internal clock at %ld Hz", frequency);
        break;
      case E_NOT_SUPPORTED:
        Serial.println("500 INTERNAL ERROR - The clock pin has been changed to an unsupported pin. Generating an internal clock is only supported on Timer 3 on channel A or B.");
        break;
      case E_CLOCK_DETECTED:
        Serial.println("409 CONFLICT - An external clock has been detected.");
        break;
      case E_OUT_OF_RANGE:
        Serial.println("400 BAD REQUEST - The frequency requested exceeds the maximum frequency of the clock generator.");
        break;
      default:
        Serial.println("500 INTERNAL ERROR - An unexpected error occurred");
        break;
    }
}

void stopClock()
{
  bool wasClockRunning = internalClock;

  stopInternalClock();
        
  if (wasClockRunning)
  {
    Serial.println("200 OK - Clock has been stopped");
  }
  else
  {
    Serial.println("200 OK - Internal clock was not running");
  }
}

void setAddress()
{
  unsigned int address = 0;

  if (TryParseUInt16(address) != E_OK)
  {
    Serial.println("400 BAD REQUEST - Address was missing or invalid");
    return;
  }

  byte data = 0;

  switch (TryParseUInt8(data))
  {
    case E_OK:
      setAddressData(address, data);
      writelnf("200 OK - Address set to 0x%04x (%u), Data set to 0x%02hx (%hu)", address, address, data, data);
      break;

    case E_MISSING:
      setAddress(address);
      writelnf("200 OK - Address set to 0x%04x (%u)", address, address);
      break;

    case E_INVALID:
      Serial.println("400 BAD REQUEST - Data value was invalid");
      break;
  }
}

void peek()
{
  unsigned int address = 0;

  if (TryParseUInt16(address) != E_OK)
  {
    Serial.println("400 BAD REQUEST - Address was missing or invalid");
    return;
  }

  byte data;

  if (peek(address, data))
  {
    if (' ' <= data && data <= '~')
    {
      writelnf("200 OK - 0x%04x: 0x%02hx (%hu) '%c'", address, data, data, data);
    }
    else
    {
      writelnf("200 OK - 0x%04x: 0x%02hx (%hu)", address, data, data);
    }
  }
  else
  {
    Serial.println("500 INTERNAL ERROR - Timeout while waiting for clock");
  }
}

void poke()
{
  unsigned int address = 0;

  if (TryParseUInt16(address) != E_OK)
  {
    Serial.println("400 BAD REQUEST - Address was missing or invalid");
    return;
  }

  byte data = 0;

  if (TryParseUInt8(data) != E_OK)
  {
    Serial.println("400 BAD REQUEST - Data was missing or invalid");
    return;
  }

  if (poke(address, data))
  {
    Serial.println("200 OK");
  }
  else
  {
    Serial.println("500 INTERNAL ERROR - Timeout while waiting for clock");
  }
}

void monitor()
{

  if (!tryReset()) { 
    Serial.println("500 INTERNAL ERROR - Could not reset!");
    return;
  }

  monitorEnabled = true;

  Serial.println("200 OK - Monitoring bus, send any key to stop...");

  size_t index = 0;

  while(monitorEnabled) {
    while (sampleReadIndex != sampleWriteIndex) {
      processSample(index++);
    }
    if (Serial.available()) {
      monitorEnabled = false;
    }
  }

}

void readProm()
{
  unsigned int address = 0;
  unsigned int dataLength = 0;

  if (TryParseUInt16(address) != E_OK)
  {
    Serial.println("400 BAD REQUEST - Address was missing or invalid");
    return;
  }

  if (TryParseUInt16(dataLength) != E_OK)
  {
    Serial.println("400 BAD REQUEST - Length was missing or invalid");
    return;
  }

  long lastAddress = (long) address + (long) dataLength - 1;
    
  if (lastAddress > 65535L)
  {
    Serial.println("400 BAD REQUEST - Address + Length must be less than 65536.");
    return;
  }

  Serial.println("200 OK");

  byte buffer[16];

  int pages = (dataLength + 15) >> 4;

  for (int page = 0; page < pages; page++)
  {
    unsigned int pageStart = address + page * 16;

    readProm(pageStart, buffer, 16);

    writelnf("%04x: %02hx %02hx %02hx %02hx %02hx %02hx %02hx %02hx  %02hx %02hx %02hx %02hx %02hx %02hx %02hx %02hx",
      pageStart, buffer[0], buffer[1], buffer[2], buffer[3], buffer[4], buffer[5], buffer[6], buffer[7],
      buffer[8], buffer[9], buffer[10], buffer[11], buffer[12], buffer[13], buffer[14], buffer[15]);
  }
}

void writeProm()
{
  unsigned int address = 0;

  int dataLength = 256;

  byte data[dataLength];

  if (TryParseUInt16(address) != E_OK)
  {
    Serial.println("400 BAD REQUEST - Address was missing or invalid");
    return;
  }

  if (TryParseByteArray(data, dataLength) != E_OK)
  {
    Serial.println("400 BAD REQUEST - Data was missing or invalid");
    return;
  }

  writeProm(address, data, dataLength);

  Serial.println("200 OK");
}

void lockProm()
{
  unsigned int address = 0;

  switch (TryParseUInt16(address))
  {
    case E_MISSING:
      address = 0x8000;
      /* intentional fall-through */

    case E_OK:
      Serial.println("200 OK");
      lockProm(address);
      break;

    default:
      Serial.println("400 BAD REQUEST - EEPROM Base Address was invalid");
  }
}

void unlockProm()
{
  unsigned int address = 0;

  switch (TryParseUInt16(address))
  {
    case E_MISSING:
      address = 0x8000;
      /* intentional fall-through */

    case E_OK:
      Serial.println("200 OK");
      unlockProm(address);
      break;

    default:
      Serial.println("400 BAD REQUEST - EEPROM Base Address was invalid");
  }
}

void loop()
{
  Serial.print("> ");

  char buffer[256];

  int length = getLine(buffer, 256);

  if (length <= 0) return;

  char* command = strtok(buffer, " \t");

  if (isNullOrEmpty(command)) return;

  bool commandFound = false;

  for (int i = 0; COMMANDS[i].name != NULL; i++)
  {
    if (stricmp(command, COMMANDS[i].name) != 0) continue;

    commandFound = true;

    COMMANDS[i].execute();
  }

  if (!commandFound)
  {
    Serial.print("Unknown Command: ");
    Serial.println(command);
    Serial.println("Type 'help' for a list of commands.");
  }

  Serial.println();
}
