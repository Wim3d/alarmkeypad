/* 
  Alarm keypad version 4
  Written by W. Hoogervorst, nov 2019
  Based on examples in Keypad.h library, such as Eventkeypad by Alexander Brevig
*/
#include <Adafruit_NeoPixel.h>
#include <Keypad.h>

#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ESP8266mDNS.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <credentials.h>  //definitions of mySSID, myPASSWORD, mqtt_server 

long lastReconnectAttempt, lastBlink = 0;

// for HTTPupdate
const char* host = "Keypad1";
ESP8266WebServer httpServer(80);
ESP8266HTTPUpdateServer httpUpdater;
const char* software_version = "version 4";

/*credentials & definitions */
//MQTT
const char* mqtt_id = "Keypad1";
const char* ALARM_st_topic = "alarm/main/state";
const char* ALARM_com_topic = "alarm/main/set";

boolean alarm = false;

// NeoPixels
#define PIN            15 // neopixel attached to GPIO15
#define NUMPIXELS      1
Adafruit_NeoPixel pixels = Adafruit_NeoPixel(NUMPIXELS, PIN, NEO_GRB + NEO_KHZ800);

String tmp_str; // String for publishing the int's as a string to MQTT
char buf[5];

WiFiClient espClient;
PubSubClient client(espClient);

float voltage;

// keypad
const byte ROWS = 4; //four rows
const byte COLS = 4; //four columns
char keys[ROWS][COLS] = {
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'}
};
char oncode[] = {'1', '2', '3', '4'};   // this is the key sequence to enable the alarm
char offcode[] = {'4', '3', '2', '1'};  // this is the key sequence to disable the alarm
char inputcode[20];
char startkey = '*';  // the sequencing starts at this key
char endkey = '#';    // the sequencing ends at this key
int count;

byte rowPins[ROWS] = {5, 4, 0, 2}; //these GPIO pins connect to the row pinouts of the keypad
byte colPins[COLS] = {16, 14, 12, 13}; //these GPIO pins connect to the column pinouts of the keypad

boolean verification = false, input = false;

Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

void setup() {
  Serial.begin(115200);
  Serial.println("\n\nProgram started\n");
  Serial.printf("lenght of oncode: %d\n", sizeof(oncode));
  Serial.printf("lenght of offcode: %d\n", sizeof(offcode));
  pixels.begin(); // This initializes the NeoPixel library.


  // 3.6 - 4.2 is 0 % - 100 %
  // A0 can read 0 - 1V as 0 - 1023, battery voltage is max 4.2V, so use a voltage divider.
  // using voltage divider of 20K and 68K, a voltage 1V at the analog pin refers to 4.4, so 4.4 V at the input of the voltage divider results in a reading of 1023
  for (int i = 0; i < 10; i++) //take 10 measurements
    voltage += map(analogRead(A0), 0, 1023, 0, 440); // analog value 0 - 1023 and map to 440 (4.4V * 100)
  voltage = voltage / (float)1000;      // divide by 100 for converting to value to a float with 2 digits, divide by 10 for averaging measurements
  setup_wifi();
  // MQTT
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
  if (!client.connected())
    reconnect();
  tmp_str = String(voltage); //converting temperature to a string
  tmp_str.toCharArray(buf, tmp_str.length() + 1);
  client.publish("keypad/voltage", buf);

  if (voltage < 3.3) // voltage to low
  {
    boolean blinker = false;
    pixels.setPixelColor(0, pixels.Color(150, 0, 0)); //red
    client.publish("keypad/voltage", "voltage to low");
  }
  else
    client.publish("keypad/voltage", "voltage ok");
  // for HTTPudate
  MDNS.begin(host);
  httpUpdater.setup(&httpServer);
  httpServer.begin();
  MDNS.addService("http", "tcp", 80);
  httpServer.on("/", handleRoot);
}

void loop()
{
  if (client.connected())
  {
    // Client connected
    client.loop();
    httpServer.handleClient();    // for HTTPupdate
    char key = keypad.getKey();
    if (key) Serial.println(managekey(key));
    if (verification) verificate_input();
  }
  else
    // Client is not connected
  {
    long now = millis();
    long now2 = millis();

    if (now2 - lastBlink > 500) {          // blink the LED while not connected to MQTT
      digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
      Serial.println("blink loop");
      lastBlink = now2;
    }

    if (now - lastReconnectAttempt > 10000) {
      lastReconnectAttempt = now;
      // Attempt to reconnect
      if (reconnect()) {
        lastReconnectAttempt = 0;
      }
    }
  }
}


String managekey(char key)
{
  String started = "started code input";
  String ended = "ended code input";
  String digit = "";
  Serial.println(key);
  if (key == startkey && !input)
  {
    //Serial.println("started code input");
    input = true;
    verification = false;
    count = 0;
    return started;
  }

  if (key == endkey)
  {
    //Serial.println("ended code input");
    input = false;
    verification = true;
    return ended;
  }
  if (input && key != startkey)
  {
    inputcode[count] = key;
    //Serial.printf("digit: %d is entered: %c\n", count + 1, key);
    //Serial.printf("count: %d\n", count);
    digit += "digit: ";
    digit += count + 1;
    digit += " is entered: ";
    digit += key;
    count++;
  }
  return digit;
}


void verificate_input()
{
  if (count < 4)
  {
    Serial.println("too little digits");
    verification = false;
  }
  else if (count == 4)
  {
    Serial.println("right number off digits");
    verification = true;
  }
  else
  {
    Serial.println("too many digits");
    verification = false;
  }

  if (verification)
  {
    Serial.printf("count = %d\n", count);
    count = 0;
    verification = false;
    int okcount = 0;
    Serial.println("check oncode");
    for (int i = 0; i < 4; i++)
    {
      if (inputcode[i] == oncode[i])
      {
        Serial.printf("digit: %d OK\n", i + 1);
        okcount++;
      }
      else
        Serial.printf("digit: %d is not OK\n", i + 1);
    }
    Serial.printf("%d digits are OK\n", okcount);
    if (okcount == 4)
    {
      Serial.println("code is oncode");
      client.publish(ALARM_com_topic, "ON");
    }
    else
    {
      okcount = 0;
      Serial.println("check offcode");
      for (int i = 0; i < 4; i++)
      {
        if (inputcode[i] == offcode[i])
        {
          Serial.printf("digit: %d OK\n", i + 1);
          okcount++;
        }
        else
          Serial.printf("digit: %d is not OK\n", i + 1);
      }
      Serial.printf("%d digits are OK\n", okcount);
      if (okcount == 4)
      {
        Serial.println("code is offcode");
        client.publish(ALARM_com_topic, "OFF");        
      }
      else
      {
        Serial.println("code is not a known code");
      }
    }
  }
}

void setup_wifi() {
  delay(10);
  // We start by connecting to a WiFi network
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(mySSID);
  pixels.setPixelColor(0, pixels.Color(0, 0, 150));
  WiFi.begin(mySSID, myPASSWORD);
  boolean blinker = false;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    if (blinker)
      pixels.setBrightness(32);
    else
      pixels.setBrightness(255);
    pixels.show();
    blinker = !blinker;
    Serial.print(".");
  }
  pixels.setBrightness(255);
  pixels.setPixelColor(0, pixels.Color(100, 100, 100));
  pixels.show();
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

boolean reconnect()
{
  if (WiFi.status() != WL_CONNECTED) {    // check if WiFi connection is present
    setup_wifi();
  }
  Serial.println("Attempting MQTT connection...");
  if (client.connect(mqtt_id)) {
    Serial.println("connected");
    // ... and resubscribe
    client.subscribe(ALARM_st_topic);    
  }
  Serial.println(client.connected());
  return client.connected();
}

void handleRoot() {
  Serial.println("Connected to client");
  httpServer.send(200, "text/html", SendHTML());
}

String SendHTML() {
  String ptr = "<!DOCTYPE html> <html>\n";
  ptr += "<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0, user-scalable=no\">\n";
  ptr += "<title>";
  ptr += mqtt_id;
  ptr += "</title>\n";
  ptr += "<style>html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}\n";
  ptr += "body{margin-top: 50px;} h1 {color: #444444;margin: 25px auto 30px;} h3 {color: #444444;margin-bottom: 30px;}\n";
  ptr += "p {font-size: 18px;color: #383535;margin-bottom: 15px;}\n";
  ptr += "</style>\n";
  ptr += "</head>\n";
  ptr += "<body>\n";
  ptr += "<h1>Keypad alarm Webpage</h1>\n";
  ptr += "<h3>Alarm status</h3>\n";
  ptr += "<p>WimIOT\nDevice: ";
  ptr += mqtt_id;
  ptr += "<br>Software version: ";
  ptr += software_version;
  ptr += "<p>Voltage: ";
  ptr += voltage;
  ptr += "<br>Alarm: ";
  if (alarm)
    ptr += "ON";
  else
    ptr += "OFF";
  ptr += "<br><br></p>";
  ptr += "<p>Click for update page</p><a class=\"button button-update\" href=\"/update\">Update</a>\n";
  ptr += "</body>\n";
  ptr += "</html>\n";
  return ptr;
}

void callback(char* topic, byte * payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();

  if ((char)topic[6] == 'm')          // check for the 'm' in alarm/main/state
  {
    if ((char)payload[1] == 'N')      // ON
    {
      alarm = true;
      pixels.setPixelColor(0, pixels.Color(150, 0, 0));
    }
    else if ((char)payload[1] == 'F') // OFF
    {
      alarm = false;
      pixels.setPixelColor(0, pixels.Color(0, 150, 0));
    }
    pixels.show();
  }
}
