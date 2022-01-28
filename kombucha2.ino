/*
 * Create by Chris Toews
 * https://github.com/cdtoews
 * 
 * This is for controlling a relay that has a light attached to it,
 * I use this for my kombucha cabinet. There is a high and low temp setting
 * If the current temperature is between the high and low settings
 * the relay will go off and on every 20 minutes or so.
 * The percentage of on vs off will chage as it either goes outside of the acceptable range
 * 
 * You will need a library called arduino_secrets.h
 * this will need the following
 * 
// for adafruit:
#define IO_USERNAME  "adafruit_user"
#define IO_KEY       "adafruit_io_key"


//for Blynk
#define BLYNK_TEMPLATE_ID "blynk_template_ID"
#define BLYNK_DEVICE_NAME "blynk_device_name"
#define BLYNK_AUTH_TOKEN "blynk_auth_token"


// for different libraries
#define WIFI_SSID "your_ssis"
#define WIFI_PASS "your_wifi_pass"

 * 
 * 
 */


#include <dht11.h>
#include <Adafruit_AHTX0.h>
const int sensorIndicator = 2; // 1 = DHT11, 2=AHT20
#include <LiquidCrystal.h>
#include <arduino-timer.h>
auto timer = timer_create_default(); // create a timer with default settings
long BlynkTimerRepeat = 60000; // every minute
long AdafruitTimerRepeat = 60000; //every minute
long AdafruitPullTimerRepeat = 60000; //every minute
long AdafruitTriggerPullTimerRepeat = 3600000;// every hour
long rebootTimer = 3600000;//how long before we reboot ourselves, 3600000 = 1 hour

long readTempRepeat = 15000;
bool updateToBlynk = true;
bool updateToAdafruit = true;
bool updateFromAdafruit = true;

#include <arduino_secrets.h>
#include<BlynkSimpleWiFiNINA.h>
#include "Adafruit_MQTT.h"
#include "Adafruit_MQTT_Client.h"

Adafruit_AHTX0 aht;

// You should get Auth Token in the Blynk App.
char auth[] = BLYNK_AUTH_TOKEN;

// Your WiFi credentials.
char ssid[] = WIFI_SSID;
char pass[] = WIFI_PASS;

/************************* Adafruit.io Setup *********************************/
#define AIO_SERVER      "io.adafruit.com"
#define AIO_SERVERPORT  1883                   // use 8883 for SSL
#define AIO_USERNAME    IO_USERNAME
#define AIO_KEY         IO_KEY

WiFiClient client;

// Setup the MQTT client class by passing in the WiFi client and MQTT server and login details.
Adafruit_MQTT_Client mqtt(&client, AIO_SERVER, AIO_SERVERPORT, AIO_USERNAME, AIO_KEY);

/****************************** Feeds ***************************************/

// Setup a feed called 'photocell' for publishing.
// Notice MQTT paths for AIO follow the form: <username>/feeds/<feedname>
Adafruit_MQTT_Publish tempPub = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/temperature");
Adafruit_MQTT_Publish humidityPub = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/humiditiy");
Adafruit_MQTT_Publish lightPub = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/light");

// Setup a feed subscribing to changes.
Adafruit_MQTT_Publish sethightempGet = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/kombuha.hightemp/get");
Adafruit_MQTT_Publish setlowtempGet = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/kombuha.lowtemp/get");
Adafruit_MQTT_Subscribe sethightemp = Adafruit_MQTT_Subscribe(&mqtt, AIO_USERNAME "/feeds/kombuha.hightemp");
Adafruit_MQTT_Subscribe setlowtemp = Adafruit_MQTT_Subscribe(&mqtt, AIO_USERNAME "/feeds/kombuha.lowtemp");


dht11 DHT11;
#define DHT11PIN 7
#define RELAYPIN 8
#define RESETPIN 10


float tempF = -1.0;
const int tempsToRound = 4;
float temps[tempsToRound];
float humidities[tempsToRound];
int tempAnomalyCount = 0;
float humidity;
int tempsArrayIndicator = 0; //keep track of which element in array we are updating, this will be for rounding temps
//these are initial and default settings and will be overwritten from adafruit (if adafruit is enabled
int highSetTemp = 79; //default setting, possibly overwritten from adafruit
int lowSetTemp = 77; //default setting possibly overwritten from adafruit



long lowTempCycleDuration = 1800000; //amount of time to cyle on&off when between high and low temps//600000 10 minutes, 3600000 1 hour
int lowTempCyclePercentUp = 45; //good starting point.
long cycleUpTime; //this will be calculated
long cycleDownTime;//this will be calculated

long cycleLastChange = millis();


long startMillis = millis();
long lastChangeMillis = millis();
int relayState; //read from actual pin:RELAYPIN, 0=off 1=on
int currentStatus = -2; //what the status is 0=off, 1=on, -1=low temp cycle (intermittent on and off)
char currentState[4];


// initialize the library by associating any needed LCD interface pin
// with the arduino pin number it is connected to
const int rs = 12, en = 11, d4 = 5, d5 = 4, d6 = 3, d7 = 2;
LiquidCrystal lcd(rs, en, d4, d5, d6, d7);

void MQTT_connect();

void setup() {
  //get ready for manual reset
  digitalWrite(RESETPIN, HIGH);
  pinMode(RESETPIN, OUTPUT);

  Serial.begin(9600);
  Blynk.begin(auth, ssid, pass);

  //subscribe to settings
  mqtt.subscribe(&sethightemp);
  mqtt.subscribe(&setlowtemp);

  if (! aht.begin()) {
    Serial.println("Could not find AHT? Check wiring");
    printRow(1, "AHT20 not found");
    delay(10000);
    printRow(1, "Do Something");
    delay(10000);
  }

  pinMode(RELAYPIN, OUTPUT);     //Set relay pin as output
  // set up the LCD's number of columns and rows:
  lcd.begin(16, 2);
  printRow(0, "Calibrating");
  printRow(1, "Flux Capacitor");
  //some setup of variables
  cycleUpTime = (lowTempCycleDuration * lowTempCyclePercentUp) / 100;
  cycleDownTime = lowTempCycleDuration - cycleUpTime;
  Serial.println("##############");
  Serial.println("doing setup");
  Serial.println("##############");
  //give dht11 a few seconds to get its bearings and poll it, get a full reading, and send

  if (updateFromAdafruit) {
    timer.in(7500, triggerGetFromAdafruit);
    timer.in(9000, pullFromAdafruit);
  }

  timer.in(5000, readTemp);
  timer.in(7000, readTemp);
  timer.in(9000, readTemp);
  timer.in(11000, readTemp);
  if (updateToBlynk) {
    timer.in(13000, updateBlynk);
  }
  if (updateToAdafruit) {
    timer.in(17000, updateAdafruit);
  }

  timer.in(rebootTimer, rebootFunc);//reboot in an hour. until I get everything happy I need to reset it



  timer.every(readTempRepeat, readTemp);

  if (updateToBlynk) {
    delay(5000);
    timer.every(BlynkTimerRepeat, updateBlynk);
  }
  if (updateToAdafruit) {
    delay(5000);
    timer.every(AdafruitTimerRepeat, updateAdafruit);
  }
  if (updateFromAdafruit) {
    delay(2000);
    timer.every(AdafruitTriggerPullTimerRepeat, triggerGetFromAdafruit);
    delay(2000);
    timer.every(AdafruitPullTimerRepeat , pullFromAdafruit);
  }


}



void loop() {
  timer.tick();
}


bool rebootFunc( void *) {
  printRow(1, "Going to Reboot");
  delay(2000);
  //bring reset pin low to trigger reset
  digitalWrite(RESETPIN, LOW);
  return false; //this will never run (unless wires are disconnected
}

bool triggerGetFromAdafruit(void *) {
  Serial.println("triggering a get to adafruit hightemp and lowtemp");
  MQTT_connect();


  //let's do low temp first
  //first we need to publish to the /get feed
  Serial.print("Sending to low temp get ... ");
  if (! setlowtempGet.publish(0.0)) {
    Serial.println(F("Failed"));
  } else {
    Serial.println(F("OK!"));
  }

  //now let's do the high temp
  Serial.print("Sending to high temp get ... ");
  if (! sethightempGet.publish(0.0)) {
    Serial.println(F("Failed"));
  } else {
    Serial.println(F("OK!"));
  }
}

bool pullFromAdafruit(void *) {
  Serial.println("going to update FROM adafruit");
  MQTT_connect();
  // this is our 'wait for incoming subscription packets' busy subloop
  Adafruit_MQTT_Subscribe *subscription;
  while ((subscription = mqtt.readSubscription(3000))) {
    // Check high temp first
    if (subscription == &sethightemp) {
      Serial.print("%%%%%%%%%%%%%%%%%%%%%%%%% high temp pulled from subscription : ");
      Serial.print((char *)sethightemp.lastread);
      Serial.print("   SET high temp ");
      highSetTemp =  atoi((char *)sethightemp.lastread); 
      Serial.println(highSetTemp);
      printRowInt(1, "hi temp: ", highSetTemp);
      delay(2000);
    }

    // check low temp now
    if (subscription == &setlowtemp) {
      Serial.print("%%%%%%%%%%%%%%%%%%%%%%% got LOW temp pulled from subscription: ");
      Serial.print((char *)setlowtemp.lastread);
      Serial.print("   SET low temp ");
      lowSetTemp = atoi((char *)setlowtemp.lastread); 
      Serial.println(lowSetTemp);
      printRowInt(1, "lo temp: ",  lowSetTemp);
      delay(2000);
    }
  }
  return true;
}


bool updateAdafruit(void *) {
  Serial.println("updating Adafruit");

  printRow(1, "Updating Adafruit");
  MQTT_connect();

  Serial.println("going to update TO adafruit");
  uint32_t unsignedRelayState = digitalRead(RELAYPIN);

  // Now we can publish stuff!
  Serial.print("Sending temp to adafruit val ");
  Serial.print(tempF);
  Serial.print("...");
  if (! tempPub.publish(tempF)) {
    Serial.println(F("Failed"));
  } else {
    Serial.println(F("OK!"));
  }

  // Now we can publish stuff!
  Serial.print("Sending humidity val ");
  Serial.print(humidity);
  Serial.print("...");
  if (! humidityPub.publish(humidity)) {
    Serial.println(F("Failed"));
  } else {
    Serial.println(F("OK!"));
  }

  // Now we can publish stuff!
  Serial.print("Sending relay state val ");
  Serial.print("relay");
  Serial.print("...");
  if (! lightPub.publish(unsignedRelayState)) {
    Serial.println(F("Failed"));
  } else {
    Serial.println(F("OK!"));
  }

}

bool updateBlynk(void *) {
  if (!updateToBlynk) {
    Serial.println("not updating blynk");
    return false;
  }
  printRow(1, "Updating Blynk");
  if (Blynk.connected()) {
    Blynk.run();
  } else {
    Blynk.connect();  // Try to reconnect to the server
    Serial.println("###had to reconnect to Blynk");
  }
  Serial.println("Updating Blynk");
  Blynk.virtualWrite(V5, tempF);
  //uptime minutes
  int uptimeMinutes = (millis() - startMillis) / 60000;
  Serial.print("uptimeMinutes: ");
  Serial.println(uptimeMinutes);
  Blynk.virtualWrite(V2, uptimeMinutes);
  Blynk.virtualWrite(V7, humidity);
  relayState = digitalRead(RELAYPIN);
  Blynk.virtualWrite(V6, relayState);

  return true; //repeat? true
}

void checkThresholds() {

  //get current relay state
  relayState = digitalRead(RELAYPIN);

  //check current state against current temp
  if (tempF < lowSetTemp) {
    if (currentStatus != 1) {
      //under lowSetTemp but not full on
      currentStatus = 1;
      incrementIntermittentSettings(1);//bring pecentage up of how much is runs up when intermittnet
    }
    digitalWrite(RELAYPIN, HIGH); //turn on relay

  } else if (tempF > highSetTemp) {
    if (currentStatus != 0) {
      //over highSetTemp but not full off
      currentStatus = 0;
      incrementIntermittentSettings(-1);//bring pecentage down of how much is runs up when intermittnet
    }
    digitalWrite(RELAYPIN, LOW); //turn off relay

  } else {
    //we are between high and low
    if (currentStatus != -1) {
      //we just jumped into intermittent cycling
      //leave relay as it was
      currentStatus = -1;
      cycleLastChange = millis(); //reset our counter

    } else {
      //we were already in mid-cycle, let's check status duration
      if (relayState == 1) {
        if ((millis() - cycleLastChange) > cycleUpTime) {
          printRow(1, "#setting int OFF");
          delay(2000);
          digitalWrite(RELAYPIN, LOW); //turn off relay
          cycleLastChange = millis();
        } else {
          digitalWrite(RELAYPIN, HIGH);
        }

      } else {
        //relayState == 0  or weirdness
        if ((millis() - cycleLastChange) > cycleDownTime) {
          printRow(1, "#setting int ON");
          delay(2000);
          digitalWrite(RELAYPIN, HIGH); //turn on relay
          cycleLastChange = millis();
        } else {
          digitalWrite(RELAYPIN, LOW);
        }

      }
    }
  }//end of if/else for temps
}

void updateLCDstatus() {
  printRow(1, "Updating LCD");
  lcd.setCursor(0, 0);
  lcd.print("                 ");
  lcd.setCursor(0, 0);

  lcd.print(String(tempF, 2));
  lcd.print(char(223));
  lcd.print(" ");

  lcd.print(String(humidity, 1));
  lcd.print("%");

  if (currentStatus == 1) {
    strcpy(currentState, " U");
  } else if (currentStatus == 0) {
    strcpy(currentState, " D");
  } else if (currentStatus == -1) {
    relayState = digitalRead(RELAYPIN);
    if (relayState == 1) {
      strcpy(currentState, " ~U");
    } else {
      strcpy(currentState, " ~D");
    }

  } else {
    strcpy(currentState, " ?");
  }
  lcd.print(currentState );

}

bool readTemp(void *) {
  Serial.println("reading temp");


  float tempC;
  float currentTempF;
  if (sensorIndicator == 1) {
    int chk = DHT11.read(DHT11PIN);
    tempC = DHT11.temperature;
    currentTempF = roundFloat(float((tempC * 1.8) + 32));
    temps[tempsArrayIndicator] = currentTempF;
    humidities[tempsArrayIndicator++] = DHT11.humidity;
  } else if (sensorIndicator == 2) {
    //we have AHT20
    sensors_event_t humidity, temp;
    aht.getEvent(&humidity, &temp);// populate temp and humidity objects with fresh data
    tempC = temp.temperature;
    currentTempF = roundFloat(float((tempC * 1.8) + 32));

    //check if read temp is anomalous temperature reading
    //anomalous being defined as the temp being (25 * (tempAnomalyCount + 1))% off of the previous reading
    float tempDiffPercent = (abs(currentTempF - tempF)) / tempF;
    //if it's not the first rading, and it's anomalous, we will ignore it. 
    //each subsequent anomalous reading adds 10% to allowed range of non-anomalous readings
    if (tempF != -1.0 && tempDiffPercent > ((tempAnomalyCount + 1) * .1)) {
      printRow(1, "Anomalous Temp");
      Serial.println("########################");
      Serial.println("we got an anomalous reading");
      Serial.print("last temp reading : ");
      Serial.print(tempF);
      Serial.print("   currentTempReading in F :");
      Serial.print(currentTempF);
      Serial.print("   current reading in C :");
      Serial.print(tempC);
      Serial.print("   anomaly count: ");
      Serial.println(tempAnomalyCount);
      Serial.println("########################");
      tempAnomalyCount += 1;


      return true;
    } else {
      tempAnomalyCount = 0;
    }


    temps[tempsArrayIndicator] = currentTempF;
    humidities[tempsArrayIndicator++] = humidity.relative_humidity;
    Serial.print("AHT20 Temperature: ");
    Serial.print(temp.temperature);
    Serial.print(" degrees C   "    );
    Serial.print("Humidity: ");
    Serial.print(humidity.relative_humidity);
    Serial.println("% rH");
  } else {
    printRow(1, "Sensor Error");
  }




  Serial.print("array indicator: ");
  Serial.println(tempsArrayIndicator, DEC);
  //put temp on lcd without strings
  lcd.setCursor(0, 1);
  lcd.print("                   ");
  lcd.setCursor(0, 1);
  char indicator[] = "_";
  lcd.print("temp:");
  lcd.print(indicator);
  for (int i = 0; i < tempsArrayIndicator; i++) {
    lcd.print(indicator);
  }
  lcd.print(String(currentTempF, 2));


  Serial.println("temp: " + String(currentTempF, 2));



  //let's see if we have tempsToRound temps to average
  if (tempsArrayIndicator >= tempsToRound) {
    float tempTotal = 0.0;
    int humidityTotal = 0;
    for (int i = 0; i < tempsToRound; i++) {
      tempTotal += temps[i];
      humidityTotal += humidities[i];
    }
    float roundedTemp = roundFloat(tempTotal / tempsToRound);
    tempF = roundedTemp;
    Serial.println("new Temp: " + String(tempF, 2));
    float roundedHumidity = roundFloat(humidityTotal / tempsToRound);
    humidity = roundedHumidity;
    Serial.println("new Humdidity: " + String(humidity, 1));
    checkThresholds();//see if we need to change relay states
    updateLCDstatus();
    tempsArrayIndicator = 0;
  }
  return true;
}

void incrementIntermittentSettings(int increaseUp) {

  lowTempCyclePercentUp += increaseUp;
  cycleUpTime = (lowTempCycleDuration * lowTempCyclePercentUp) / 100;
  cycleDownTime = lowTempCycleDuration - cycleUpTime;
}


float roundFloat(float x) {
  x = x + 0.005;
  x = x * 100;
  int y = (int)x;
  float z = (float)y / 100;
  return z;
}

void printRow(int rowNum, char toPrint[]) {
  lcd.setCursor(0, rowNum);
  lcd.print("                   ");
  lcd.setCursor(0, rowNum);
  lcd.print(toPrint);
}

void printRowInt(int rowNum, char printFirst[], int toPrint) {
  lcd.setCursor(0, rowNum);
  lcd.print("                   ");
  lcd.setCursor(0, rowNum);
  lcd.print(printFirst);
  lcd.print(toPrint,DEC);
}

// Function to connect and reconnect as necessary to the MQTT server.
// Should be called in the loop function and it will take care if connecting.
void MQTT_connect() {
  int8_t ret;

  // Stop if already connected.
  if (mqtt.connected()) {
    return;
  }

  Serial.print("Connecting to MQTT... ");

  uint8_t retries = 3;
  while ((ret = mqtt.connect()) != 0) { // connect will return 0 for connected
    Serial.println(mqtt.connectErrorString(ret));
    Serial.println("Retrying MQTT connection in 5 seconds...");
    mqtt.disconnect();
    delay(5000);  // wait 5 seconds
    retries--;
    if (retries == 0) {
      // basically die and wait for WDT to reset me
      while (1);
    }
  }
  Serial.println("MQTT Connected!");
}
