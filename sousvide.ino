// Sous vide immersion cooker controller
// @author Stefan Wehner

#include <LiquidCrystal.h>
#include <OneWire.h>
#include <EEPROM.h>

#define MIN_TEMP 1000
#define MAX_TEMP 9000
#define BTN_UP_PIN 6
#define BTN_DOWN_PIN 7
#define RELAY_PIN 8
#define TEMP_PIN 10

#define SLEEP_INTERVAL 20

// all temperatures in centigrade
#define temp_t int

// hysteresis values for under/over target
#define HYST_LOW 10
#define HYST_HIGH 0
// notify temp has been reached
#define HAS_REACHED_DIFF 50
// notify target temp has been lost
#define HAS_LOST_DIFF 300
#define TEMP_STEP 50
#define BTN_INTERVAL 100

temp_t target=1000;
temp_t temp;
unsigned long btn_last_time = 0;
unsigned long now = 0;

byte power = 0;
unsigned long start_time = 0;
// has reached target temp flag 
byte has_reached = 0;
//set up lcd
LiquidCrystal lcd(12,11,2,3,4,5);

void do_buttons(){
  temp_t oldtarget = target;
  
  if (digitalRead(BTN_UP_PIN) && target < MAX_TEMP){
    target += TEMP_STEP;
  }
  if (digitalRead(BTN_DOWN_PIN) && target > MIN_TEMP){
    target -= TEMP_STEP;
  }

  // safety for targets out of range
  if (target > MAX_TEMP || target < MIN_TEMP){
    target = MIN_TEMP;
  }
  
  // store target in eeprom
  if (target != oldtarget){
    byte *v = (byte*)&target;
    EEPROM.write(0, v[0]);
    EEPROM.write(1, v[1]);
  }
}

void printTemp(temp_t num){
  int before=num/100;
  int after=(num%100)/10;
  if (before < 10) {
    lcd.print(" ");
  }
  lcd.print(before);
  lcd.print(".");
  lcd.print(after);
}

void printTime() {
  unsigned long now = millis();
  unsigned long seconds = (now - start_time) / 1000;
  byte sprint = seconds % 60;
  unsigned long minutes = (seconds) / 60;
  byte mprint = minutes % 60;
  byte hours = minutes / 60;
  lcd.print(hours);
  lcd.print(":");
  if (mprint < 10) {
    lcd.print("0");
  }
  lcd.print(mprint);
  lcd.print(":");
  if (sprint < 10) {
    lcd.print("0");
  }
  lcd.print(sprint);
}

void display(){
  // print temp
  lcd.setCursor(0,0);
  lcd.print("T: ");
  printTemp(temp);
  
  //print timer
  lcd.setCursor(9, 0);
  printTime();
  
  // second line
  lcd.setCursor(0,1);

  // print target emp
  lcd.print("-> ");
  printTemp(target);

  // print power state
  lcd.setCursor(9,1);
  if (power == 1) {
    lcd.print("On ");
  } else {
    lcd.print("Off");
  }

  // print has reached flag
  lcd.setCursor(13,1);
  if (has_reached) {
    lcd.print("   ");
  } else {
    lcd.print("Pre");
  }
}

class TempReader{
private:
  // DS18S20 Temperature chip i/o
  OneWire ds;
  byte addr[8];
  unsigned long schedule_read;
  int temp;

public:
  TempReader(byte pin):ds(pin), schedule_read(0){
    get_addr();
  }
  
  void get_addr(){
    if (!ds.search(addr)){
      Serial.print("No more addresses");
      ds.reset_search();
    }
    int i;
    Serial.print("R=");
    for( i = 0; i < 8; i++) {
      Serial.print(addr[i], HEX);
      Serial.print(" ");
    } 
    if ( OneWire::crc8( addr, 7) != addr[7]) {
      Serial.print("CRC is not valid!\n");
      return;
    }

    if ( addr[0] == 0x10) {
      Serial.print("Device is a DS18S20 family device.\n");
    } else if ( addr[0] == 0x28) {
      Serial.print("Device is a DS18B20 family device.\n");
    } else {
      Serial.print("Device family is not recognized: 0x");
      Serial.println(addr[0],HEX);
      return;
    }
  }

  boolean has_temp(){
     if (millis() > schedule_read + 1000){
       temp = get_temp();      
       return true;
     } else {
       return false;
     }
  }

  void scheduleRead(){
    ds.reset();
    ds.select(addr);    

    ds.write(0x44,1);         // start conversion, with parasite power on at the end
    schedule_read = millis();
  }

  int get_temp(){
    byte data[12];
    
    ds.reset();
    ds.select(addr);    
    ds.write(0xBE);         // Read Scratchpad
    int i;
    for ( i = 0; i < 9; i++) {           // we need 9 bytes
      data[i] = ds.read();
    }
    int TReading = (data[1] << 8) + data[0];

    int Tc_100 = TReading*6 + TReading/4;

    return Tc_100;
  }
};

TempReader tr(TEMP_PIN);

void setup(void) {
  // initialize inputs/outputs
  // start serial port
  Serial.begin(9600);
  lcd.begin(16, 2);
  
  // read target from eeprom
  byte v[2];
  v[0] = EEPROM.read(0);
  v[1] = EEPROM.read(1);
  
  target = *((int*)&v);

  // setup pins
  pinMode(BTN_UP_PIN, INPUT);
  pinMode(BTN_DOWN_PIN, INPUT);
  pinMode(RELAY_PIN, OUTPUT);

  start_time = millis();
}

void notify_has_reached() {
  if (!has_reached) {
     start_time = millis();
     has_reached = 1;
  }
}

void notify_not_reached() {
  if (has_reached) {
     start_time = millis();
     has_reached = 0;
  }
}

void loop(){
  now = millis();
  if (tr.has_temp()){
    temp = tr.get_temp();
    Serial.print(millis(), DEC);
    Serial.print(" ");
    Serial.print(temp, DEC);
    Serial.print(" ");
    Serial.print(power, DEC);
    Serial.println();
    tr.scheduleRead();

    // set power
    if (temp <= (target - HYST_LOW)) {
      power = 1;
    }
    if (temp >= (target + HYST_HIGH)) {
      power = 0;
    }
    // check if target has been reached and notify
    temp_t tdiff = abs(temp - target);
    Serial.println(tdiff);
    if (tdiff <= HAS_REACHED_DIFF) {
      notify_has_reached();
    }
    if (tdiff >= HAS_LOST_DIFF) {
      notify_not_reached();
    }
  }

  // safety range
  if (temp < MIN_TEMP|| temp > MAX_TEMP) {
    power = 0;
  }

  // write to relay
  digitalWrite(RELAY_PIN, power);

  if (now >= (btn_last_time + BTN_INTERVAL)){
    do_buttons();
    display();
    btn_last_time = now;
  }
  
  delay(SLEEP_INTERVAL);
}

