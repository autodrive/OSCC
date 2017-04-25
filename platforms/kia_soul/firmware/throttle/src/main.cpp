// Throttle control ECU firmware
// 2014 Kia Soul throttle system


#include <Arduino.h>
#include <SPI.h>
#include "arduino_init.h"
#include "mcp_can.h"
#include "DAC_MCP49xx.h"
#include "serial.h"
#include "can.h"
#include "time.h"
#include "debug.h"

#include "globals.h"
#include "throttle_module.h"
#include "init.h"
#include "communications.h"
#include "throttle_control.h"


int main( void )
{
    init_arduino( );

    init_structs( );

    init_pins( );

    init_interfaces( );

    publish_throttle_report( );

    // update last Rx timestamps so we don't set timeout warnings on start up
    rx_frame_throttle_command.timestamp = GET_TIMESTAMP_MS( );

    DEBUG_PRINTLN( "init: pass" );


    while( true )
    {
        can_frame_s rx_frame;
        can_status_t ret = check_for_rx_frame( can, &rx_frame );

        if( ret == CAN_RX_FRAME_AVAILABLE )
        {
            handle_ready_rx_frame( &rx_frame );
        }

        publish_timed_report( );

        check_rx_timeouts( );

        throttle_state.accel_pos_sensor_low = analogRead( PIN_ACCELERATOR_POSITION_SENSOR_HIGH ) << 2; //10 bit to 12 bit
        throttle_state.accel_pos_sensor_high = analogRead( PIN_ACCELERATOR_POSITION_SENSOR_LOW ) << 2;

        check_accelerator_override( );

        if ( throttle_control_state.enabled == true )
        {
            accel_spoof_t accel_spoof;

            calculate_accelerator_spoof(
                    throttle_state.accel_pos_target,
                    &accel_spoof );

            dac.outputA( accel_spoof.high );
            dac.outputB( accel_spoof.low );
        }
    }

    return 0;
}