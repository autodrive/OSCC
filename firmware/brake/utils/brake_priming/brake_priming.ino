#include <SPI.h>
#include "common.h"

#define DEBUG_PRINT(x)  Serial.println(x)

// MOSFET pin (digital) definitions ( MOSFETs control the solenoids )
// pins are not perfectly sequential because the clock frequency of certain pins is different.
const byte PIN_SLAFL = 5;      // front left actuation
const byte PIN_SLAFR = 7;      // front right actuation
// Duty cycles of pins 6, 7, and 8 controlled by timer 4 (TCCR4B)
const byte PIN_SLRFL = 6;      // front left return
const byte PIN_SLRFR = 8;      // front right return
const byte PIN_SMC = 2;      // master cylinder solenoids (two of them)

const byte PIN_PUMP = 49;     // accumulator pump motor

// brake spoofer relay pin definitions
const byte PIN_BRAKE_SWITCH_1 = 48;

// sensor pin (analog) definitions
const byte PIN_PACC = 9;       // pressure accumulator sensor
const byte PIN_PMC1 = 10;      // pressure master cylinder sensor 1
const byte PIN_PMC2 = 11;      // pressure master cylinder sensor 2
const byte PIN_PRL = 12;       // pressure rear left sensor
const byte PIN_PFR = 13;       // pressure front right sensor
const byte PIN_PFL = 14;       // pressure front left sensor
const byte PIN_PRR = 15;       // pressure rear right sensor

// the following are guesses, these need to be debugged/researched
const double ZERO_PRESSURE = 0.48;        // The voltage the sensors read when no pressure is present
const double MIN_PACC = 2.3;              // minumum accumulator pressure to maintain
const double MAX_PACC = 2.4;              // max accumulator pressure to maintain
const double PEDAL_THRESH = 0.5;          // Pressure for pedal interference

int PACC_CURRENT_CYCLE_COUNT = 0;    // How many times has the accumulator been pressurized and purged
int PACC_CYCLE_TARGET = 10;           // Number of times to presserize and purge the accumulator

uint8_t incomingSerialByte;
static uint32_t timestamp;

// convert the ADC reading (which goes from 0 - 1023) to a voltage (0 - 5V):
float convertToVoltage(int input) {
    return input * (5.0 / 1023.0);
}

// accumulator structure
struct Accumulator {
    float _pressure = 0.0;    // pressure is initliazed at 0
    byte _sensorPin = 99;     // set to 99 to avoid and accidental assignments
    byte _controlPin = 99;
    Accumulator( byte sensorP, byte relayP );

    // turn relay on or off
    void pumpOn()
    {
      digitalWrite(_controlPin, HIGH);
    }

    void pumpOff()
    {
      digitalWrite(_controlPin, LOW);
    }

    // maintain accumulator pressure
    void maintainPressure()
    {
      _pressure = convertToVoltage(analogRead(_sensorPin));

      if( _pressure < MIN_PACC )
      {
          pumpOn();
          Serial.println(_pressure);
      }

      if( _pressure > MAX_PACC )
      {
          pumpOff();
      }
    }
};

// accumulator constructor
Accumulator::Accumulator( byte sensorPin, byte controlPin )
{
  _sensorPin = sensorPin;
  _controlPin = controlPin;

  pinMode( _controlPin, OUTPUT ); // set pinmode to OUTPUT

  // initialize pump to off
  pumpOff();
}

// master Solenoid structure
struct SMC {
    float _pressure1 = 0.0; // Initialize pressures to 0.0 to avoid false values
    float _pressure2 = 0.0;
    byte _sensor1Pin = 99;
    byte _sensor2Pin = 99;
    byte _controlPin = 99;

    SMC( byte sensor1Pin, byte sensor2Pin, byte controlPin );

    void solenoidsClose()
    {
        analogWrite( _controlPin, 255 );
    }

    void solenoidsOpen()
    {
        analogWrite( _controlPin, 0 );
    }
};

SMC::SMC( byte sensor1Pin, byte sensor2Pin, byte controlPin )
{
  _sensor1Pin = sensor1Pin;
  _sensor2Pin = sensor2Pin;
  _controlPin = controlPin;

  pinMode( _controlPin, OUTPUT );  // We're writing to pin, set as an output

  solenoidsOpen();
}

// wheel structure
struct Brakes {
    float _pressureLeft = 0.0;            // last known right-side pressure
    float _pressureRight = 0.0;           // last known left-side pressure
    byte _sensorPinLeft = 99;             // pin associated with left-side  pressure sensor
    byte _sensorPinRight = 99;            // pin associated with right-side pressure sensors
    byte _solenoidPinLeftA = 99;          // pin associated with MOSFET, associated with actuation solenoid
    byte _solenoidPinRightA = 99;         // pin associated with MOSFET, associated with return solenoid
    byte _solenoidPinLeftR = 99;          // pin associated with MOSFET, associated with actuation solenoid
    byte _solenoidPinRightR = 99;         // pin associated with MOSFET, associated with return solenoid
    bool _increasingPressure = false;     // used to track if pressure should be increasing
    bool _decreasingPressure = false;     // used to track if pressure should be decreasing
    unsigned long _previousMillis = 0;    // will store last time solenoid was updated

    Brakes( byte sensorPinLeft, byte sensorPinRight, byte solenoidPinLeftA, byte solenoidPinRightA, byte solenoidPinLeftR, byte solenoidPinRightR );

    void depowerSolenoids()
    {
      analogWrite(_solenoidPinLeftA, 0);
      analogWrite(_solenoidPinRightA, 0);
      analogWrite(_solenoidPinLeftR, 0);
      analogWrite(_solenoidPinRightR, 0);
    }

    // fill pressure
    void powerSLA(int scaler)
    {
        analogWrite( _solenoidPinLeftA, scaler );
        analogWrite( _solenoidPinRightA, scaler );
    }

    void depowerSLA()
    {
        analogWrite( _solenoidPinLeftA, 0 );
        analogWrite( _solenoidPinRightA, 0 );
    }

    // spill pressure
    void powerSLR(int scaler)
    {
        analogWrite( _solenoidPinLeftR, scaler );
        analogWrite( _solenoidPinRightR, scaler );
    }

    void depowerSLR()
    {
        digitalWrite( _solenoidPinLeftR, LOW );
        digitalWrite( _solenoidPinRightR, LOW );
    }

    // take a pressure reading
    void updatePressure()
    {
      _pressureLeft = convertToVoltage( analogRead(_sensorPinLeft) );
      _pressureRight = convertToVoltage( analogRead(_sensorPinRight) );
    }
};

// brake constructor
Brakes::Brakes( byte sensorPLeft, byte sensorPRight, byte solenoidPinLeftA, byte solenoidPinRightA, byte solenoidPinLeftR, byte solenoidPinRightR ) {
  _sensorPinLeft = sensorPLeft;
  _sensorPinRight = sensorPRight;
  _solenoidPinLeftA = solenoidPinLeftA;
  _solenoidPinRightA = solenoidPinRightA;
  _solenoidPinLeftR = solenoidPinLeftR;
  _solenoidPinRightR = solenoidPinRightR;

  // initialize solenoid pins to off
  digitalWrite( _solenoidPinLeftA, LOW );
  digitalWrite( _solenoidPinRightA, LOW );
  digitalWrite( _solenoidPinLeftR, LOW );
  digitalWrite( _solenoidPinRightR, LOW );

  // set pinmode to OUTPUT
  pinMode( _solenoidPinLeftA, OUTPUT );
  pinMode( _solenoidPinRightA, OUTPUT );
  pinMode( _solenoidPinLeftR, OUTPUT );
  pinMode( _solenoidPinRightR, OUTPUT );
}

// Instantiate objects
Accumulator accumulator( PIN_PACC, PIN_PUMP );
SMC smc(PIN_PMC1, PIN_PMC2, PIN_SMC);
Brakes brakes = Brakes( PIN_PFL, PIN_PFR, PIN_SLAFL, PIN_SLAFR, PIN_SLRFL, PIN_SLRFR);

// corrects for overflow condition
static void get_update_time_delta_ms(
		const uint32_t time_in,
		const uint32_t last_update_time_ms,
		uint32_t * const delta_out )
{
    // check for overflow
    if( last_update_time_ms < time_in )
    {
		// time remainder, prior to the overflow
		(*delta_out) = (UINT32_MAX - time_in);

        // add time since zero
        (*delta_out) += last_update_time_ms;
    }
    else
    {
        // normal delta
        (*delta_out) = ( last_update_time_ms - time_in );
    }
}
static void init_serial( void )
{
    // Disable serial
    Serial.end();
    Serial.begin( SERIAL_BAUD );

    // Debug log
    DEBUG_PRINT( "init_serial: pass" );
}


// A function to parse incoming serial bytes
void processSerialByte() {

    if (incomingSerialByte == 'p') {                  // reset
        accumulator.pumpOn();
        DEBUG_PRINT("pump on");
    }
    if (incomingSerialByte == 'q') {                  // reset
        accumulator.pumpOff();
        DEBUG_PRINT("pump off");
    }
}

// the setup routine runs once when you press reset:
void setup( void )

{
    // set the PWM timers, above the acoustic range
    TCCR3B = (TCCR3B & 0xF8) | 0x02; // pins 2,3,5 | timer 3
    TCCR4B = (TCCR4B & 0xF8) | 0x02; // pins 6,7,8 | timer 4

    // relay boards are active low, set to high before setting output to avoid unintended energisation of relay
    digitalWrite( PIN_BRAKE_SWITCH_1, LOW );
    pinMode( PIN_BRAKE_SWITCH_1, OUTPUT );


    // Set up solenoids for priming
    smc.solenoidsOpen();
    brakes.depowerSLR();
    brakes.powerSLA(250);

    init_serial();

    timestamp = GET_TIMESTAMP_MS();
    PACC_CURRENT_CYCLE_COUNT = 0;

}

void loop()
{
    // read and parse incoming serial commands
    if( Serial.available() > 0 )
    {
        incomingSerialByte = Serial.read();
        processSerialByte();
    }

    // Cycle opening and closing solenoids in a loop while runnig the pump

    while( PACC_CURRENT_CYCLE_COUNT < PACC_CYCLE_TARGET )
    {
        uint32_t delta = 0;

        accumulator.maintainPressure();
        
        get_update_time_delta_ms(
                timestamp,
                GET_TIMESTAMP_MS(),
                &delta );

        if( delta < 5000 )
        {
            brakes.powerSLA(250);
            brakes.powerSLR(250);
            smc.solenoidsClose();
        }

        if( delta >= 5000 && delta < 10000 )
        {
            brakes.powerSLA(250);
            brakes.depowerSLR();
            smc.solenoidsOpen();
       }

        if( delta >= 10000 && delta < 15000 )
        {
            brakes.depowerSLR();
            brakes.depowerSLA();
            smc.solenoidsClose();

        }
        if (delta >= 15000 )
        {
            timestamp = GET_TIMESTAMP_MS();
            PACC_CURRENT_CYCLE_COUNT++;
        }
    }

    accumulator.pumpOff();
}
