#ifndef SERVO_DRIVER_H
#define SERVO_DRIVER_H

#include <stdint.h>

#define SERVO_MAX_COUNT 4

#define SERVO_MIN_ANGLE 0.0f
#define SERVO_MAX_ANGLE 180.0f

#define SERVO_MIN_PULSE_US 500
#define SERVO_MAX_PULSE_US 2400


typedef struct
{
    uint8_t     is_attached;
    uint8_t  gpio;
    uint8_t  slice_num;
    uint8_t  channel;
    float    current_angle;

} servo_t;


/*
 * Initialize internal driver state
 */
void servo_driver_init(void);


/*
 * Attach servo to GPIO
 *
 * Returns:
 *  >=0 : servo handle/index
 *  -1  : no free slots
 */
int servo_attach(uint8_t gpio);


/*
 * Set servo angle in degrees
 *
 * angle range:
 * 0 -> 180
 */
void servo_write_angle(int handle, float angle);


/*
 * Set pulse width directly in microseconds
 */
void servo_write_us(int handle, uint16_t pulse_us);


/*
 * Disable servo
 */
void servo_detach(int handle);

#endif // SERVO_DRIVER_H