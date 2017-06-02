# language: en

Feature: Receiving reports

  Chassis state reports should be received and parsed.


  Scenario Outline: Chassis State 1 report sent from CAN gateway.
    When a Chassis State 1 report is received with steering wheel angle <angle>
    Then the control state's current_steering_wheel_angle field should be <angle>

    Examples:
        | angle     |
        |  -32768   |
        |  -16384   |
        |  -8192    |
        |  -4096    |
        |  -2048    |
        |  -1024    |
        |  -512     |
        |  -256     |
        |  0        |
        |  256      |
        |  512      |
        |  1024     |
        |  2048     |
        |  4096     |
        |  8192     |
        |  16348    |
        |  32767    |

