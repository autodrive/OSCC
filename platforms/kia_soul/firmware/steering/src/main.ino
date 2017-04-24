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

// Steering control ECU firmware
// 2014 Kia Soul Motor Driven Power Steering (MDPS) system


#include <SPI.h>
#include "mcp_can.h"
#include "gateway_protocol_can.h"
#include "steering_protocol_can.h"
#include "PID.h"
#include "DAC_MCP49xx.h"
#include "serial.h"
#include "can.h"
#include "time.h"
#include "debug.h"

#include "globals.h"
#include "steering_module.h"
#include "init.h"
#include "steering_control.h"
#include "communications.h"


void setup( )
{
    memset( &rx_frame_steering_command,
            0,
            sizeof(rx_frame_steering_command) );

    init_pins( );

    #ifdef DEBUG
        init_serial( );
    #endif

    init_can( can );

    publish_timed_tx_frames( );

    control_state.enabled = false;
    control_state.emergency_stop = false;

    override_flags.wheel_active = false;
    override_flags.voltage = 0;
    override_flags.voltage_spike_a = 0;
    override_flags.voltage_spike_b = 0;

    // Initialize the Rx timestamps to avoid timeout warnings on start up
    rx_frame_steering_command.timestamp = GET_TIMESTAMP_MS( );

    pid_zeroize( &pid, PARAM_PID_WINDUP_GUARD );

    DEBUG_PRINTLN( "init: pass" );
}


void loop( )
{
    // checks for CAN frames, if yes, updates state variables
    can_frame_s rx_frame;
    int ret = check_for_rx_frame( can, &rx_frame );

    if( ret == RX_FRAME_AVAILABLE )
    {
        handle_ready_rx_frame( &rx_frame );
    }

    // publish all report CAN frames
    publish_timed_tx_frames( );

    // check all timeouts
    check_rx_timeouts( );

    uint32_t current_timestamp_us = GET_TIMESTAMP_US();

    uint32_t deltaT = get_time_delta( control_state.timestamp_us,
                                      current_timestamp_us );

    if ( deltaT > 50000 )
    {

        control_state.timestamp_us = current_timestamp_us;

        bool override = check_driver_steering_override( );

        if ( override == true
             && control_state.enabled == true )
        {
            override_flags.wheel_active = true;
            disable_control( );
        }
        else if ( control_state.enabled == true )
        {
            // Calculate steering angle rates (degrees/microsecond)
            float steering_angle_rate =
                ( steering_state.steering_angle -
                  steering_state.steering_angle_last ) / 0.05;

            float steering_angle_rate_target =
                ( steering_state.steering_angle_target -
                  steering_state.steering_angle ) / 0.05;

            // Save the angle for next iteration
            steering_state.steering_angle_last =
                steering_state.steering_angle;

            steering_angle_rate_target =
                constrain( ( float )steering_angle_rate_target,
                           ( float )-PARAM_STEERING_ANGLE_RATE_MAX,
                           ( float )PARAM_STEERING_ANGLE_RATE_MAX );

            pid.proportional_gain = PARAM_PID_PROPORTIONAL_GAIN;
            pid.integral_gain = PARAM_PID_INTEGRAL_GAIN;
            pid.derivative_gain = PARAM_PID_DIFFERENTIAL_GAIN;

            pid_update(
                    &pid,
                    steering_angle_rate_target,
                    steering_angle_rate,
                    0.050 );

            float control = pid.control;

            control = constrain( ( float ) control,
                                 ( float ) -1500.0f,
                                 ( float ) 1500.0f );

            struct torque_spoof_t torque_spoof;

            calculate_torque_spoof( control, &torque_spoof );

            torque_sum = (uint8_t) ( torque_spoof.low + torque_spoof.high );

            dac.outputA( torque_spoof.low );
            dac.outputB( torque_spoof.high );
        }
        else
        {
            override_flags.wheel_active = false;

            pid_zeroize( &pid, PARAM_PID_WINDUP_GUARD );
        }
    }
}