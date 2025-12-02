/**
 * Silabs MG24 6PWM closed loop velocity control example with HALL sensor based rotor position and Bluetooth LE serial
 *
 * HARDWARE CONFIGURATION:
 *  CPU Board: Arduino Nano Matter or Arduino Thingplusmatter
 *  Motor Driver Board: BOOSTXL-DRV8305EVM
 *  BLDC Motor: Newark DF45M024053-A2
 */
#include <SimpleFOC.h>
#include "sppBLE.h"

#define HALL_SENSOR_IRQ 1
#define ENABLE_MONITOR  0
#define MOTOR_PP        8  // BLDC motor pole pairs
// I/O configuration
#if defined(ARDUINO_BOARD_SILABS_THINGPLUSMATTER)
#pragma message ( "ARDUINO_BOARD_SILABS_THINGPLUSMATTER" )
#define PWM_1H     D6
#define PWM_1L     D7
#define PWM_2H     D8
#define PWM_2L     D9
#define PWM_3H     D10
#define PWM_3L     D11
#define PWM_EN     D0
#define HALL_A     D3
#define HALL_B     D4
#define HALL_C     D5
#elif defined(ARDUINO_BOARD_NANO_MATTER)
#pragma message ( "ARDUINO_BOARD_NANO_MATTER" )
#define PWM_1H     D6
#define PWM_1L     D7
#define PWM_2H     D8
#define PWM_2L     D9
#define PWM_3H     D10
#define PWM_3L     D11
#define PWM_EN     D12
#define HALL_A     D5
#define HALL_B     D4
#define HALL_C     D13
#else
#error "Board is not supported"
#endif

static bool allow_run = false;

// BLDC motor instance
BLDCMotor *motor;

// BLDC driver instance
BLDCDriver6PWM *driver;

// Commander instance
Commander *command;

// Hall sensor instance
HallSensor *sensor;

// Interrupt routine initialisation
void doA()
{
  sensor->handleA();
}
void doB()
{
  sensor->handleB();
}
void doC()
{
  sensor->handleC();
}

void doMotor(char* cmd)
{
  if (!command) {
    return;
  }
  command->motor(motor, cmd);
}

bool sendReady(size_t index, const uint8_t *buffer, size_t size)
{
  if (!buffer) {
    return false;
  }
  if (index < size && (buffer[index] == '\r' || buffer[index] == '\n')) {
    return true;
  }
  return false;
}

void setup()
{
  Serial.begin(115200);

  SimpleFOCDebug::enable(&Serial);

  // Driver
  driver = new BLDCDriver6PWM(PWM_1H, PWM_1L, PWM_2H, PWM_2L, PWM_3H, PWM_3L, PWM_EN);
  if (!driver) {
    return;
  }

  // Power supply voltage [V]
  driver->voltage_power_supply = 24;
  // Max DC voltage allowed - default voltage_power_supply
  driver->voltage_limit = 12;
  // PWM frequency to be used [Hz]
  driver->pwm_frequency = 20000;
  // Dead zone percentage of the duty cycle - default 0.02 - 2%
  // Can set value to 0 because the TI driver (DRV8305) will provide the
  // required dead-time.
  driver->dead_zone = 0;

  // Init driver
  if (!driver->init()) {
    return;
  }

  driver->enable();

  // Setup Hall Sensor
  sensor = new HallSensor(HALL_A, HALL_B, HALL_C, MOTOR_PP);
  if (!sensor) {
    return;
  }

  sensor->init();

#if HALL_SENSOR_IRQ
  sensor->enableInterrupts(doA, doB, doC);
#else
  // Note: There is a bug when initializing HallSensor in heap, attribute
  // `use_interrupt` gets value not `false` even `enableInterrupts` has not been
  // called. So we initialize this attribute value `false` after creating a
  // `HallSensor` instance.
  sensor->use_interrupt = false;
#endif

  // Setup BLDC Motor
  motor = new BLDCMotor(MOTOR_PP);
  if (!motor) {
    return;
  }

  // Link the motor and the driver
  motor->linkDriver(driver);

  // Link the motor to the sensor
  motor->linkSensor(sensor);

  // Set below the motor's max
  motor->velocity_limit = 530.0f;

  // Set FOC modulation
  motor->foc_modulation = FOCModulationType::SpaceVectorPWM;

  // Set motion control loop to be used
  motor->controller = MotionControlType::velocity;

  // controller configuration
  // velocity PI controller parameters
  motor->PID_velocity.P = 0.05f;
  motor->PID_velocity.I = 1;

  // velocity low pass filtering time constant
  motor->LPF_velocity.Tf = 0.01f;

#if ENABLE_MONITOR
  motor->useMonitoring(Serial);
  motor->monitor_variables = _MON_TARGET | _MON_CURR_Q | _MON_CURR_D | _MON_VEL;
#endif

  // Initialize motor
  if (!motor->init()) {
    Serial.println("Motor init failed!");
    return;
  }

  // Commander
  command = new Commander(Serial);
  if (!command) {
    return;
  }

  // add target command M
  command->add('M', doMotor, "motor");

  // align sensor and start FOC
  if (!motor->initFOC()) {
    Serial.println("FOC init failed!");
    return;
  }

  Serial.println("Motor ready!");
  Serial.println("Set target velocity [rad/s]");

  // BLE SPP
  sppBLE.onCheckSendCondition(sendReady);
  // sppBLE.enable_log(true);
  sppBLE.begin("motor");
  Serial.println("BLE ready!");

  allow_run = true;

  _delay(1000);
}

void loop()
{
  if (!allow_run) {
    return;
  }

  // main FOC algorithm function
  // the faster you run this function the better
  motor->loopFOC();

  // Motion control function
  motor->move();

#if ENABLE_MONITOR
  // Function intended to be used with serial plotter to monitor motor variables
  // significantly slowing the execution down!!!!
  motor->monitor();
#endif

  // user communication
  command->run();

  command->run(sppBLE);
}
