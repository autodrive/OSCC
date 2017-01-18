/* Copyright (c) 2016 PolySync Technologies, Inc.  All Rights Reserved. */
/*                                                                      */
/* This file is part of Open Source Car Control (OSCC).                 */
/*                                                                      */
/* OSCC is free software: you can redistribute it and/or modify         */
/* it under the terms of the GNU General Public License as published by */
/* the Free Software Foundation, either version 3 of the License, or    */
/* (at your option) any later version.                                  */
/*                                                                      */
/* OSCC is distributed in the hope that it will be useful,              */
/* but WITHOUT ANY WARRANTY; without even the implied warranty of       */
/* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the        */
/* GNU General Public License for more details.                         */
/*                                                                      */
/* You should have received a copy of the GNU General Public License    */
/* along with OSCC.  If not, see <http://www.gnu.org/licenses/>.        */
/************************************************************************/

// Throttle control ECU firmware
// Firmware for control of 2014 Kia Soul throttle system
// Component
//    Arduino Uno
//    OSCC Sensor Interface Board V1
// J Hartung, 2015


#include <SPI.h>
#include <PID_v1.h>
#include "mcp_can.h"
#include "can_frame.h"
#include "control_protocol_can.h"
#include "DAC_MCP49xx.h"





// *****************************************************
// static global types/macros
// *****************************************************

#define PSYNC_DEBUG_FLAG

//
#ifdef PSYNC_DEBUG_FLAG
    #define DEBUG_PRINT(x)  Serial.println(x)
#else
    #define DEBUG_PRINT(x)
#endif

// set CAN_CS to pin 10 for CAN 
#define CAN_CS 10

#define CAN_BAUD (CAN_500KBPS)

//
#define SERIAL_DEBUG_BAUD (115200)

//
#define CAN_INIT_RETRY_DELAY (50)

// ms
#define PS_CTRL_RX_WARN_TIMEOUT (250)

//
#define GET_TIMESTAMP_MS() ((uint32_t) millis())

// set up pins for interface with DAC (MCP4922)

#define DAC_CS                9  // Chip select pin

#define SIGNAL_INPUT_A        A0  // Sensing input for the DAC output (high values)

#define SIGNAL_INPUT_B        A1  // Green wire from the torque sensor (low values)

#define SPOOF_SIGNAL_A        A2  // Sensing input for the DAC output (high values)

#define SPOOF_SIGNAL_B        A3  // Blue wire from the torque sensor (low values)

#define SPOOF_ENGAGE          6   // Signal interrupt (relay) for spoofed torque values


// Threshhold to detect when a person is pressing accelerator
#define PEDAL_THRESH 1000


// *****************************************************
// static global data
// *****************************************************


DAC_MCP49xx dac( DAC_MCP49xx::MCP4922, 9 );     // DAC model, SS pin, LDAC pin

// construct the CAN shield object
MCP_CAN CAN(CAN_CS);                            // Set CS pin for the CAN shield

//
static uint32_t last_update_ms;

//
static can_frame_s rx_frame_ps_ctrl_throttle_command;

//
static can_frame_s tx_frame_ps_ctrl_throttle_report;





// *****************************************************
// static declarations
// *


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


// uses last_update_ms, corrects for overflow condition
static void get_update_time_ms(
                const uint32_t * const time_in,
                        uint32_t * const delta_out )
{
    // check for overflow
    if( last_update_ms < (*time_in) )
    {
            // time remainder, prior to the overflow
            (*delta_out) = (UINT32_MAX - (*time_in));
    
            // add time since zero
            (*delta_out) += last_update_ms;
        }
    else
    {
            // normal delta
            (*delta_out) = (last_update_ms - (*time_in));
        }
}


static void init_serial( void ) 
{
    Serial.begin(115200);
}

static void init_can ( void ) 
{
    // wait until we have initialized
    while( CAN.begin(CAN_BAUD) != CAN_OK )
    {
        // wait a little
        delay( CAN_INIT_RETRY_DELAY );
    }

    // debug log
    DEBUG_PRINT( "init_can: pass" );

}


// set up values for use in the steering control system
uint16_t PSensL_current,        // Current measured accel sensor values
         PSensH_current,
         PSpoofH,               // Current spoofing values
         PSpoofL;

can_frame_s can_frame;          // CAN message structs

bool controlEnabled;

int local_override = 0;
         
double pedalPosition_target,
       pedalPosition;
       
uint8_t incomingSerialByte;




/* ====================================== */
/* ============== CONTROL =============== */
/* ====================================== */


// A function to enable SCM to take control
void enableControl() 
{
	// Do a quick average to smooth out the noisy data
	static int AVG_max = 20;  // Total number of samples to average over
	long sum_sensA_samples = 0;
	long sum_sensB_samples = 0;

	for (int i = 0; i < AVG_max; i++) 
	{
		sum_sensA_samples += analogRead(SIGNAL_INPUT_A);
		sum_sensB_samples += analogRead(SIGNAL_INPUT_B);
	}

	uint16_t avg_sensA_sample = (sum_sensA_samples / AVG_max) << 2;
	uint16_t avg_sensB_sample = (sum_sensB_samples / AVG_max) << 2;

	// Write measured torque values to DAC to avoid a signal discontinuity when the SCM takes over
     dac.outputA( avg_sensA_sample );
     dac.outputB( avg_sensB_sample );

	// TODO: check if the DAC value and the sensed values are the same. If not, return an error and do NOT enable the sigint relays.

	// Enable the signal interrupt relays
	digitalWrite(SPOOF_ENGAGE, HIGH);

	controlEnabled = true;

	DEBUG_PRINT("Control enabled");
}


// A function to disable SCM control
void disableControl() 
{
	// Do a quick average to smooth out the noisy data
	static int AVG_max = 20;  // Total number of samples to average over
	long sum_sensA_samples = 0;
	long sum_sensB_samples = 0;

	for (int i = 0; i < AVG_max; i++) 
	{
		sum_sensA_samples += analogRead(SIGNAL_INPUT_A) << 2;
		sum_sensB_samples += analogRead(SIGNAL_INPUT_B) << 2;
	}

	uint16_t avg_sensA_sample = sum_sensA_samples / AVG_max;
	uint16_t avg_sensB_sample = sum_sensB_samples / AVG_max;

	// Write measured torque values to DAC to avoid a signal discontinuity when the SCM relinquishes control
     dac.outputA( avg_sensA_sample );
     dac.outputB( avg_sensB_sample );

	// Disable the signal interrupt relays
	digitalWrite(SPOOF_ENGAGE, LOW);

	controlEnabled = false;

	DEBUG_PRINT("Control disabled");
}

void calculatePedalSpoof(float pedalPosition) {
  
  // values calculated with min/max calibration curve and hand tuned for neutral balance
  // DAC requires 12-bit values, (4096steps/5V = 819.2 steps/V)
  PSpoofL = 819.2*(0.0004*pedalPosition + 0.366);    
  PSpoofH = 819.2*(0.0008*pedalPosition + 0.732);
  PSpoofL = constrain(PSpoofL, 0, 1800); // range = 300 - ~1750
  PSpoofH = constrain(PSpoofH, 0, 3500); // range = 600 - ~3500

    //Serial.print("PSPOOF_LOW:");
    //Serial.print(PSpoofL);
    //Serial.print("PSPOOF_LOW");
    //Serial.println(PSpoofH);
  
}


/* ====================================== */
/* =========== COMMUNICATIONS =========== */
/* ====================================== */

// A function to parse incoming serial bytes
void processSerialByte( uint8_t incomingSerialByte) 
{
  
  if (incomingSerialByte == 'a') {                  // accelerate
    pedalPosition_target += 10;
  }
  if (incomingSerialByte == 'd') {                  // deaccelerate 
    pedalPosition_target -= 10;
  }
  if (incomingSerialByte == 's') {                  // return to center
    pedalPosition_target = 0;
  }
  if (incomingSerialByte == 'p') {                  // enable/disable control
      if( !controlEnabled ) 
      {   
          controlEnabled = true;
          enableControl();
      }   

      // disable control from the PolySync interface
      else if( controlEnabled ) 
      {   
          controlEnabled = false;
          disableControl();
      }   

  }
}


//
static void publish_ps_ctrl_throttle_report( void )
{
    // cast data
    ps_ctrl_throttle_report_msg * const data =
            (ps_ctrl_throttle_report_msg*) tx_frame_ps_ctrl_throttle_report.data;

    // set frame ID
    tx_frame_ps_ctrl_throttle_report.id = (uint32_t) (PS_CTRL_MSG_ID_THROTTLE_REPORT);

    // set DLC
    tx_frame_ps_ctrl_throttle_report.dlc = 8; //TODO

    // set override flag
    data->override = local_override;

    //// Set Pedal Command (PC)
    //data->pedal_command = 

    //// Set Pedal Output (PO)
    //data->pedal_output = max()

    // publish to control CAN bus
    CAN.sendMsgBuf(
            tx_frame_ps_ctrl_throttle_report.id,
            0, // standard ID (not extended)
            tx_frame_ps_ctrl_throttle_report.dlc,
            tx_frame_ps_ctrl_throttle_report.data );

    // update last publish timestamp, ms
    tx_frame_ps_ctrl_throttle_report.timestamp = last_update_ms;
}


//
static void publish_timed_tx_frames( void )
{
    // local vars
    uint32_t delta = 0;


    // get time since last publish
    get_update_time_ms( &tx_frame_ps_ctrl_throttle_report.timestamp, &delta );

    // check publish interval
    if( delta >= PS_CTRL_THROTTLE_REPORT_PUBLISH_INTERVAL )
    {
        // publish frame, update timestamp
        publish_ps_ctrl_throttle_report();
    }
}



static void process_ps_ctrl_throttle_command( const uint8_t * const rx_frame_buffer )
{

    // cast control frame data
    const ps_ctrl_throttle_command_msg * const control_data =
            (ps_ctrl_throttle_command_msg*) rx_frame_buffer;


    bool enabled = control_data->enabled == 1;

    // enable control from the PolySync interface
    if( enabled == 1 && !controlEnabled ) 
    {   
        controlEnabled = true;
        enableControl();
    }   

    // disable control from the PolySync interface
    if( enabled == 0 && controlEnabled ) 
    {   
        controlEnabled = false;
        disableControl();
    }   

    rx_frame_ps_ctrl_throttle_command.timestamp = GET_TIMESTAMP_MS();

    pedalPosition_target = control_data->pedal_command / 24 ;
    DEBUG_PRINT(pedalPosition_target);

}

// A function to parse CAN data into useful variables
void handle_ready_rx_frames(void) {

    // local vars
    can_frame_s rx_frame;

    if( CAN.checkReceive() == CAN_MSGAVAIL )
    {
        memset( &rx_frame, 0, sizeof(rx_frame) );

        // update timestamp
        rx_frame.timestamp = last_update_ms;

        // read frame
        CAN.readMsgBufID(
                (INT32U*) &rx_frame.id,
                (INT8U*) &rx_frame.dlc,
                (INT8U*) rx_frame.data );

        // check for a supported frame ID
        if( rx_frame.id == PS_CTRL_THROTTLE_COMMAND_ID )
        {
            // process status1
            process_ps_ctrl_throttle_command( rx_frame.data );
        }
    }

}


//
static void check_rx_timeouts( void )
{
    // local vars
    uint32_t delta = 0;

    // get time since last receive
    get_update_time_delta_ms( 
			rx_frame_ps_ctrl_throttle_command.timestamp, 
			GET_TIMESTAMP_MS(), 
			&delta );

    // check rx timeout
    if( delta >= PS_CTRL_RX_WARN_TIMEOUT ) 
    {
        // disable control from the PolySync interface
        if( controlEnabled ) 
        {
            Serial.println("control disabled: timeout");
            disableControl();
        }
    }
}


/* ====================================== */
/* ================ SETUP =============== */
/* ====================================== */

void setup() 
{
    // zero
    last_update_ms = 0;
    memset( &rx_frame_ps_ctrl_throttle_command, 0, sizeof(rx_frame_ps_ctrl_throttle_command) );

    // set up pin modes
    pinMode(DAC_CS, OUTPUT);
    pinMode(SIGNAL_INPUT_A, INPUT);
    pinMode(SIGNAL_INPUT_B, INPUT);
    pinMode(SPOOF_SIGNAL_A, INPUT);
    pinMode(SPOOF_SIGNAL_B, INPUT);
    pinMode(SPOOF_ENGAGE, OUTPUT);

    // initialize the DAC board
    digitalWrite(DAC_CS, HIGH);     // Deselect DAC CS

    // Initialize relay board
    digitalWrite(SPOOF_ENGAGE, LOW);

    init_serial();

    init_can();

    publish_ps_ctrl_throttle_report();


    // update last Rx timestamps so we don't set timeout warnings on start up
    rx_frame_ps_ctrl_throttle_command.timestamp = GET_TIMESTAMP_MS();

    // update the global system update timestamp, ms
    last_update_ms = GET_TIMESTAMP_MS();

    // debug log
    DEBUG_PRINT( "init: pass" );


}


/* ====================================== */
/* ================ LOOP ================ */
/* ====================================== */

void loop()
{

    // update the global system update timestamp, ms
    last_update_ms = GET_TIMESTAMP_MS();

    handle_ready_rx_frames();

    publish_timed_tx_frames();

    check_rx_timeouts();

    // update state variables
    PSensH_current = analogRead(SIGNAL_INPUT_A) << 2;  //10 bit to 12 bit
    PSensL_current = analogRead(SIGNAL_INPUT_B) << 2;

    
    // if someone is pressing the throttle pedal disable control
    if ( ( PSensL_current + PSensH_current) / 2 > PEDAL_THRESH ) {
        disableControl();
        local_override = 1;

    } 
    else 
    {
        local_override = 0;
    }

    // Read and parse incoming serial commands
    if ( Serial.available() > 0 ) 
    {
        uint8_t incomingSerialByte = Serial.read();
        processSerialByte( incomingSerialByte );
    }



    // now that we've set control status, do throttle if we are in control
    if (controlEnabled) {

        calculatePedalSpoof(pedalPosition_target);

        dac.outputA( PSpoofH );
        dac.outputB( PSpoofL );

    }

}
