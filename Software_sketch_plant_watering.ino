#include <Wire.h> //Used for the I2C protocol
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_BMP280.h> //Used for the temperature and pressure sensor
#include <ESP8266WiFi.h> //Used to connect to the WiFi
#include <Servo.h> //Used to control the servo
#include <PubSubClient.h> //Used to connect to the MQTT broker, publish and subscribe
#include <ESP8266mDNS.h> //Used for OTA updates
#include <WiFiUdp.h> //Used for OTA updates
#include <ArduinoOTA.h> //Used for OTA updates
#include <WiFiManager.h> // Used for managing WIFI https://github.com/tzapu/WiFiManager


//OLED Display
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define OLED_RESET     -1
#define SCREEN_ADDRESS 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

//BMP280 sensor
#define BMP_SCK  (13)
#define BMP_MISO (12)
#define BMP_MOSI (11)
#define BMP_CS   (10)

Adafruit_BMP280 bmp;

//LDR and soil moisture sensors
int sensorPin = A0;
int selectPin = D3;
const int ldrSensor = LOW; //LOW selects LDR 
const int soilSensor = HIGH; //HIGH selects soil moisture sensor
int ldrValue = 0;
int soilValue = 0;

unsigned long messageDelay = 60000;
unsigned long timeNow;

//Servo
Servo myservo;

//Connecting to the WiFi and MQTT broker
WiFiManager wm;

const char* mqtt_server = "mqtt.uu.nl";
const char* mqtt_id = "student053";
const char* mqtt_password = "gH8YkBQN";

WiFiClient espClient;
PubSubClient client(espClient);
unsigned long lastMsg = 0;
#define MSG_BUFFER_SIZE  (50)
char msg[MSG_BUFFER_SIZE];

//General
int devideMode = 0; //0 = automatic, 1 = manual

//Setup device
void setup() {
  Serial.begin(115200);
  Serial.println("Booting");
  WiFi.mode(WIFI_STA);
  wm.setConfigPortalBlocking(true);
  wm.setConfigPortalTimeout(180);
  bool res = wm.autoConnect("AutoConnectAP");
  if(!res){
      Serial.println("Failed");
  }
  else {
      Serial.println("Connected to the WIFI");
  }
  
  setupOTAupdates(); //Configures the system for OTA updates
  
  client.setServer(mqtt_server, 1883); //Sets the MQTT server
  client.setCallback(callback);
  
  display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS); //Starting the OLED display
  display.clearDisplay();
  
  unsigned status;
  status = bmp.begin(BMP280_ADDRESS_ALT, BMP280_CHIPID); //Starting the BMP280 sensor
  /* Default settings from datasheet. */
  bmp.setSampling(Adafruit_BMP280::MODE_NORMAL,     /* Operating Mode. */
                  Adafruit_BMP280::SAMPLING_X2,     /* Temp. oversampling */
                  Adafruit_BMP280::SAMPLING_X16,    /* Pressure oversampling */
                  Adafruit_BMP280::FILTER_X16,      /* Filtering. */
                  Adafruit_BMP280::STANDBY_MS_500); /* Standby time. */
  pinMode(selectPin, OUTPUT); //Selects LDR or soil moisture sensor
  digitalWrite(selectPin, ldrSensor); //Selects the LDR sensor
  myservo.attach(2); //Attaches the servo on GIO2, which is D4 on the NodeMCU board
}

void loop() {
  ArduinoOTA.handle();
  int temperature = bmp.readTemperature();
  int pressure = bmp.readPressure();
  ldrValue = analogRead(sensorPin);
  readSoilMoisture();
  timeNow = millis();
  
  int pos = 0;
  myservo.write(pos);
  delay(15);

  if (!client.connected()) { //Check if client is connected to the MQTT broker, currently a blocking function
    reconnect(); //If client is not connected, reconnect to the MQTT broker first
  }
  client.loop();

  
  if (timeNow - lastMsg > messageDelay) {
    lastMsg = timeNow;
    publishSensor();
  }

  if (soilValue <= 200 && deviceMode == 0) {
    waterPlant();
  }
}

//Get sensor values
int readSoilMoisture() {
  digitalWrite(selectPin, soilSensor); //Select the Soil sensor
  soilValue = analogRead(sensorPin);
  digitalWrite(selectPin, ldrSensor); //Select the LDR sensor to prevent corrosion on the soil sensor
}

//Publish sensor values.
void publishSensor() { //QoS must be 0 due to PubSubClient library, this is fine for frequent updates of sensor values. However, QoS 1 would have been more ideal in the case of the manual reading of sensor values, to make sure the values are sent at least once.
    //Publish temperature
    snprintf (msg, MSG_BUFFER_SIZE, "%ld", temperature);
    client.publish("infob3it/053/temperature", msg);
    //Publish pressure
    snprintf (msg, MSG_BUFFER_SIZE, "%ld", pressure);
    client.publish("infob3it/053/pressure", msg);
    //Publish light level
    snprintf (msg, MSG_BUFFER_SIZE, "%ld", ldrValue);
    client.publish("infob3it/053/light", msg);
    //Publish moisture level
    snprintf (msg, MSG_BUFFER_SIZE, "%ld", soilValue);
    client.publish("infob3it/053/soil", msg);
}

//Water the plant
void waterPlant() {
  
}

//OTA updates
void setupOTAupdates() {
  // Port defaults to 8266
  // ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
  // ArduinoOTA.setHostname("myesp8266");

  // No authentication by default
  // ArduinoOTA.setPassword("admin");

  // Password can be set with it's md5 value as well
  // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
  // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_FS
      type = "filesystem";
    }

    // NOTE: if updating FS this would be the place to unmount FS using FS.end()
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
  });
  ArduinoOTA.begin();
  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

//MQTT functions
void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect("plantWateringApp", mqtt_id, mqtt_password, "infob3it/053/status", 0, true, "offline")) {
      Serial.println("connected");
      // Send online message
      client.publish("infob3it/053/status", "online", true);
      // Resubscribe
      client.subscribe("infob3it/053/water", 0); //Sends the command to water the plant. 
      client.subscribe("infob3it/053/read", 1);  //Sends command to read sensor values. QoS cannot be 0, as the command must be received at least once. Duplicate commands are not a problem as this will just send sensor values multiple times.
      client.subscribe("infob3it/053/mode", 1); //Automatic or manual mode. QoS cannot be 0, as the mode must be changed. Duplicate messages are not a problem as this will just set the device mode to the same mode twice.
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void callback(char* topic, byte* payload, unsigned int length) { //Do something with the arrived message
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();

  if (topic == "infob3it/053/water") {
    waterPlant();
  }

  if (topic == "infob3it/053/read") { //
    publishSensor();
  }

  if (topic == "infob3it/053/state") { //Set the devide mode to manual (1) or automatic (0). 
    if ((char)payload[0] == '1') {
      deviceMode = 1;
      Serial.println("Set to manual mode");
    }
    else {
      deviceMode = 0;
      Serial.println("Set to automatic mode");
    }
  }
}
