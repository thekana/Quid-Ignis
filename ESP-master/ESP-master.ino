  // Include Libraries
#include "Button.h"
#include "WiFi.h"
#include "ESPAsyncWebServer.h"
#include <RF24.h>
#include <RF24Network.h>
#include <SPI.h>
#include <LiquidCrystal_I2C.h>

// Motor states
#define DEPLOYED 1
#define DEPLOYING 2
#define UNDEPLOYED 3
#define UNDEPLOYING 4
#define STOPPED 11

// Payload messages
#define DEPLOY 8
#define UNDEPLOY 9
#define STOP 10

// Pin Definitions
#define EMERGENCY_START 4
#define EMERGENCY_STOP 32
#define EMERGENCY_TIMEOUT 3000

// Replace with your network credentials
const char* ssid = "Group32";
const char* password = "testpa$$";

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);
LiquidCrystal_I2C lcd(0x27, 16, 2);

unsigned long emergencyMillis;

// Create struct to store state
// 3 possible value
// 0 : OFF
// 1 : ON
// 2 : N/A
struct state {
  byte node1;
  byte node2;
} deviceState;

RF24 radio(12, 5);               // nRF24L01 (CE,CSN)
RF24Network network(radio);      // Include the radio in the network
const uint16_t this_node = 0;   // Address of this node in Octal format ( 04,031, etc)
const uint16_t node01 = 1;
const uint16_t node02 = 2;

unsigned int lcdDelay = 1000;
char menu = 1;

const char index_html[] PROGMEM = R"rawliteral(<!DOCTYPE html><html><head><meta name="viewport" content="width=device-width, initial-scale=1" /><style>html{font-family:Arial;display:inline-block;margin:0px auto;text-align:center}h2{font-size:3rem}.labels{font-size:1.5rem;vertical-align:middle;padding-bottom:15px}.container{max-width:1000px;margin-right:auto;margin-left:auto;display:flex;justify-content:center;align-items:baseline;min-height:100vh}table tr{background-color:#f8f8f8;border:1px solid #ddd;padding:0.35em}table th, table td{padding:0.625em;text-align:center}table th{font-size:0.85em;letter-spacing:0.1em;text-transform:uppercase}@media screen and (max-width: 600px){table{border:0}table caption{font-size:1.3em}table thead{border:none;clip:rect(0 0 0 0);height:1px;margin:-1px;overflow:hidden;padding:0;position:absolute;width:1px}table tr{border-bottom:3px solid #ddd;display:block;margin-bottom:0.625em}table td{border-bottom:1px solid #ddd;display:block;font-size:0.8em;text-align:right}table td::before{content:attr(data-label);float:left;font-weight:bold;text-transform:uppercase;font-size:1rem}table td:last-child{border-bottom:0}}</style></head><body><h2>Group32 Devices Controller</h2><div class="container"><table><thead><tr><th scope="col" class="labels">Node</th><th scope="col" class="labels">Status</th><th scope="col" class="labels">Actions</th></tr></thead><tbody><tr><td data-label="Node">01</td><td data-label="Status"><span id="node1">%NODE01%</span></td><td data-label="Actions"> <button style="background-color: green; color: wheat;" onclick="update('1-1')"> DEPLOY </button> <button style="background-color: orange; color: wheat;" onclick="update('1-0')"> UNDEPLOY </button> <button style="background-color: red; color: wheat;" onclick="update('1-3')"> STOP </button></td></tr><tr><td data-label="Node">02</td><td data-label="Status"><span id="node2">%NODE02%</span></td><td data-label="Actions"> <button style="background-color: green; color: wheat;" onclick="update('2-1')"> DEPLOY </button> <button style="background-color: orange; color: wheat;" onclick="update('2-0')"> UNDEPLOY </button> <button style="background-color: red; color: wheat;" onclick="update('2-3')"> STOP </button></td></tr></tbody></table></div></body> <script>function update(value){var xhr=new XMLHttpRequest();var res=value.split("-");var url="/update?".concat("node=",res[0],"&","data=",res[1]);xhr.onreadystatechange=function(){if(this.readyState==4&&this.status==200){refreshNode(res[0]);}};xhr.open("GET",url,true);xhr.send();} function refreshNode(nodeNum){var xhr=new XMLHttpRequest();xhr.onreadystatechange=function(){if(this.readyState==4&&this.status==200){document.getElementById("node".concat(nodeNum)).innerHTML=this.responseText;}};xhr.open("GET","/poll?n=".concat(nodeNum),true);xhr.send();}</script> </html>)rawliteral";

Button emergencyStart(EMERGENCY_START);
Button emergencyStop(EMERGENCY_STOP);

void setup() {
  // Serial port for debugging purposes
  Serial.begin(115200);
  WiFi.softAP(ssid, password);
  delay(100);
  // Push button pin
  emergencyStart.init();
  emergencyStop.init();
  SPI.begin();
  radio.begin();
  radio.setDataRate(RF24_1MBPS);
  network.begin(90, this_node);  //(channel, node address)
  server.on("/", HTTP_GET, [](AsyncWebServerRequest * request) {
    // reset device state to not connected
    deviceState = {0};
    // flush old messages from the network
    flushMessages();
    // renew messages
    delay(500);
    // poll devices 10 times, dodgy work around to radio timing
    for (int i = 0; i < 10; i++) {
      pollDevices();
    }
    request->send_P(200, "text/html", index_html, processor);
  });
  server.on("/update", HTTP_GET, [](AsyncWebServerRequest * request) {
    uint16_t node = request->getParam(0)->value().toInt();
    int value = request->getParam(1)->value().toInt();
    Serial.print("Update : ");
    Serial.print(node);
    Serial.println(value);
    network.update();
    RF24NetworkHeader header(node);
    if (value == 0){
      value = UNDEPLOY;
    } else if (value == 1){
      value = DEPLOY;
    } else if (value == 3){
      value = STOP;
    }
    Serial.println(value);
    network.write(header, &value, sizeof(value));
    request->send(200);
  });
  server.on("/poll", HTTP_GET, [](AsyncWebServerRequest * request) {
    deviceState = {2, 2};
    // flush old messages from the network
    flushMessages();
    // renew messages
    delay(500);
    // poll devices 10 times, dodgy work around to radio timing
    for (int i = 0; i < 10; i++) {
      pollDevices();
    }
    uint16_t nodeNum = request->getParam(0)->value().toInt();
    switch (nodeNum) {
      case node01:
        request->send_P(200, "text/plain", nodeStatusText(deviceState.node1).c_str());
        break;
      case node02:
        request->send_P(200, "text/plain", nodeStatusText(deviceState.node2).c_str());
        break;
    }
  });
  server.begin();
  lcd.init();
  lcd.backlight();
  printLCD();
  delay(500);
}

void loop() {
  network.update();
  bool button_val = emergencyStop.onPress();
  if (button_val) {
    sendEmergencyStop();
    menu = 1;
    printLCD();
  }
  if (menu == 1) {
    button_val = emergencyStart.onPress();
    if (button_val) {
      menu = 2;
      emergencyMillis = millis();
      printLCD();
    }
  } else if (menu == 2) {
    if (millis() - emergencyMillis >= EMERGENCY_TIMEOUT) {
      menu = 1;
      printLCD();
    } else {
      button_val = emergencyStart.onPress();
      if (button_val) {
        menu = 3;
        printLCD();
      }
    }
  } else if (menu == 3) {
    button_val = emergencyStop.onPress();
    if (button_val) {
      sendEmergencyStop();
      menu = 1;
      printLCD();
    } else {
      sendEmergencyStart();
    }
  }
}

void printLCD() {
  lcd.clear();
  if (menu == 1){
    lcd.setCursor(0, 0);
    lcd.print("ENTER IN BROWSER");
    lcd.setCursor(0, 1);
    lcd.print(WiFi.softAPIP());
  } else if (menu == 2){
    lcd.setCursor(0, 0);
    lcd.print("EMERGENCY MODE");
    lcd.setCursor(0, 1);
    lcd.print("PRESS AGAIN");
  } else if (menu == 3){
    lcd.setCursor(0, 0);
    lcd.print("EMERGENCY MODE");
    lcd.setCursor(0, 1);
    lcd.print("ENGAGED");
  }
}

void sendEmergencyStart() {
  byte value = DEPLOY;
  RF24NetworkHeader header1(node01);
  network.write(header1, &value, sizeof(value));
  RF24NetworkHeader header2(node02);
  network.write(header2, &value, sizeof(value));
}

void sendEmergencyStop() {
  byte value = STOP;
  RF24NetworkHeader header1(node01);
  network.write(header1, &value, sizeof(value));
  RF24NetworkHeader header2(node02);
  network.write(header2, &value, sizeof(value));
}

// Replaces placeholder with DHT values
String processor(const String& var) {
  if (var == "NODE01") {
    return nodeStatusText(deviceState.node1);
  }
  else if (var == "NODE02") {
    return nodeStatusText(deviceState.node2);
  }
  return String();
}

String nodeStatusText(byte val) {
  switch (val) {
    case DEPLOYED:
      return F("DEPLOYED");
    case DEPLOYING:
      return F("DEPLOYING");
    case UNDEPLOYING:
      return F("UNDEPLOYING");
    case UNDEPLOYED:
      return F("UNDEPLOYED");
    case STOPPED:
      return F("STOPPED");
    default:
      return F("N/A");
  }
}

void pollDevices() {
  network.update();
  int i = 0;
  while (network.available()) {
    RF24NetworkHeader header;
    byte incomingData;
    network.read(header, &incomingData, sizeof(incomingData));
    if (header.from_node == 1) {
      deviceState.node1 = (byte) incomingData;
    }
    if (header.from_node == 2) {
      deviceState.node2 = (byte) incomingData;
    }
  }
}

void flushMessages() {
  network.update();
  for (int i = 0; i < 10; i++) {
    while (network.available()) {
      RF24NetworkHeader header;
      byte incomingData;
      network.read(header, &incomingData, sizeof(incomingData));
    }
  }
}
