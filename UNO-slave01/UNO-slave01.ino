/*
- 4 Buttons are used to mimic limit switches
- 1 LED is used to mimic the relay to control deployment door
- 1 L298N motor driver is used to mimic 4 relays motors control
Assumptions at boot up
- Doors are assumed to be closed
- Motors are off and both jackscrews are at home positions
Ideally the current state of the device should be checked at bootup
*/
#include <RF24.h>
#include <RF24Network.h>
#include <SPI.h>
#include "Button.h"

// Motor states
#define DEPLOYED 1
#define DEPLOYING 2
#define UNDEPLOYED 3
#define UNDEPLOYING 4
#define STOPPED 11

// Motor Commands
#define FORWARD 5
#define BACKWARD 6
#define OFF 7

// Payload messages
#define DEPLOY 8
#define UNDEPLOY 9
#define STOP 10

// Motors Control Pins
#define MOTOR_LEFT1 18 //A4
#define MOTOR_LEFT2 17 //A3
#define MOTOR_RIGHT1 16 //A2
#define MOTOR_RIGHT2 15 //A1

// Deployment Length Control Pins
#define HOME_SWITCH_LEFT 6
#define LIMIT_SWITCH_LEFT 5
#define HOME_SWITCH_RIGHT 4
#define LIMIT_SWITCH_RIGHT 3

// Deployment Door Control Pins
#define DOOR_RELAY 14 //A0
#define DOOR_MOTOR_1 9 //D9
#define DOOR_MOTOR_2 19 //A5
#define DOOR_RELAY_DELAY 5000
#define CLOSED 20
#define OPENING 21
#define OPENED 22

struct MotorState {
  byte left;
  byte right;
};

struct Command {
  byte currentMsg;
} command;

RF24 radio(7, 10);               // nRF24L01 (CE,CSN)
RF24Network network(radio);      // Include the radio in the network
const uint16_t this_node = 01;   // Address of our node in Octal format ( 04,031, etc)
const uint16_t master_node = 00;
Button leftHome(HOME_SWITCH_LEFT);
Button leftLimit(LIMIT_SWITCH_LEFT);
Button rightHome(HOME_SWITCH_RIGHT);
Button rightLimit(LIMIT_SWITCH_RIGHT); 
byte payloadMsg;
MotorState motorState = {UNDEPLOYED, UNDEPLOYED};
byte doorState = CLOSED;
unsigned long doorMillis;

void setup() {
  SPI.begin();
  radio.begin();
  radio.setDataRate(RF24_1MBPS);
  network.begin(90, this_node); //(channel, node address)
  Serial.begin(9600);
  pinMode(MOTOR_LEFT1, OUTPUT);
  pinMode(MOTOR_LEFT2, OUTPUT);
  pinMode(MOTOR_RIGHT1, OUTPUT);
  pinMode(MOTOR_RIGHT2, OUTPUT);
  pinMode(DOOR_RELAY, OUTPUT);
  pinMode(DOOR_MOTOR_1, OUTPUT);
  pinMode(DOOR_MOTOR_2, OUTPUT);
  leftHome.init();
  leftLimit.init();
  rightHome.init();
  rightLimit.init();
  //Update state of the device here
}

void loop() {
  network.update();
  // only allow to undeploy and stop when doors are closed
  if(doorState == OPENED || command.currentMsg != DEPLOY) {
    // Doors are opened now, do whatever
    leftMotorRoutine();
    rightMotorRoutine();
  }
  if (doorState == OPENING) {
    if(millis() - doorMillis > DOOR_RELAY_DELAY){
      digitalWrite(DOOR_RELAY, LOW);
      doorState = OPENED;
      doorMotorControl(OFF)
    }
  }
  while (network.available()){     // Is there any incoming data?
    RF24NetworkHeader header;
    unsigned long incomingData;
    network.read(header, &incomingData, sizeof(incomingData)); // Read the incoming data
    command.currentMsg = (byte)incomingData;
    if (command.currentMsg == DEPLOY){
      if (doorState == CLOSED){
        // Wait 5 secs for the doors to open
        // After that motors are free to do whatever
        doorState = OPENING;
        doorMillis = millis();
        digitalWrite(DOOR_RELAY, HIGH);
        doorMotorControl(FORWARD);
      }
    }
  }
  if(motorState.left==motorState.right){
    payloadMsg = motorState.left;
  } else {
    payloadMsg = (motorState.left > motorState.right) ? motorState.left : motorState.right;
  }
  if(doorState == OPENING){
    // At least let the user know that the device is deploying
    payloadMsg = DEPLOYING;
  }
  // Send feedback to master
  RF24NetworkHeader header(master_node);
  network.write(header, &payloadMsg, sizeof(payloadMsg));
  Serial.print("Left motorState: ");
  printState(motorState.left);
  Serial.print(" Right motorState: ");
  printState(motorState.right);
  Serial.println("");
}

void printState(byte num) {
  switch(num){
    case DEPLOYED:
      Serial.print("DEPLOYED");
      break;
    case DEPLOYING:
      Serial.print("DEPLOYING");
      break;
    case UNDEPLOYED:
      Serial.print("UNDEPLOYED");
      break;
    case UNDEPLOYING:
      Serial.print("UNDEPLOYING");
      break;
    case STOPPED:
      Serial.print("STOPPED");
      break;
  }
}

void leftMotorRoutine() {
  if (command.currentMsg == STOP){
    leftMotorControl(OFF);
    motorState.left = STOPPED;
    return;
  }
  // left home is touched
  if (leftHome.read() == 0 && leftLimit.read() == 1) {
    if(motorState.left == UNDEPLOYED && command.currentMsg == DEPLOY){
      leftMotorControl(FORWARD);
      motorState.left = DEPLOYING;
    } else if (motorState.left == UNDEPLOYING && command.currentMsg == UNDEPLOY){
      leftMotorControl(OFF);
      motorState.left = UNDEPLOYED;
    }
  // left limit is touched
  } else if (leftHome.read() == 1 && leftLimit.read() == 0) {
    if(motorState.left == DEPLOYED && command.currentMsg == UNDEPLOY){
      leftMotorControl(BACKWARD);
      motorState.left = UNDEPLOYING;
    } else if (motorState.left == DEPLOYING && command.currentMsg == DEPLOY){
      leftMotorControl(OFF);
      motorState.left = DEPLOYED;
    }
  // jack is between the two switches
  } else {
    if(command.currentMsg == DEPLOY && motorState.left != DEPLOYED){
      leftMotorControl(FORWARD);
      motorState.left = DEPLOYING;
    } else if (command.currentMsg == UNDEPLOY && motorState.left != UNDEPLOYED) {
      leftMotorControl(BACKWARD);
      motorState.left = UNDEPLOYING;
    }
  }
}

void rightMotorRoutine() {
  if (command.currentMsg == STOP){
    rightMotorControl(OFF);
    motorState.right = STOPPED;
    return;
  }
  // right home is touched
  if (rightHome.read() == 0 && rightLimit.read() == 1) {
    if(motorState.right == UNDEPLOYED && command.currentMsg == DEPLOY){
      rightMotorControl(FORWARD);
      motorState.right = DEPLOYING;
    } else if (motorState.right == UNDEPLOYING && command.currentMsg == UNDEPLOY){
      rightMotorControl(OFF);
      motorState.right = UNDEPLOYED;
    }
  // right limit is touched
  } else if (rightHome.read() == 1 && rightLimit.read() == 0) {
    if(motorState.right == DEPLOYED && command.currentMsg == UNDEPLOY){
      rightMotorControl(BACKWARD);
      motorState.right = UNDEPLOYING;
    } else if (motorState.right == DEPLOYING && command.currentMsg == DEPLOY){
      rightMotorControl(OFF);
      motorState.right = DEPLOYED;
    }
  // jack is between the two switches
  } else {
    if(command.currentMsg == DEPLOY && motorState.right != DEPLOYED){
      rightMotorControl(FORWARD);
      motorState.right = DEPLOYING;
    } else if (command.currentMsg == UNDEPLOY && motorState.right != UNDEPLOYED) {
      rightMotorControl(BACKWARD);
      motorState.right = UNDEPLOYING;
    }
  }
}

void leftMotorControl(byte val) {
  if(val == OFF){
    //OFF
    digitalWrite(MOTOR_LEFT1,LOW);
    digitalWrite(MOTOR_LEFT2,LOW);
  }else if(val == FORWARD){
    digitalWrite(MOTOR_LEFT1,HIGH);
    digitalWrite(MOTOR_LEFT2,LOW);
  }else if(val == BACKWARD){
    digitalWrite(MOTOR_LEFT1,LOW);
    digitalWrite(MOTOR_LEFT2,HIGH);
  }
}

void rightMotorControl(byte val) {
  if(val == OFF){
    //OFF
    digitalWrite(MOTOR_RIGHT1,LOW);
    digitalWrite(MOTOR_RIGHT2,LOW);
  }else if(val == FORWARD){
    digitalWrite(MOTOR_RIGHT1,HIGH);
    digitalWrite(MOTOR_RIGHT2,LOW);
  }else if(val == BACKWARD){
    digitalWrite(MOTOR_RIGHT1,LOW);
    digitalWrite(MOTOR_RIGHT2,HIGH);
  }
}

void doorMotorControl(byte val) {
  if(val == OFF){
    //OFF
    digitalWrite(MOTOR_RIGHT1,LOW);
    digitalWrite(MOTOR_RIGHT2,LOW);
  }else if(val == FORWARD){
    digitalWrite(MOTOR_RIGHT1,HIGH);
    digitalWrite(MOTOR_RIGHT2,LOW);
  }else if(val == BACKWARD){
    digitalWrite(MOTOR_RIGHT1,LOW);
    digitalWrite(MOTOR_RIGHT2,HIGH);
  }
}