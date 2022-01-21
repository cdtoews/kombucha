

#include <dht11.h>
#include <LiquidCrystal.h>
#include <WiFiNINA.h>

dht11 DHT11;
#define DHT11PIN 7
#define RELAYPIN 8

/*
 * DHT11 has a 1 degree celsius precision
 * so, about 1.8 degrees F precision
 * 
 * 71.6
 * 73.3
 * 75.2
 * 77.0
 * 78.8
 * 80.6
 * 
 * 
 */

int tempF;
int highTemp= 78; 
int lowTemp= 75; 

long loopTime = 15000;

long cycleDuration = 1800000; //amount of time to cyle on&off when between high and low temps//600000 10 minutes, 3600000 1 hour
int cyclePercentUp = 45; //it's gone over a day at 45% so far. that's a good starting point
long cycleUpTime; //this will be calculated
long cycleDownTime;//this will be calculated
//int cycleStatus = 0;
long cycleLastChange = millis();

long startMillis = millis();
long lastChangeMillis = millis();
int relayState; //read from actual pin:RELAYPIN, 0=off 1=on
int currentStatus=-2; //what the status is 0=off, 1=on, -1=middle cycle (intermittent on and off)


String statusString;
String tempHistString;
String tempSettingsString;
int lastExtremeTemp;

// initialize the library by associating any needed LCD interface pin
// with the arduino pin number it is connected to
const int rs = 10, en = 9, d4 = 5, d5 = 4, d6 = 3, d7 = 2;
LiquidCrystal lcd(rs, en, d4, d5, d6, d7);

void setup() {
  Serial.begin(9600);
    while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB port only
  }
  Serial.println("starting setup");
  pinMode(RELAYPIN, OUTPUT);     //Set relay pin as output 
  // set up the LCD's number of columns and rows:
  lcd.begin(16, 2);

  //some setup of variables
  tempSettingsString = "Hi:" + String(highTemp) + char(223) + " Lo:" + String(lowTemp) + char(223);
  cycleUpTime = (cycleDuration * cyclePercentUp) / 100;
  cycleDownTime = cycleDuration - cycleUpTime;
  Serial.println("doing setup");
  getTemp();
}



void loop() {
  
  Serial.println("starting loop again");
  tempF = getTemp();
 
  //get current relay state
  relayState = digitalRead(RELAYPIN);

  //check current state against current temp
  if(tempF < lowTemp){
    if(currentStatus != 1){
      //under lowTemp but not full on
      changeStatus(currentStatus);
      currentStatus = 1;
      incrementIntermittentSettings(1);//bring pecentage up of how much is runs up when intermittnet
      lastExtremeTemp = tempF;
    }
    digitalWrite(RELAYPIN, HIGH); //turn on relay
    if(tempF < lastExtremeTemp){
      lastExtremeTemp = tempF;
    }
  }else if(tempF > highTemp){
    if(currentStatus != 0){
      //over highTemp but not full off
      changeStatus(currentStatus);
      currentStatus = 0;
      incrementIntermittentSettings(-1);//bring pecentage down of how much is runs up when intermittnet
      lastExtremeTemp = tempF;
    }
    digitalWrite(RELAYPIN, LOW); //turn off relay
    if(tempF > lastExtremeTemp){
      lastExtremeTemp = tempF;
    }
  }else{
    //we are between high and low
    if(currentStatus != -1){
      //we just jumped into intermittent cycling
      //leave relay as it was
      changeStatus(currentStatus);
      currentStatus = -1;
      cycleLastChange = millis(); //reset our counter
//      if(relayState == 1){
//        //if it was on, keep it on to push a littler further into middle
//        printRow(1,"#init int ON");
//        delay(2000);
//        //digitalWrite(RELAYPIN, HIGH); 
//        
//      }else{
//        //if it was off, keep it off to push further into our perfect zone (not a calzone, our temp zone)
//        printRow(1,"#init int OFF");
//        delay(2000);
//        //digitalWrite(RELAYPIN, LOW); 
//        
//      }
    }else{
      //we were already in mid-cycle, let's check status duration
      if(relayState == 1){
        if((millis() - cycleLastChange) > cycleUpTime){
          printRow(1,"#setting int OFF");
          delay(2000);
          digitalWrite(RELAYPIN, LOW); //turn off relay
  
          cycleLastChange = millis();
        }else{
          digitalWrite(RELAYPIN, HIGH); 
        }
        
      }else{
        //relayState == 0  or weirdness
        if((millis() - cycleLastChange) > cycleDownTime){
          printRow(1,"#setting int ON");
          delay(2000);
          digitalWrite(RELAYPIN, HIGH); //turn on relay
  
          cycleLastChange = millis();
        }else{
          digitalWrite(RELAYPIN, LOW); 
        }
        
      }
    }
  }//end of if/else for temps





 
  lcd.setCursor(0, 0);
  lcd.print("                 ");
  lcd.setCursor(0,0);
    
  lcd.print(String(tempF));
  lcd.print(char(223));
  lcd.print(" ");

  int humidity = int((float)DHT11.humidity + 0.5);
  lcd.print(humidity, DEC);
  lcd.print("%");



  long sinceLastChange = millis() - lastChangeMillis;
  int minSinceChange = (int) (sinceLastChange / 1000 / 60);
  String currentState;
  if(currentStatus == 1){
    currentState = "U";
  }else if(currentStatus == 0){
    currentState = "D";
  }else if(currentStatus == -1){
    relayState = digitalRead(RELAYPIN);
    if(relayState == 1){
      currentState = "~U";
    }else{
      currentState = "~D";
    }
    
  }else{
    currentState = "?";
  }
  lcd.print(" " + currentState + " " + minSinceChange + "M");

  


   long millisStartedOutput = millis();
   loopStatus();
   long loopTimeLeft = loopTime - (millis() - millisStartedOutput);
   if(loopTimeLeft < 0){
    loopTimeLeft = 0;
   }
   delay(loopTimeLeft);
}

void loopStatus(){
  long titleDelay = 2000;
  long statusDelay = 5000;

  printRow(1,"Cycle Times:");
  delay(titleDelay);
  printRow(1,statusString);
  delay(statusDelay);

  printRow(1,"Temp Extremes");
  delay(titleDelay);
  printRow(1,tempHistString);
  delay(statusDelay);

  printRow(1,"Temp Settings");
  delay(titleDelay);
  printRow(1,tempSettingsString);
  delay(statusDelay);

  if(currentStatus == -1){
    //we are currently cycling
    printRow(1,"Between hi/lo");
    delay(titleDelay);
    relayState = digitalRead(RELAYPIN);
    if(relayState ==1){
      printRow(1,"~ on, %up:" + String(cyclePercentUp));
    }else{
      printRow(1,"~ off, %up:"  + String(cyclePercentUp));
    }
    delay(titleDelay);
  }else{
    printRow(1,"---- %up:"   + String(cyclePercentUp));
  }
  
}


void changeStatus(int lastStatus){
  //see how long at last status
  long sinceLastChange = millis() - lastChangeMillis;
  int minSinceChange = (int) (sinceLastChange / 1000 / 60);
  lastChangeMillis = millis();
  String currentStateString;
  if(lastStatus == 1){
    currentStateString = "U";
    
  }else if( lastStatus == 0){
    currentStateString = "D";
    
  }else if(lastStatus == -1){
    currentStateString = "~";
  }else if(lastStatus == -2){
    currentStateString = "startup";
  }else{
    currentStateString = "?";
  }
  tempHistString = String(lastExtremeTemp) + "|" + tempHistString;
  tempHistString = tempHistString.substring(0,16);
  String thisUpdate = currentStateString +  minSinceChange + "|";
  statusString = thisUpdate + statusString;
  statusString = statusString.substring(0,16);

}


int getTemp(){
  Serial.println("reading temp");
  printRow(1,"reading temp");
  delay(2000);
  
  
  int chk = DHT11.read(DHT11PIN);
  float tempC = DHT11.temperature;
  float tempTemp = float((tempC *1.8) +32);
  int temp1 = int(tempTemp + 0.5);

  printRow(1, ".  " + String(temp1));
  delay(5000);

  
  chk = DHT11.read(DHT11PIN);
  tempC = DHT11.temperature;
  tempTemp = float((tempC *1.8) +32);
  int temp2 = int(tempTemp + 0.5);
  printRow(1, ".. " + String(temp1) + " " + String(temp2));
  delay(5000);

  chk = DHT11.read(DHT11PIN);
  tempC = DHT11.temperature;
  tempTemp = float((tempC *1.8) +32);
  int temp3 = int(tempTemp + 0.5);
  printRow(1, "..." + String(temp1) + " " + String(temp2) + " " + String(temp3));
  delay(2000);



  
  if(temp1 == temp2 && temp2 == temp3){
    return temp1;
  }else{
    return getTemp();
  }
  
}

void incrementIntermittentSettings(int increaseUp){

  cyclePercentUp += increaseUp;
  cycleUpTime = (cycleDuration * cyclePercentUp) / 100;
  cycleDownTime = cycleDuration - cycleUpTime;
}


void printRow(int rowNum, String toPrint){
  lcd.setCursor(0,rowNum);
  lcd.print("                   ");
  lcd.setCursor(0,rowNum);
  lcd.print(toPrint);
}
