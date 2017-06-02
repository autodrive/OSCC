/**
 * @file communications.h
 * @brief Communication functionality.
 *
 */


#ifndef _OSCC_KIA_SOUL_STEERING_COMMUNICATIONS_H_
#define _OSCC_KIA_SOUL_STEERING_COMMUNICATIONS_H_


/*
 * @brief Scalar value to convert angle reported by OBD to a floating-point value.
 *
 */
#define ANGLE_SCALAR ( 0.01 )


/*
 * @brief Amount of time after controller command that is considered a
 *        timeout. [milliseconds]
 *
 */
#define COMMAND_TIMEOUT_IN_MSEC ( 250 )


/*
 * @brief Amount of time after a Chassis State 1 report that is considered a
 *        timeout. [milliseconds]
 *
 */
#define CHASSIS_STATE_1_REPORT_TIMEOUT_IN_MSEC ( 500 )


// ****************************************************************************
// Function:    publish_reports
//
// Purpose:     Publish all valid reports to CAN bus.
//
// Returns:     void
//
// Parameters:  void
//
// ****************************************************************************
void publish_reports( void );


// ****************************************************************************
// Function:    check_for_timeouts
//
// Purpose:     Check for command and report timeouts.
//
// Returns:     void
//
// Parameters:  void
//
// ****************************************************************************
void check_for_timeouts( void );


// ****************************************************************************
// Function:    check_for_incoming_message
//
// Purpose:     Check CAN bus for incoming messages and process any present.
//
// Returns:     void
//
// Parameters:  void
//
// ****************************************************************************
void check_for_incoming_message( void );


#endif /* _OSCC_KIA_SOUL_STEERING_COMMUNICATIONS_H_ */
