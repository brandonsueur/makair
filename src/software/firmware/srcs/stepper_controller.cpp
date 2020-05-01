/******************************************************************************
 * @author Makers For Life
 * @copyright Copyright (c) 2020 Makers For Life
 * @file blower.cpp
 * @brief Tools to control the blower
 *****************************************************************************/

#pragma once

#include "../includes/config.h"
#include "Arduino.h"
#include <HardwareSerial.h>

// INCLUDES ===================================================================
#if MODE == MODE_STEPPER_CONTROLLER

#define STEPPER_IDLE_CURRENT 15
#define STEPPER_MOVING_CURRENT 40
#define STEPPER_MIN_SPEED 400
#define STEPPER_MAX_SPEED 3000

#define STEPPER_OPENED_POSITION -134
#define STEPPER_CLOSED_POSITION 59

#define STEPPER_EN PA10
#define STEPPER_HALFFULL PB5
#define STEPPER_CONTROL PB4
#define STEPPER_CLOCK PB10
#define STEPPER_CWCCW PA8
#define STEPPER_RESET PA9
// VrefA = VrefB, connect them together on IHM05A1
#define STEPPER_VREF PC7

#define STEPPER_OPTICAL_INPUT PA6
#define STEPPER_PWM_INPUT PB6

// do not include math.h for such function...
#define min(a, b) (((a) < (b)) ? (a) : (b))
#define max(a, b) (((a) > (b)) ? (a) : (b))

HardwareTimer* Timer_Hw_Vref;
HardwareTimer* Timer_Stepper;
HardwareTimer* Timer_Stepper_Speed_Control;
HardwareTimer* Timer_Pwm_Input;

int VrefIdle = STEPPER_IDLE_CURRENT;
int VrefPwmPercent = STEPPER_IDLE_CURRENT;
int VrefCurrent = STEPPER_IDLE_CURRENT;
int32_t stepperSetpoint = 0;
int32_t stepperCurrentPosition = 0;
int32_t stepperSpeed = 0;     // 0 (low speed) - 100 (full speed)  signed, speed can turn negative.
boolean initialized = false;  // false until optical zero is reached
int32_t stepperPreviousMeasuredDuty = 0;

#define PWM_CAPTURE_OVERFLOW 50000
#define PWM_CAPTURE_PRESCALER 100    // 100 -> 1 us precision
int32_t pwm_capture_frequency = -1;  // will be computed during init
int32_t edge_counter = 0;
int32_t current_edge_capture;
int32_t last_period_capture = 0;
int32_t measured_high_state = 0;
int32_t measured_freq;  // herz
int32_t measured_duty;  // per thousand

enum stepper_modeEnum { AUTO, MANUAL };
enum stepper_modeEnum stepperMode = AUTO;

// measure time between last falling edge and rising edge, compute freq and duty cycle
void PWM_Input_Rising_Callback(HardwareTimer*) {
    edge_counter++;
    current_edge_capture = Timer_Pwm_Input->getCaptureCompare(1);  // get channel 1 capture
    if (current_edge_capture > last_period_capture) {
        measured_freq = pwm_capture_frequency / (current_edge_capture - last_period_capture);
        measured_duty = 1000 * measured_high_state / (current_edge_capture - last_period_capture);
    } else {
        measured_freq = pwm_capture_frequency
                        / (current_edge_capture - last_period_capture + PWM_CAPTURE_OVERFLOW);
        measured_duty = 1000 * measured_high_state
                        / (current_edge_capture - last_period_capture + PWM_CAPTURE_OVERFLOW);
    }

    last_period_capture = current_edge_capture;
}

// measure time between last rising edge and current falling edge
void PWM_Input_Falling_Callback(HardwareTimer*) {
    edge_counter++;
    current_edge_capture = Timer_Pwm_Input->getCaptureCompare(2);  // get channel 2 capture
    if (current_edge_capture > last_period_capture) {
        measured_high_state = current_edge_capture - last_period_capture;
    } else {
        measured_high_state = current_edge_capture - last_period_capture + PWM_CAPTURE_OVERFLOW;
    }
}

void PWM_Input_Rollover_Callback(HardwareTimer*) {
    if (edge_counter == 0) {
        measured_duty = 0;
        measured_freq = 0;
        Serial.println("no PWM input signal");
    }
    edge_counter = 0;
}

void setVref(uint32_t pwm_percent) {
    Timer_Hw_Vref->setCaptureCompare(2, pwm_percent);
    VrefCurrent = pwm_percent;
}

void setVrefRamp(uint32_t pwm_percent) {
    if (pwm_percent > VrefCurrent) {
        VrefCurrent = pwm_percent;
    }
    if (pwm_percent < VrefCurrent) {
        VrefCurrent--;
    }
    Timer_Hw_Vref->setCaptureCompare(2, VrefCurrent);
}

int previousOpticalInput = LOW;
int currentOpticalInput;
// fixed period 1khz
void Timer_Stepper_SpeedControl_Callback(HardwareTimer*) {
    if (initialized) {
        if (stepperSetpoint != stepperCurrentPosition) {
            setVref(STEPPER_MOVING_CURRENT);
            if (stepperSetpoint > stepperCurrentPosition) {
                stepperSpeed = min(
                    min(stepperSpeed + 2, ((stepperSetpoint - stepperCurrentPosition) / 2)), 100);
                if (stepperSpeed == 0) {
                    stepperSpeed = 2;
                }
            } else {
                stepperSpeed = max(
                    max(stepperSpeed - 2, ((stepperSetpoint - stepperCurrentPosition) / 2)), -100);
                if (stepperSpeed == 0) {
                    stepperSpeed = -2;
                }
            }

        } else {
            stepperSpeed = 0;
            setVrefRamp(VrefIdle);
        }
    } else {
        stepperSpeed = 15;  // rotate until initilized
    }
    // if rising edge on optical input while speed is positive, set zero
    currentOpticalInput = digitalRead(STEPPER_OPTICAL_INPUT);
    if (stepperSpeed > 0 && previousOpticalInput == LOW && currentOpticalInput == HIGH) {
        stepperCurrentPosition = 0;
        initialized = true;
    }
    previousOpticalInput = currentOpticalInput;
}

// clock and rotation direction (period vary up to speed)
void Timer_Stepper_Callback(HardwareTimer*) {
    if (stepperSpeed != 0) {
        digitalWrite(STEPPER_CLOCK, LOW);
        if (stepperSpeed > 0) {
            digitalWrite(STEPPER_CWCCW, LOW);
            stepperCurrentPosition++;
        } else {
            digitalWrite(STEPPER_CWCCW, HIGH);
            stepperCurrentPosition--;
        }
        delayMicroseconds(1);
        digitalWrite(STEPPER_CLOCK, HIGH);
    }
    // 0->100 pulse/s  100->1400 pulse/s
    Timer_Stepper->setOverflow(
        100000 / map(abs(stepperSpeed), 0, 100, STEPPER_MIN_SPEED, STEPPER_MAX_SPEED));
}

void loop(void) {
    // read serial port for commands

    while (Serial.available()) {
        // get the new byte:
        char inChar = (char)Serial.read();

        switch (inChar) {
        case '+':
            if (VrefIdle < 99) {
                VrefIdle++;
            }
            setVref(VrefIdle);
            Serial.print("increase current pwm to ");
            Serial.print(VrefIdle);
            Serial.print("% ,");
            Serial.print(VrefIdle * 0.035);
            Serial.println(" A");
            break;

        case '-':
            if (VrefIdle > 1) {
                VrefIdle--;
            }
            setVref(VrefIdle);
            Serial.print("decrease current pwm to ");
            Serial.print(VrefIdle);
            Serial.print("% ,");
            Serial.print(VrefIdle * 0.035);
            Serial.println(" A");
            break;

        case 'e':
            digitalWrite(STEPPER_EN, !digitalRead(STEPPER_EN));
            Serial.print("toggle power enable ");
            Serial.println(digitalRead(STEPPER_EN) == LOW ? "OFF" : "ON");
            if (digitalRead(STEPPER_EN) == LOW) {
                initialized = false;
            }
            break;
        case '0':
            stepperMode = MANUAL;
            stepperSetpoint = -160;
            break;
        case '1':
            stepperMode = MANUAL;
            stepperSetpoint = 160;
            break;
        case '2':
            stepperMode = MANUAL;
            stepperSetpoint--;
            break;
        case '8':
            stepperMode = MANUAL;
            stepperSetpoint++;
            break;
        case 'a':
            stepperMode = AUTO;
            break;
        }
    }

    if (stepperMode == AUTO) {
        if (abs(stepperPreviousMeasuredDuty - measured_duty) > 3) {
            stepperSetpoint =
                map(measured_duty, 640, 900, STEPPER_OPENED_POSITION, STEPPER_CLOSED_POSITION);
        }
        stepperPreviousMeasuredDuty = measured_duty;
    }
    Serial.println(measured_duty);

    // Serial.print("freq=");
    // Serial.print(measured_freq);
    // Serial.print(" highstate=");
    // Serial.print(measured_high_state);
    // Serial.print(" duty=");
    // Serial.println((measured_duty));
}

void setup(void) {
    Serial.begin(115200);
    Serial.println("Stepper Controler with L6208PD");

    pinMode(STEPPER_EN, OUTPUT);
    pinMode(STEPPER_HALFFULL, OUTPUT);
    pinMode(STEPPER_CONTROL, OUTPUT);
    pinMode(STEPPER_CLOCK, OUTPUT);
    pinMode(STEPPER_CWCCW, OUTPUT);
    pinMode(STEPPER_RESET, OUTPUT);
    pinMode(STEPPER_VREF, OUTPUT);
    pinMode(PB3, INPUT);  // the other vref source
    pinMode(PA7, INPUT);  // the other vref source
    pinMode(STEPPER_OPTICAL_INPUT, INPUT);
    pinMode(STEPPER_PWM_INPUT, INPUT);

    // digitalWrite(STEPPER_VREF, LOW);
    digitalWrite(STEPPER_RESET, LOW);
    digitalWrite(STEPPER_CONTROL, LOW);    // low = fast decay
    digitalWrite(STEPPER_HALFFULL, HIGH);  // low = full step
    digitalWrite(STEPPER_CWCCW, LOW);
    digitalWrite(STEPPER_CLOCK, HIGH);
    digitalWrite(STEPPER_EN, LOW);  // low = disable power
    digitalWrite(STEPPER_RESET, HIGH);
    digitalWrite(STEPPER_EN, HIGH);  // low = disable power

    // create Vref with a pwm on pwm3 channel 2
    Timer_Hw_Vref = new HardwareTimer(TIM3);
    // CPU Clock down to 10 kHz
    Timer_Hw_Vref->setPrescaleFactor(10);  // 100khz
    Timer_Hw_Vref->setMode(2, TIMER_OUTPUT_COMPARE_PWM1, STEPPER_VREF);

    Timer_Hw_Vref->setOverflow(100);
    setVref(VrefPwmPercent);
    Timer_Hw_Vref->resume();
    Serial.println("Vref set to half");

    // use TIM10, channel 1.
    Timer_Stepper = new HardwareTimer(TIM10);
    // prescaler. stm32f411 clock is 100mhz
    Timer_Stepper->setPrescaleFactor(1000);  // 100khz
    // set the period
    Timer_Stepper->setOverflow(1000);                     // 100hz, default step freq
    Timer_Stepper->setMode(1, TIMER_OUTPUT_COMPARE, NC);  // channel 1
    Timer_Stepper->attachInterrupt(Timer_Stepper_Callback);
    Timer_Stepper->setInterruptPriority(3, 1);
    Timer_Stepper->resume();

    Serial.println("stepper timer launched");

    // use TIM11, channel 1.
    Timer_Stepper_Speed_Control = new HardwareTimer(TIM11);
    // prescaler. stm32f411 clock is 100mhz
    Timer_Stepper_Speed_Control->setPrescaleFactor(1000);  // 100khz
    // set the period
    Timer_Stepper_Speed_Control->setOverflow(100);                      // 1 khz
    Timer_Stepper_Speed_Control->setMode(1, TIMER_OUTPUT_COMPARE, NC);  // channel 1
    Timer_Stepper_Speed_Control->attachInterrupt(Timer_Stepper_SpeedControl_Callback);
    Timer_Stepper_Speed_Control->resume();

    // Use TIM4 for input capture of the PWM command signal.
    Timer_Pwm_Input = new HardwareTimer(TIM4);
    // prescaler. stm32f411 clock is 100mhz
    Timer_Pwm_Input->setPrescaleFactor(PWM_CAPTURE_PRESCALER);  // 100khz -> 10 us precision...
    pwm_capture_frequency = Timer_Pwm_Input->getTimerClkFreq() / PWM_CAPTURE_PRESCALER;
    // Used 2 channels for a single pin. One channel in TIM_INPUTCHANNELPOLARITY_RISING another
    // channel in TIM_INPUTCHANNELPOLARITY_FALLING.
    // Channels must be used by pair: CH1 with CH2, or CH3 with CH4
    Timer_Pwm_Input->setMode(1, TIMER_INPUT_FREQ_DUTY_MEASUREMENT, STEPPER_PWM_INPUT);  // channel 1
    Timer_Pwm_Input->setOverflow(PWM_CAPTURE_OVERFLOW);               // 0.5 second before overflow
    Timer_Pwm_Input->attachInterrupt(1, PWM_Input_Rising_Callback);   // channel 1
    Timer_Pwm_Input->attachInterrupt(2, PWM_Input_Falling_Callback);  // channel 2
    Timer_Pwm_Input->attachInterrupt(PWM_Input_Rollover_Callback);

    Timer_Pwm_Input->resume();
}

#endif