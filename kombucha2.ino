/*
   Create by Chris Toews
   https://github.com/cdtoews

   This is for controlling a relay that has a light attached to it,
   I use this for my kombucha cabinet. There is a high and low temp setting
   If the current temperature is between the high and low settings
   the relay will go off and on every 20 minutes or so.
   The percentage of on vs off will chage as it goes outside of the acceptable range

   I originally used a DHT11, but i now use an AHT20. you can use either by
   enabling either code by changing value of sensorIndicator

   You will need a library called arduino_secrets.h
   this will need the following

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



*/



#define DEBUG 0

# if DEBUG == 1
#define debugSerial(x) debugSerial(x)
#define debugSerialln(x) debugSerialln(x)
#else
#define debugSerial(x)
#define debugSerialln(x)
#endif


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
long rebootTimer = 7200000;//how long before we reboot ourselves, 3600000 = 1 hour

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
Adafruit_MQTT_Publish thermostatStatusPub = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/kombuha.thermostatstatus");
Adafruit_MQTT_Publish lowTempPercentagePub = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/kombuha.lowtemppercentage");

//publishes for getting latest
Adafruit_MQTT_Publish sethightempGet = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/kombuha.hightemp/get");
Adafruit_MQTT_Publish setlowtempGet = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/kombuha.lowtemp/get");
Adafruit_MQTT_Publish lowTempPercentageGet = Adafruit_MQTT_Publish(&mqtt, AIO_USERNAME "/feeds/kombuha.lowtemppercentage/get");

//subscriptions
Adafruit_MQTT_Subscribe sethightemp = Adafruit_MQTT_Subscribe(&mqtt, AIO_USERNAME "/feeds/kombuha.hightemp");
Adafruit_MQTT_Subscribe setlowtemp = Adafruit_MQTT_Subscribe(&mqtt, AIO_USERNAME "/feeds/kombuha.lowtemp");
Adafruit_MQTT_Subscribe lowTempPercentageSub = Adafruit_MQTT_Subscribe(&mqtt, AIO_USERNAME "/feeds/kombuha.lowtemppercentage");



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
int lowTempCyclePercentUp = 50; //good starting point.
int lowTempPercentageChange = 3;
long cycleUpTime; //this will be calculated
long cycleDownTime;//this will be calculated

long cycleLastChange = millis();


long startMillis = millis();
long lastChangeMillis = millis();
int relayState; //read from actual pin:RELAYPIN, 0=off 1=on
const int statusInitializing = -2;
const int statusOff = 0;
const int statusLow = 1;
const int statusHigh = 2;
int currentStatus = statusInitializing; //what the status is 0=off, 1=on, -1=low temp cycle (intermittent on and off)
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
  mqtt.subscribe(&lowTempPercentageSub);

  if (! aht.begin()) {
    debugSerialln("Could not find AHT? Check wiring");
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
  debugSerialln("##############");
  debugSerialln("doing setup");
  debugSerialln("##############");
  //give dht11 a few seconds to get its bearings and poll it, get a full reading, and send

  if (updateFromAdafruit) {
    //    timer.in(5000, getLowTempPercentage);
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

//############ Reboot Function  ##############
bool rebootFunc( void *) {
  debugSerialln(F("%%%%%%%%%% about to reboot %%%%%%%%%%%%%%%%"));
  printRow(1, "Going to Reboot");
  delay(2000);
  //bring reset pin low to trigger reset
  digitalWrite(RESETPIN, LOW);
  return false; //this will never run (unless wires are disconnected
}

//############ Trigger Get from Adafruit  ##############
bool triggerGetFromAdafruit(void *) {
  debugSerialln("triggering a get to adafruit hightemp and lowtemp");
  MQTT_connect();


  //let's do low temp first
  //first we need to publish to the /get feed
  debugSerial("Sending to low temp get ... ");
  if (! setlowtempGet.publish(0.0)) {
    debugSerialln(F("Failed"));
  } else {
    debugSerialln(F("OK!"));
  }

  //now let's do the high temp
  debugSerial("Sending to high temp get ... ");
  if (! sethightempGet.publish(0.0)) {
    debugSerialln(F("Failed"));
  } else {
    debugSerialln(F("OK!"));
  }

  debugSerial("Sending to low percentage get ... ");
  if (! lowTempPercentageGet.publish(0.0)) {
    debugSerialln(F("Failed"));
  } else {
    debugSerialln(F("OK!"));
  }

}

//############ Pull from Adafruit  ##############
bool pullFromAdafruit(void *) {
  debugSerialln("going to update FROM adafruit");
  MQTT_connect();
  // this is our 'wait for incoming subscription packets' busy subloop
  Adafruit_MQTT_Subscribe *subscription;
  while ((subscription = mqtt.readSubscription(5000))) {

    if (subscription == &lowTempPercentageSub) {
      lowTempCyclePercentUp =  atoi((char *)lowTempPercentageSub.lastread);
      cycleUpTime = (lowTempCycleDuration * lowTempCyclePercentUp) / 100;
      cycleDownTime = lowTempCycleDuration - cycleUpTime;
      debugSerial(F("New low temp cycle, cycle up Percent:"));
      debugSerialln(lowTempCyclePercentUp);
      delay(1000);
    }

    // Check high temp first
    if (subscription == &sethightemp) {
      debugSerial("high temp pulled from subscription : ");
      debugSerial((char *)sethightemp.lastread);
      debugSerial("   SET high temp ");
      highSetTemp =  atoi((char *)sethightemp.lastread);
      debugSerialln(highSetTemp);
      printRowInt(1, "hi temp: ", highSetTemp);
      delay(1000);
    }

    // check low temp now
    if (subscription == &setlowtemp) {
      debugSerial("got LOW temp pulled from subscription: ");
      debugSerial((char *)setlowtemp.lastread);
      debugSerial("   SET low temp ");
      lowSetTemp = atoi((char *)setlowtemp.lastread);
      debugSerialln(lowSetTemp);
      printRowInt(1, "lo temp: ",  lowSetTemp);
      delay(1000);
    }


  }
  return true;
}

//############ Update TO Adafruit ##############
bool updateAdafruit(void *) {
  debugSerialln("updating Adafruit");

  printRow(1, "Updating Adafruit");
  MQTT_connect();

  debugSerialln("going to update TO adafruit");
  uint32_t unsignedRelayState = digitalRead(RELAYPIN);

  // Now we can publish stuff!
  debugSerial("Sending temp to adafruit val ");
  debugSerial(tempF);
  ("...");
  if (! tempPub.publish(tempF)) {
    debugSerialln(F("Failed"));
  } else {
    debugSerialln(F("OK!"));
  }

  // Now we can publish stuff!
  debugSerial("Sending humidity val ");
  debugSerial(humidity);
  debugSerial("...");
  if (! humidityPub.publish(humidity)) {
    debugSerialln(F("Failed"));
  } else {
    debugSerialln(F("OK!"));
  }

  // Now we can publish stuff!
  debugSerial("Sending relay state val ... ");
  if (! lightPub.publish(unsignedRelayState)) {
    debugSerialln(F("Failed"));
  } else {
    debugSerialln(F("OK!"));
  }

  //thermostatStatusPub
  // Now we can publish stuff!
  debugSerial("Sending current thermostat status val ... ");
  uint32_t unsignedStatus = currentStatus;
  if (! thermostatStatusPub.publish(unsignedStatus)) {
    debugSerialln(F("Failed"));
  } else {
    debugSerialln(F("OK!"));
  }

}

//############ Update TO Blynk  ##############
bool updateBlynk(void *) {
  if (!updateToBlynk) {
    debugSerialln("not updating blynk");
    return false;
  }
  printRow(1, "Updating Blynk");
  if (Blynk.connected()) {
    Blynk.run();
  } else {
    Blynk.connect();  // Try to reconnect to the server
    debugSerialln("###had to reconnect to Blynk");
  }
  debugSerialln("Updating Blynk");
  Blynk.virtualWrite(V5, tempF);
  //uptime minutes
  int uptimeMinutes = (millis() - startMillis) / 60000;
  debugSerial("uptimeMinutes: ");
  debugSerialln(uptimeMinutes);
  Blynk.virtualWrite(V2, uptimeMinutes);
  Blynk.virtualWrite(V7, humidity);
  relayState = digitalRead(RELAYPIN);
  Blynk.virtualWrite(V6, relayState);

  return true; //repeat? true
}


//############ Check temperature thresholds  ##############
void checkThresholds() {

  //get current relay state
  relayState = digitalRead(RELAYPIN);

  //check current state against current temp
  if (tempF < lowSetTemp) {
    if (currentStatus != statusHigh) {
      //under lowSetTemp but not full on
      currentStatus = statusHigh;
      incrementIntermittentSettings(true);//bring pecentage up of how much is runs up when intermittnet
    }
    digitalWrite(RELAYPIN, HIGH); //turn on relay

  } else if (tempF > highSetTemp) {
    if (currentStatus != statusOff) {
      //over highSetTemp but not full off
      currentStatus = statusOff;
      incrementIntermittentSettings(false);//bring pecentage down of how much is runs up when intermittnet
    }
    digitalWrite(RELAYPIN, LOW); //turn off relay

  } else {
    //we are between high and low

    //let's check if we are first starting
    if (currentStatus == statusInitializing) {
      currentStatus = statusLow;
      //when first starting and in low temp setting, have chance of being int-on or int-off
      if (random(1, 3) > 1) {
        //turn on
        printRow(1, "#setting int ON");
        digitalWrite(RELAYPIN, HIGH); //turn on relay
        cycleLastChange = millis();
        delay(2000);
      } else {
        //turn off
        printRow(1, "#setting int OFF");
        digitalWrite(RELAYPIN, LOW); //turn off relay
        cycleLastChange = millis();
        delay(2000);
      }

    } else  if (currentStatus != statusLow) {
      //we just jumped into intermittent cycling
      //leave relay as it was
      currentStatus = statusLow;
      cycleLastChange = millis(); //reset our counter

    } else {
      //we were already in mid-cycle, let's check status duration
      if (relayState == 1) {
        if ((millis() - cycleLastChange) > cycleUpTime) {
          printRow(1, "#setting int OFF");
          digitalWrite(RELAYPIN, LOW); //turn off relay
          cycleLastChange = millis();
          delay(2000);
        } else {
          digitalWrite(RELAYPIN, HIGH);
        }

      } else {
        //relayState == 0  or weirdness
        if ((millis() - cycleLastChange) > cycleDownTime) {
          printRow(1, "#setting int ON");
          digitalWrite(RELAYPIN, HIGH); //turn on relay
          cycleLastChange = millis();
          delay(2000);
        } else {
          digitalWrite(RELAYPIN, LOW);
        }

      }
    }
  }//end of if/else for temps
}

//############ Update status on LCD  ##############
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

  if (currentStatus == statusHigh) {
    strcpy(currentState, " U");
  } else if (currentStatus == statusOff) {
    strcpy(currentState, " D");
  } else if (currentStatus == statusLow) {
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

  printRowInt(1, "low temp % up:", lowTempCyclePercentUp );
}

//############ Read the Temperature  ##############
bool readTemp(void *) {
  debugSerialln("reading temp");

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
      debugSerialln("########################");
      debugSerialln("we got an anomalous reading");
      debugSerial("last temp reading : ");
      debugSerial(tempF);
      debugSerial("   currentTempReading in F :");
      debugSerial(currentTempF);
      debugSerial("   current reading in C :");
      debugSerial(tempC);
      debugSerial("   anomaly count: ");
      debugSerialln(tempAnomalyCount);
      debugSerialln("########################");
      tempAnomalyCount += 1;

      return true;
    } else {
      tempAnomalyCount = 0;
    }

    temps[tempsArrayIndicator] = currentTempF;
    humidities[tempsArrayIndicator++] = humidity.relative_humidity;
    debugSerial("AHT20 Temperature: ");
    debugSerial(temp.temperature);
    debugSerial(" degrees C   "    );
    debugSerial("Humidity: ");
    debugSerial(humidity.relative_humidity);
    debugSerialln("% rH");
  } else {
    printRow(1, "Sensor Error");
  }

  debugSerial("array indicator: ");
  debugSerialln(tempsArrayIndicator);
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

  debugSerialln("temp: " + String(currentTempF, 2));

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
    debugSerialln("new Temp: " + String(tempF, 2));
    float roundedHumidity = roundFloat(humidityTotal / tempsToRound);
    humidity = roundedHumidity;
    debugSerialln("new Humdidity: " + String(humidity, 1));
    checkThresholds();//see if we need to change relay states
    updateLCDstatus();
    tempsArrayIndicator = 0;
  }
  return true;
}

//############ Increment the percentage on the low heat setting  ##############
void incrementIntermittentSettings(bool increaseUp) {

  if (increaseUp) {
    lowTempCyclePercentUp += lowTempPercentageChange;
  } else {
    lowTempCyclePercentUp -= lowTempPercentageChange;
  }

  cycleUpTime = (lowTempCycleDuration * lowTempCyclePercentUp) / 100;
  cycleDownTime = lowTempCycleDuration - cycleUpTime;

  //lowTempPercentageSub
  // Now we can publish stuff!
  debugSerial(F("Sending lowTempPercentageSub to adafruit val "));
  debugSerial(lowTempCyclePercentUp);
  uint32_t unsignedLowTempPercentage = lowTempCyclePercentUp;
  if (! lowTempPercentagePub.publish(unsignedLowTempPercentage)) {
    debugSerialln(F("Failed"));
  } else {
    debugSerialln(F("OK!"));
  }


}

//############ Round a float to 2 digits  ##############
float roundFloat(float x) {
  x = x + 0.005;
  x = x * 100;
  int y = (int)x;
  float z = (float)y / 100;
  return z;
}

//############ Printrow  ##############
void printRow(int rowNum, char toPrint[]) {
  lcd.setCursor(0, rowNum);
  lcd.print("                   ");
  lcd.setCursor(0, rowNum);
  lcd.print(toPrint);
}

//############ Printrow with Integer  ##############
void printRowInt(int rowNum, char printFirst[], int toPrint) {
  lcd.setCursor(0, rowNum);
  lcd.print("                   ");
  lcd.setCursor(0, rowNum);
  lcd.print(printFirst);
  lcd.print(toPrint, DEC);
}


//############ MQTT_connect  ##############
void MQTT_connect() {
  int8_t ret;

  // Stop if already connected.
  if (mqtt.connected()) {
    return;
  }

  debugSerial("Connecting to MQTT... ");

  uint8_t retries = 3;
  while ((ret = mqtt.connect()) != 0) { // connect will return 0 for connected
    debugSerialln(mqtt.connectErrorString(ret));
    debugSerialln("Retrying MQTT connection in 5 seconds...");
    mqtt.disconnect();
    delay(5000);  // wait 5 seconds
    retries--;
    if (retries == 0) {
      // basically die and wait for WDT to reset me
      while (1);
    }
  }
  debugSerialln("MQTT Connected!");
}
