//Robot Tour by Chinmay Govind & Reuben James for Cumberland Valley Science Olympiad

#define PIN_MOTOR_RIGHT_PWM   5 //analog
#define PIN_MOTOR_RIGHT_IN    7 //digital
#define PIN_MOTOR_LEFT_PWM    6 //analog
#define PIN_MOTOR_LEFT_IN     8 //digital
#define PIN_ENCODER_LEFT_FRONT_A    18 //digital (yellow wire), goes high on encoder pulse
#define PIN_ENCODER_RIGHT_FRONT_A    19 //digital (yellow wire), goes high on encoder pulse
#define PIN_ENCODER_LEFT_FRONT_B 22 //digital (white wire), high on forward tick, low on reverse tick
#define PIN_ENCODER_RIGHT_FRONT_B 24 //digital (white wire), high on forward tick, low on reverse tick
#define PIN_MOTOR_ENABLE      3 //digital

#define PIN_TRIGGER 2
long startMillis = 0;

#include "I2Cdev.h"
#include "MPU6050_6Axis_MotionApps20.h"
#define INTERRUPT_PIN 20

MPU6050 mpu;
double GYRO_TURN_CONSTANT = 0.0000109;
#if I2CDEV_IMPLEMENTATION == I2CDEV_ARDUINO_WIRE
    #include "Wire.h"
#endif

#include <FastLED.h>
#define NUM_LEDS 1    // Number of LEDs in strip (just 1)
#define PIN_LED 4    // Pin for data communications

#define NORTH 0
#define EAST -90
#define WEST 90
#define SOUTH 180
// Define the array of leds
CRGB leds[NUM_LEDS];

int encoderBPins[] = {PIN_ENCODER_LEFT_FRONT_B, PIN_ENCODER_RIGHT_FRONT_B};
int lastEncoderTicks[] = {0, 0};//LF, RF, LB, RB
int encoderTicks[] = {0, 0};//LF, RF, LB, RB

int STRAIGHT_SLOW = 100;//60 80 default, 100 120 on hard surfaces
int STRAIGHT_FAST = 120;
int STRAIGHT_ENDING_SLOW = 44;
int STRAIGHT_ENDING_FAST = 64;
double FORWARD_LR_TOLERANCE = 0.01;
int TURN_SPEED = 100;
int TURN_SLOW = 60;
int FULL_TURN_TICKS = 740;
double TURN_TOLERANCE = 0.04;//if this is too low, it will miss turns.
double TURN_TIME = 1000;

double robotX = 0;
double robotY = 0;
double robotTheta = 0;
double targetTheta = 0;
double targetThetaDeg;
double TICKS_PER_CM = 9.51;
double TICKS_PER_REV = 740;
double WHEEL_RADIUS = 7.7;

Quaternion quat;
bool dmpReady = false;  // set true if DMP init was successful
uint8_t mpuIntStatus;   // holds actual interrupt status byte from MPU
uint8_t devStatus;      // return status after each device operation (0 = success, !0 = error)
uint16_t packetSize;    // expected DMP packet size (default is 42 bytes)
uint16_t fifoCount;     // count of all bytes currently in FIFO
uint8_t fifoBuffer[64]; // FIFO storage buffer

// orientation/motion vars
VectorInt16 aa;         // [x, y, z]            accel sensor measurements
VectorInt16 aaReal;     // [x, y, z]            gravity-free accel sensor measurements
VectorInt16 aaWorld;    // [x, y, z]            world-frame accel sensor measurements
VectorFloat gravity;    // [x, y, z]            gravity vector
float euler[3];         // [psi, theta, phi]    Euler angle container
float ypr[3];  

void gyroSetup() {
  //mpu setup
  #if I2CDEV_IMPLEMENTATION == I2CDEV_ARDUINO_WIRE
      Wire.begin();
  #elif I2CDEV_IMPLEMENTATION == I2CDEV_BUILTIN_FASTWIRE
      Fastwire::setup(400, true);
  #endif
  Serial.println("Testing MPU connection...");
  mpu.initialize();
  leds[0] = CRGB::Orange;
  Serial.println(mpu.testConnection() ? "MPU6050 connection successful" : "MPU6050 connection failed");
  Serial.println("Calibrating MPU...");
  //mpu.CalibrateGyro();
  //mpu.CalibrateAccel();

  Serial.println("Initializing DMP...");
  devStatus = mpu.dmpInitialize();
  if (devStatus == 0) {
      // Calibration Time: generate offsets and calibrate our MPU6050
      mpu.CalibrateAccel(10);
      mpu.CalibrateGyro(10);
      mpu.PrintActiveOffsets();
      // turn on the DMP, now that it's ready
      Serial.println(F("Enabling DMP..."));
      mpu.setDMPEnabled(true);

      // enable Arduino interrupt detection
      Serial.print(F("Enabling interrupt detection (Arduino external interrupt "));
      Serial.print(digitalPinToInterrupt(INTERRUPT_PIN));
      Serial.println(F(")..."));
      attachInterrupt(digitalPinToInterrupt(INTERRUPT_PIN), dmpDataReady, RISING);
      mpuIntStatus = mpu.getIntStatus();

      // set our DMP Ready flag so the main loop() function knows it's okay to use it
      Serial.println(F("DMP ready! Waiting for first interrupt..."));
      dmpReady = true;

      // get expected DMP packet size for later comparison
      packetSize = mpu.dmpGetFIFOPacketSize();
  } else {
      // ERROR!
      // 1 = initial memory load failed
      // 2 = DMP configuration updates failed
      // (if it's going to break, usually the code will be 1)
      Serial.print(F("DMP Initialization failed (code "));
      Serial.print(devStatus);
      Serial.println(F(")"));
  }
  delay(1000);//DO NOT REMOVE
}

void setSpeed(int val) {
  STRAIGHT_SLOW = val;
  STRAIGHT_FAST = val + 20;
}

void readLFEncoder() {
  readEncoder(0);
}
void readRFEncoder() {
  readEncoder(1);
}
void readEncoder(int wheel) {//0, 1, 2, 3 = LF, RF, LB, RB
  //if going forward, increase motor ticks. otherwise, decrement
  if (digitalRead(encoderBPins[wheel]) == (wheel%2)) { //even wheels need LOW, odd wheels need HIGH
    encoderTicks[wheel]++;
    //Serial.println("Incrementing encoder ticks...");
  } else {
    encoderTicks[wheel]--;
    //Serial.println("Decrementing encoder ticks...");
  }
}

volatile bool mpuInterrupt = false;     // indicates whether MPU interrupt pin has gone high
void dmpDataReady() {
    mpuInterrupt = true;
}

void setMotors(int left, int right) {
  digitalWrite(PIN_MOTOR_ENABLE, HIGH);//enable motor controller
  digitalWrite(PIN_MOTOR_LEFT_IN, (left > 0) ? LOW : HIGH);//set right wheels to CW/CCW
  analogWrite(PIN_MOTOR_LEFT_PWM, abs(left));//set right wheels to left speed
  digitalWrite(PIN_MOTOR_RIGHT_IN, (right > 0) ? LOW : HIGH);//set right wheels to CW/CCW
  analogWrite(PIN_MOTOR_RIGHT_PWM, abs(right));//set right wheels to right speed
}

void forward(double distance, double time) {
  robotX = 0;
  robotY = 0;
  double newDist = distance - 8;//cm
  double newTime = time - 1;//s
  double speed;
  if (time > 0) {
    speed = max(STRAIGHT_SLOW, min(255, 5 * (newDist / newTime)));//PWM / (cm/s) * cm/s
  } else {
    speed = STRAIGHT_SLOW;
  }
  double forwardStart = millis();
  //find theta offset
  while (robotX < distance) {
    printInfo();
    updatePose();
    bool slowMode = distance - robotX < 8;
    int slowSpeed = slowMode ? STRAIGHT_ENDING_SLOW : speed;
    int fastSpeed = slowMode ? STRAIGHT_ENDING_FAST : speed + 20;
    if (angularDistance(robotTheta, targetTheta) < -FORWARD_LR_TOLERANCE) {
      //Serial.println("Correcting for left movement by steering right...");
      setMotors(fastSpeed, slowSpeed);
    } else if (angularDistance(robotTheta, targetTheta) > FORWARD_LR_TOLERANCE) {
      //Serial.println("Correcting for right movement by steering left...");
      setMotors(slowSpeed, fastSpeed);
    } else {
      setMotors(slowSpeed, slowSpeed);
    }
  }
  setMotors(0, 0);
  while (millis() - forwardStart < time * 1000) {
    printInfo();
    delay(1);
  }
}

void forwardStall(double distance, double stallTo, double startTime) {
  double time = (millis() - startTime)/1000;
  if (time > stallTo) {
    setSpeed(200);
    forward(distance, -1);
  } else {
    forward(distance, stallTo - time);
  }
}


void backward(double distance, double time) {
  robotX = 0;
  robotY = 0;
  double newDist = distance - 8;//cm
  double newTime = time - 1;//s
  double speed;
  if (time > 0) {
    speed = max(STRAIGHT_SLOW, min(255, 5 * (newDist / newTime)));//PWM / (cm/s) * cm/s
  } else {
    speed = STRAIGHT_SLOW;
  }
  double backwardStart = millis();
  //find theta offset
  while (robotX > -distance) {
    printInfo();
    updatePose();
    bool slowMode = distance - robotX < 8;
    int slowSpeed = slowMode ? STRAIGHT_ENDING_SLOW : speed;
    int fastSpeed = slowMode ? STRAIGHT_ENDING_FAST : speed + 20;
    if (angularDistance(robotTheta, targetTheta) > FORWARD_LR_TOLERANCE) {
      //Serial.println("Correcting for left movement by steering right...");
      setMotors(-fastSpeed, -slowSpeed);
    } else if (angularDistance(robotTheta, targetTheta) < -FORWARD_LR_TOLERANCE) {
      //Serial.println("Correcting for right movement by steering left...");
      setMotors(-slowSpeed, -fastSpeed);
    } else {
      setMotors(-slowSpeed, -slowSpeed);
    }
  }
  setMotors(0, 0);
  while (millis() - backwardStart < time * 1000) {
    printInfo();
    delay(1);
  }
}

//TURNING FUNC
void turnTo(double angle) {
  double targetAngleRad = angle * (PI / 180.0);
  targetTheta = targetAngleRad;
  targetThetaDeg = angle;
  double sign = angularDistance(robotTheta, targetTheta) > 0 ? 1 : -1;
  double turnStart = millis();
  while (abs(angularDistance(robotTheta, targetTheta)) > TURN_TOLERANCE) {
    updatePose();
    //printInfo();
    //Serial.println(angularDistance(robotTheta, targetAngle));
    double k = sign * TURN_SPEED;
    if (abs(angularDistance(robotTheta, targetTheta)) < 0.5) {
      k = sign * TURN_SLOW;
    }
    setMotors(-k, k);//positive k means left turn means positive angle change
  }
  setMotors(0, 0);
  while (millis() - turnStart < TURN_TIME) {
    printInfo();
    delay(1);  
  }
}

double angularDistance(double a, double b) {//turning distance from a to b
  return abs(a - b) < abs(min(a, b) + 2 * PI - max(a, b)) ? b - a : (min(a, b) + 2 * PI - max(a, b)) * (a > b ? 1 : -1);
}

double lastTime = 0;
double lastGyroTheta = 0;
void updatePose() {
  int differentials[] = {encoderTicks[0] - lastEncoderTicks[0], 
  encoderTicks[1] - lastEncoderTicks[1]};

  double dForward = (differentials[0] + differentials[1])/2.0 / TICKS_PER_CM;//forward change in cm
  //double dTime = (millis() - lastTime) / 1000.0;

  lastTime = millis();
  robotX += dForward * cos(angularDistance(targetTheta, robotTheta));
  robotY += dForward * sin(angularDistance(targetTheta, robotTheta));
  if (!dmpReady) return;
  // read a packet from FIFO
  if (mpu.dmpGetCurrentFIFOPacket(fifoBuffer)) { // Get the Latest packet 
  // display Euler angles in degrees
          mpu.dmpGetQuaternion(&quat, fifoBuffer);
          mpu.dmpGetEuler(euler, &quat);
  }
  robotTheta = -euler[0];
  for (int i = 0; i < 2; i++) lastEncoderTicks[i] = encoderTicks[i];
}

void printInfo() {
  Serial.print("Encoder ticks: ");
  Serial.print(encoderTicks[0]);
  Serial.print(", ");
  Serial.print(encoderTicks[1]);
  Serial.print(" | Pose: ");
  Serial.print(robotX);
  Serial.print(", ");
  Serial.print(robotY);
  Serial.print(", ");
  Serial.print(angularDistance(targetTheta, robotTheta));
  Serial.print("(rad) / ");
  Serial.print(angularDistance(targetTheta, robotTheta) * (180 / PI));
  Serial.print(" (deg)");
  Serial.println();
}

void stop() {
    Serial.println("Stop Triggered, Shutting Down...");
    digitalWrite(PIN_MOTOR_ENABLE, LOW);//disable motor controller
    leds[0] = CRGB::Red;
    FastLED.show();  
    delay(250);
    exit(0);
}

void setup() {
  Serial.begin(9600);
  FastLED.addLeds<NEOPIXEL, PIN_LED>(leds, NUM_LEDS);  // Configure the LED    
  FastLED.setBrightness(1);  
  for (int i = 0; i < 3; i++) {
    leds[0] = CRGB::Black;
    FastLED.show();    
    delay(200);
    leds[0] = CRGB::Red;
    FastLED.show();     
    delay(200); 
  }

  Serial.println("\nWaiting for Trigger Press...");
  while (digitalRead(PIN_TRIGGER) == HIGH) {
    //wait
  }
  Serial.println("Trigger Pressed...");
  leds[0] = CRGB::Yellow;
  FastLED.show();  
  
  //attach encoder interrupts
  attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_LEFT_FRONT_A), readLFEncoder, RISING);
  attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_RIGHT_FRONT_A), readRFEncoder, RISING);
  //attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_LEFT_BACK_A), readLBEncoder, RISING);
  //attachInterrupt(digitalPinToInterrupt(PIN_ENCODER_RIGHT_BACK_A), readRBEncoder, RISING);

  gyroSetup();
  leds[0] = CRGB::Green;
  FastLED.show();  
  Serial.println("\nWaiting for Trigger Press...");
  while (digitalRead(PIN_TRIGGER) == HIGH) {
    //wait
  }
  leds[0] = CRGB::Blue;
  FastLED.show();  
  Serial.println("Trigger Pressed...");
  delay(250);
  Serial.println("Starting...");
  startMillis = millis();
  //RUN CODE GOES HERE (TRIPATHI)
  //start: 14
  //gates: 1, 4, 6, 12
  //end: 16
  //time: 63
  robotX = 0;
  robotY = 0;
  robotTheta = 0;
  TURN_TIME = 2000;
  forward(30, 3);
  turnTo(EAST);
  forward(30, 3);
  turnTo(SOUTH);
  forward(30, 3);
  turnTo(WEST);
  forward(30, 3);
  turnTo(NORTH);
  turnTo(WEST);
  backward(30, 3);
  turnTo(SOUTH);
  backward(30, 3);
  turnTo(EAST);
  backward(30, 3);
  turnTo(NORTH);
  backward(30, 3);
} 

void loop() {
  // put your main code here, to run repeatedly:
  if (digitalRead(PIN_TRIGGER) == LOW && (millis() - startMillis > 1500)) {
    stop();
  }
  updatePose();
  printInfo();
}
