#include <stdio.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include <Adafruit_TCS34725.h>

#define VERSION 42

char ssid[] = "derp42";
char ppwd[] = "oogaB00gaa";

unsigned int localPort = 4242;
WiFiUDP udp;

Adafruit_TCS34725 tcs = Adafruit_TCS34725(TCS34725_INTEGRATIONTIME_154MS, TCS34725_GAIN_4X);

#define SWARM_DEVICE_LIST_PRINT_INTERVAL  1000
uint32_t deviceListTimeStamp = millis();

//------------------------------------------------------------
#define SWARM_DEVICE_TIMEOUT    5000

typedef struct {
  IPAddress localIP;
  boolean   master;
  uint16_t  r, g, b, c, temp, lux;
  uint32_t  timeStamp;
} SWARM_DEVICE_DATA;

struct SWARM_DEVICE {
  SWARM_DEVICE_DATA data;
  SWARM_DEVICE      *next;
  SWARM_DEVICE      *prev;
};

// should malloc this?
SWARM_DEVICE rootDevice = {0};

void printDeviceList() {
  SWARM_DEVICE *deviceOut = &rootDevice;

  Serial.println("Device List:");
  Serial.println("------------------------------");
  while(deviceOut) {
    Serial.print("\t");
    Serial.print((uint32_t)deviceOut, HEX); Serial.print(" ");
    Serial.print((IPAddress)deviceOut->data.localIP);
    Serial.print("\tdeviceOut->prev: "); Serial.print((uint32_t)(deviceOut->prev), HEX);
    Serial.print("\tdeviceOut->next: "); Serial.println((uint32_t)(deviceOut->next), HEX);
    deviceOut = deviceOut->next;
  }
  Serial.println("------------------------------");
  Serial.println();
}

SWARM_DEVICE *findDevice(SWARM_DEVICE *deviceIn) {
  SWARM_DEVICE *deviceOut = &rootDevice;

  // Serial.println("findDevice()");
  while(NULL != deviceOut) {
    if (!memcmp((uint8_t *)&(deviceOut->data.localIP), (uint8_t *)&(deviceIn->data.localIP), sizeof(IPAddress))) {
      break;
    }
    deviceOut = deviceOut->next;
  }

  return deviceOut;
}

SWARM_DEVICE *addDevice(SWARM_DEVICE *deviceIn) {
  SWARM_DEVICE *deviceOut = &rootDevice;

  // Serial.println("addDevice()");
  while(deviceOut->next) {
    deviceOut = deviceOut->next;
  }

  deviceOut->next = (SWARM_DEVICE *)malloc(sizeof(SWARM_DEVICE));
  
  if (NULL != deviceOut->next) {
    memcpy((uint8_t *)(deviceOut->next), (uint8_t *)deviceIn, sizeof(SWARM_DEVICE));
    deviceOut->next->prev           = deviceOut;
    deviceOut->next->next           = NULL;
    deviceOut->next->data.timeStamp = millis();
  } else {
    Serial.println("Error Allocating Memory for Device List Entry!!");
  }

  return deviceOut;
}

void deleteDevice(SWARM_DEVICE *deviceIn) {
  SWARM_DEVICE *deviceOut = findDevice(deviceIn);

  if (NULL != deviceOut) {
    if (NULL != deviceOut->prev) {
      deviceOut->prev->next = deviceOut->next;
    }
    if (NULL != deviceOut->next) {
      deviceOut->next->prev = deviceOut->prev;
    }
    free(deviceOut);
  } else {
    Serial.println("deleteDevice(): Device not found!!");
  }

  return;
}

//------------------------------------------------------------
#define PACKET_EMPTY          0
#define PACKET_RESET          1
#define PACKET_UPDATE_COLOR   2

#define SWARM_PACKET_SEND_INTERVAL  1000
uint32_t packetSendTimeStamp = millis();

struct SWARM_PACKET {
  int cmd;
  SWARM_DEVICE device;
};

int           packetCount = 0;
SWARM_PACKET  swarmPacket = {0};

void swarmSendPacketUpdateColor() {
  IPAddress multiIP = rootDevice.data.localIP;

  // Serial.println("swarmSendPacketUpdateColor()");
  multiIP[3] = 255;
  memset(&swarmPacket, 0, sizeof(SWARM_PACKET));
  swarmPacket.cmd = PACKET_UPDATE_COLOR;
  memcpy(&(swarmPacket.device), &rootDevice, sizeof(SWARM_DEVICE));

  udp.beginPacketMulticast(multiIP, localPort, WiFi.localIP());
  udp.write((uint8_t *)&swarmPacket, sizeof(SWARM_PACKET)); 
  udp.endPacket(); 
}

//------------------------------------------------------------
void readLightColor(SWARM_DEVICE *swarmDevice) {
  // Serial.println("readLightColor()");
  tcs.getRawData((uint16_t *)&swarmDevice->data.r, (uint16_t *)&swarmDevice->data.g, (uint16_t *)&swarmDevice->data.b, (uint16_t *)&swarmDevice->data.c); 
  swarmDevice->data.temp  = tcs.calculateColorTemperature(swarmDevice->data.r, swarmDevice->data.g, swarmDevice->data.b); 
  swarmDevice->data.lux   = tcs.calculateLux(swarmDevice->data.r, swarmDevice->data.g, swarmDevice->data.b); 
}

void checkAndRemoveDevices() {
  SWARM_DEVICE *deviceOut = rootDevice.next;  // skip the root device

  // Serial.println("checkAndRemoveDevices()");
  while(deviceOut) {
    if ((millis() - deviceOut->data.timeStamp) > SWARM_DEVICE_TIMEOUT) {
      deleteDevice(deviceOut);
    }
    deviceOut = deviceOut->next;
  }
}

void checkAndSetMaster() {
  SWARM_DEVICE *deviceOut = &rootDevice;

  // Serial.println("checkAndSetMaster()");
  readLightColor(deviceOut);

  rootDevice.data.master = true;
  while(deviceOut->next) {
    deviceOut = deviceOut->next;
    
    if (rootDevice.data.lux < deviceOut->data.lux) {
      rootDevice.data.master = false;
      break;
    }
  }

  if (rootDevice.data.master) {
    digitalWrite(0, LOW);
  } else {
    digitalWrite(0, HIGH);
  }
}

void(*resetFunction)(void) = 0;

//------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("-------------------------");
  Serial.print("LightSwarm v");
  Serial.println(VERSION);
  Serial.print("Compiled at: "); 
  Serial.print (__TIME__); 
  Serial.print(" "); 
  Serial.println(__DATE__);  
  Serial.println("-------------------------");

  pinMode(0, OUTPUT);

  if (tcs.begin()) {
    Serial.println("Sensor Found ...");
  } else {
    Serial.println("Sensor NOT Found!!");
  }

  tcs.setInterrupt(true); // Disable color sensor light.

  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, ppwd);
  while (WiFi.status() != WL_CONNECTED) {
    delay(100);
    digitalWrite(0, HIGH);
    delay(100);
    digitalWrite(0, LOW);
    Serial.print(".");
  }

  Serial.println("\r\nWiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  // Initialize root device
  rootDevice.data.localIP = WiFi.localIP();
  
  Serial.println("Starting UDP");
  udp.begin(localPort);
  Serial.print("Local port: ");
  Serial.println(udp.localPort());
}

//------------------------------------------------------------
void loop() {
  SWARM_DEVICE *swarmDevice;
  
  if (udp.parsePacket()) {
    udp.read((uint8_t *)&swarmPacket, sizeof(SWARM_PACKET));
  }

  if (PACKET_EMPTY != swarmPacket.cmd) {
    if (NULL == findDevice(&(swarmPacket.device))) {
      addDevice(&(swarmPacket.device));
    }
  } else {
    // Serial.println("Waiting for packet ...");
  }
  
  // Check for PACKET_UPDATE_COLOR and update
  switch(swarmPacket.cmd) {
    case PACKET_EMPTY:
      break;
      
    case PACKET_UPDATE_COLOR:
      swarmDevice = findDevice(&(swarmPacket.device));

      if (NULL != swarmDevice) {
        memcpy(swarmDevice, &swarmPacket.device, sizeof(SWARM_DEVICE_DATA));
        swarmDevice->data.timeStamp = millis();
      } else {
        Serial.println("Device not found!");
      }
      break;

    case PACKET_RESET:
      resetFunction();
      break;
      
    default:
      Serial.println("Invalid Packet CMD!!");
      break;
  }

  // Check for devices no longer sending packets
  checkAndRemoveDevices();

  // Check color against list and set master (including light)
  checkAndSetMaster();
  
  // Send PACKET_UPDATE_COLOR Packet
  if ((millis() - packetSendTimeStamp) > SWARM_PACKET_SEND_INTERVAL) {
    swarmSendPacketUpdateColor();
    packetSendTimeStamp = millis();
  }

  if ((millis() - deviceListTimeStamp) > SWARM_DEVICE_LIST_PRINT_INTERVAL) {
    printDeviceList();
    deviceListTimeStamp = millis();
  }
}
