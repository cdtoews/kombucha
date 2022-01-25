

#include <dht11.h>
#include <LiquidCrystal.h>
#include <arduino-timer.h>
auto timer = timer_create_default(); // create a timer with default settings
long BlynkTimerRepeat = 60000;
long readTempRepeat = 15000;

#include <arduino_secrets.h>
#include<BlynkSimpleWiFiNINA.h>


// You should get Auth Token in the Blynk App.
char auth[] = BLYNK_AUTH_TOKEN;

// Your WiFi credentials.
char ssid[] = WIFI_SSID;
char pass[] = WIFI_PASS;


dht11 DHT11;
#define DHT11PIN 7
#define RELAYPIN 8


float tempF;
const int tempsToRound = 4;
float temps[tempsToRound];
float humidities[tempsToRound];
float humidity;
int tempsArrayIndicator = 0; //keep track of which element in array we are updating, this will be for rounding temps
int highSetTemp = 79;
int lowSetTemp = 77;



long lowTempCycleDuration = 1800000; //amount of time to cyle on&off when between high and low temps//600000 10 minutes, 3600000 1 hour
int lowTempCyclePercentUp = 45; //it's gone over a day at 45% so far. that's a good starting point
long cycleUpTime; //this will be calculated
long cycleDownTime;//this will be calculated

long cycleLastChange = millis();


long startMillis = millis();
long lastChangeMillis = millis();
int relayState; //read from actual pin:RELAYPIN, 0=off 1=on
int currentStatus = -2; //what the status is 0=off, 1=on, -1=low temp cycle (intermittent on and off)


String statusString;

// initialize the library by associating any needed LCD interface pin
// with the arduino pin number it is connected to
const int rs = 12, en = 11, d4 = 5, d5 = 4, d6 = 3, d7 = 2;
LiquidCrystal lcd(rs, en, d4, d5, d6, d7);



void setup() {
  Serial.begin(9600);

  Serial.println("starting setup");
  Blynk.begin(auth, ssid, pass);

  pinMode(RELAYPIN, OUTPUT);     //Set relay pin as output
  // set up the LCD's number of columns and rows:
  lcd.begin(16, 2);

  //some setup of variables
  cycleUpTime = (lowTempCycleDuration * lowTempCyclePercentUp) / 100;
  cycleDownTime = lowTempCycleDuration - cycleUpTime;
  Serial.println("doing setup");
  //give dht11 a few seconds to get its bearings and poll it
  timer.at(millis() + 5000, readTemp);
  timer.at(millis() + 6000, readTemp);
  timer.at(millis() + 7000, readTemp);
  timer.at(millis() + 8000, readTemp);
  timer.at(millis() + 9000, updateBlynk);
  timer.every(readTempRepeat, readTemp);
  delay(1000);
  timer.every(BlynkTimerRepeat, updateBlynk);


}



void loop() {

  timer.tick();





}


bool updateBlynk(void *) {
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
  lcd.setCursor(0, 0);
  lcd.print("                 ");
  lcd.setCursor(0, 0);

  lcd.print(String(tempF, 1));
  lcd.print(char(223));
  lcd.print(" ");

  lcd.print(String(humidity,1));
  lcd.print("%");
  String currentState;
  if (currentStatus == 1) {
    currentState = "U";
  } else if (currentStatus == 0) {
    currentState = "D";
  } else if (currentStatus == -1) {
    relayState = digitalRead(RELAYPIN);
    if (relayState == 1) {
      currentState = "~U";
    } else {
      currentState = "~D";
    }

  } else {
    currentState = "?";
  }
  lcd.print(" " + currentState );

}

bool readTemp(void *) {
  Serial.println("reading temp");

  int chk = DHT11.read(DHT11PIN);
  float tempC = DHT11.temperature;
  float temp1 = roundFloat(float((tempC * 1.8) + 32));
  temps[tempsArrayIndicator] = temp1;
  printRow(1, "temp:" + String(temp1, 1));
  Serial.println("temp: " + String(temp1, 1));
  humidities[tempsArrayIndicator++] = DHT11.humidity;


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
    Serial.println("new Temp: " + String(tempF, 1));
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
  x = x + 0.05;
  x = x * 10;
  int y = (int)x;
  float z = (float)y / 10;
  return z;
}

void printRow(int rowNum, String toPrint) {
  lcd.setCursor(0, rowNum);
  lcd.print("                   ");
  lcd.setCursor(0, rowNum);
  lcd.print(toPrint);
}
