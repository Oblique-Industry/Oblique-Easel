

#include <Wire.h>
#include <Adafruit_MCP4725.h>
#define PCAADDR 0x70
Adafruit_MCP4725 dac;


  void pcaselect(uint8_t i) {
  if (i > 7) return;
 
  Wire.beginTransmission(PCAADDR);
  Wire.write(1 << i);
  Wire.endTransmission();  
}


void setup(void) {
  
  dac.begin(0x61);
pinMode(4, OUTPUT);    // s0      
pinMode(5, OUTPUT);    // s1      
pinMode(6, OUTPUT);    // s2 

 
   
}

void loop(void) {
int readInZero = analogRead(0);
   

//pcaselect(0);
//digitalWrite(4, LOW);      
//digitalWrite(5, LOW);      
//digitalWrite(6, LOW);
//int readOut0 =  map(readInZero,0,1095,0,4095);  
//dac.setVoltage(readOut0, false);

pcaselect(7);
digitalWrite(4, LOW);      
digitalWrite(5, LOW);      
digitalWrite(6, HIGH);  
int readOut1 =  map(readInZero,0,1096,0,4095); 
dac.setVoltage(readOut1, false);

}
