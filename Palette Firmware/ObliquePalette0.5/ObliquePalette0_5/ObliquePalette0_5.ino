// Oblique Palette
// Communication of values in ASCII between the Easel and the Palette.
// The Palette (this firmware) speaks with the Easel abstraction patch in plugdata, running on a separate GP computer.
// It announces its presence with its name, version number, and hardware configuration.
// The Easel responds with output values whenever it gets DAC values from the Palette.
// The Palette responds back ADC values, (later, followed by the measured time between communications, to the Easel)
// The Palette updates all DACs
// ( The Palette adjusts its sample rate according to the round trip time)
// ( The Easel adjusts it sample rate according to the round trip time)

// #include <Adafruit_TinyUSB.h>// High efficiency USB serial communication
#include <Arduino.h>           // Implicit in the Arduino IDE
#include <Mux.h>               // Multiplexer for analog inputs
#include <Servo.h>             // For servo-range PWM controls
#include <Wire.h>              // i2c communication for DACs
#include <Adafruit_MCP4725.h>  // i2c DACs
#include <pico/stdlib.h>       // To access Pico-specific libraries
#include <hardware/adc.h>      // Direct ADC read access for high speed reads
#include <hardware/gpio.h>     // Direct GPIO access


using namespace admux;  // for ADC multiplexer


/**********************************
Hardware configuration of the Palette
***********************************/
const char modelName[] = "Palette";      // Model name
const char firmwareVersion[] = "0.5.3";  // Version number of this firmware
const int DACBitDepth = 12;              // Output/DAC resolution
const int numOutputChannels = 8;         // Number of output channels on the Palette
const int ADCBitDepth = 12;              // Input/ADC resolution
const int numInputChannels = 8;          // Number of input channels on the Palette
char specsheet[32];                      // Buffer to carry the specsheet string to the Easel

const int numSamplesPerChannel = 1;  // How many samples to pack for transmission (for increasing sample rate later)

const int maxDACValue = (pow(2, DACBitDepth) - 1);  // The maximum DAC value
const int maxADCValue = (pow(2, ADCBitDepth) - 1);  // The maximum ADC value

const int numValuesFromEaselPerPacket = numOutputChannels * numSamplesPerChannel;
const int numValuesToEaselPerPacket = numInputChannels * numSamplesPerChannel;

const byte numi2cBusses = 2;  // Using i2c busses 0 and 1

/*********************************
Analytics
*********************************/
long lastTime;                         // For holding the time to measure the amount of time processing takes
const int communicationTimeout = 100;  // How long to wait to hear back
long communicationTimeoutStart;        // If communication goes down, start a timer
bool ledState = 0;
int testLEDBrightness = 0;

/*********************************
ADCs and DACs
*********************************/
#define ADCPin 28                                                     // The µC ADC pin
Mux ADCMux(Pin(ADCPin, INPUT, PinType::Analog), Pinset(21, 20, 17));  // For addressing ADC channels with a CD4051 multiplexer

Adafruit_MCP4725 DAC[numOutputChannels];                                                        // All DACs
const byte DACAddress[numOutputChannels] = { 0x62, 0x63, 0x60, 0x61, 0x62, 0x63, 0x60, 0x61 };  // Their respective (duplicate) i2c addresses (note order)
const byte DACi2cBus[numOutputChannels] = { 0, 0, 0, 0, 1, 1, 1, 1 };                           // Which bus each DAC is on respectively
                                                                                                //
byte servoPin[numOutputChannels] = { 2, 3, 4, 5, 6, 7, 8, 9 };                                  // Servo pins mirror the output of their associated DAC channel
Servo servoChannel[numOutputChannels];                                                          // All output servos

/********************************************************************
Serial buffer and the values to parse between Easel and Palette & back
********************************************************************/
int DACSamplesFromEasel[numValuesFromEaselPerPacket] = { 511, 1023, 1535, 2047, 2559, 3071, 3583, 4095 };  // The buffer of values received from the Easel
int ADCSamplesToEasel[numValuesToEaselPerPacket];                                                          // The values to send to the Easel



/****************************************************************************************************************************************
Main code
****************************************************************************************************************************************/

void setup() {

  analogReadResolution(ADCBitDepth);  // Use the full bit depth of the ADCs

  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(ADCPin, INPUT);

  digitalWrite(LED_BUILTIN, !ledState);  // Switch whenever something happens

  Serial.begin(12000000);                // Baud setting is ignored, as it's handled by USB peripherals & Easel/OS
  digitalWrite(LED_BUILTIN, !ledState);  // Switch whenever something happens

  Wire.setSDA(0);  // Data line for DACs 0-3
  Wire.setSCL(1);  // Serial clock line for DACs 0-3
  Wire.begin();

  Wire1.setSDA(10);  // Data line for DACs 4-7
  Wire1.setSCL(11);  // Serial clock line for DACs 4-7
  Wire1.begin();

  // begin all DACs on both i2c busses
  for (byte thisDAC = 0; thisDAC < numOutputChannels; thisDAC++) {  // DEBUG LINE Remember to start thisDAC back at 0.

    DAC[thisDAC].begin(DACAddress[thisDAC], &Wire1);  // begin each DAC on i2c bus 1 DEBUG LINE

    if (DACi2cBus[thisDAC] == 0) {
      DAC[thisDAC].begin(DACAddress[thisDAC]);          // begin each DAC on i2c bus 0
    } else {                                            //
      DAC[thisDAC].begin(DACAddress[thisDAC], &Wire1);  // begin each DAC on i2c bus 1
    }
  }
  heartBlink();

  for (byte servoToSetup = 0; servoToSetup < numOutputChannels; servoToSetup++) {  // Make a servo object for each DAC channel
    servoChannel[servoToSetup].attach(servoPin[servoToSetup]);                     // Attach each servo to its pin
  }
  digitalWrite(LED_BUILTIN, HIGH);  // Switch whenever something happens

  while (!findEasel())  // Look for the Easel on boot until the Palette gets back real values
    ;
}





/******************************************** LOOP ********************************************/
/**********************************************************************************************/
void loop() {
  lastTime = micros() - lastTime;  // Time for the whole loop. Use for diagnostics if you need to know how long the loop takes.
                                   // while (findEasel()) {            //  Only enter the loop if there's an Easel to talk with.

  // Receive Serial values into array
  receiveDACs();

  // Read ADC values into array
  readAllADCs();
  // Send ADC values
  sendADCToEasel();

  // Update DAC values
  updateAllDACs();
  // Update all servo DAC values
  updateAllServos();

  heartBlink();
}
/**********************************************************************************************/
/**********************************************************************************************/




/******************************************************************************************
Functions
******************************************************************************************/

/*************************** DAC/output functions ***************************/

// Receive all DAC values from the Easel
void receiveDACs() {
  for (byte thisDACChannel = 0; thisDACChannel < numOutputChannels; thisDACChannel++) {
    DACSamplesFromEasel[thisDACChannel] = Serial.parseInt();
  }
}

// Scroll through all DACs to update them with data from the Easel
void updateAllDACs() {
  for (byte thisi2cDACChannel = 0; thisi2cDACChannel < numOutputChannels; thisi2cDACChannel++) {  // Write to each DAC
    DAC[thisi2cDACChannel].setVoltage(DACSamplesFromEasel[thisi2cDACChannel], false, 400000);
  }
}

// Map analog values to servo outputs
void updateAllServos() {
  for (byte thisServoToUpdate = 0; thisServoToUpdate < numOutputChannels; thisServoToUpdate++) {
    servoChannel[thisServoToUpdate].writeMicroseconds(map(DACSamplesFromEasel[thisServoToUpdate], 0, maxDACValue, 2000, 1000));  // Map the DAC value to the servos within their range
  }
}


/*************************** ADC/input functions ***************************/
void readAllADCs() {
  for (byte thisADCChannel = 0; thisADCChannel < numInputChannels; thisADCChannel++) {
    ADCSamplesToEasel[thisADCChannel] = ADCMux.read(thisADCChannel);  // use regular ol' analogRead() max sample rate: ~ 177Hz

    // ADCSamplesToEasel[thisADCChannel] = (thisADCChannel * 512) - 1; // Test signal bypassing analogRead()

    // if (!adc_fifo_is_empty()) { // Look at the ADC register, and if it's got anything in it, read it
    //   ADCSamplesToEasel[thisADCChannel] = adc_fifo_get(); //
    // }
  }
}

void sendADCToEasel() {
  for (byte thisADCChannel = 0; thisADCChannel < numInputChannels; thisADCChannel++) {
    Serial.print(ADCSamplesToEasel[thisADCChannel]);
    Serial.write(32);  // Space delineated values
  }
  Serial.println();  // Newline/Carriage return to end the transmission
}

// Insert this f() before sending to the Easel to just repeat back input channels so we can make sure the Easel is sending/Palette is receiving.
void testRepeater() {
  for (int thisChannelToRepeat = 1; thisChannelToRepeat < numInputChannels; thisChannelToRepeat++) {
    ADCSamplesToEasel[thisChannelToRepeat] = DACSamplesFromEasel[thisChannelToRepeat];
  }
  ADCSamplesToEasel[0] = lastTime / 100;
}


// Call out for the Easel and blink the onboard LED if it's not found yet
bool findEasel() {
  if (Serial.available()) {                                   //If the Palette is receiving data from the Easel, start talking!
    analogWrite(LED_BUILTIN, int((testLEDBrightness + 0.1)) % 255);  // Rising sawtooth LED indicator
    communicationTimeoutStart = micros();
    return (1);

  } else {  // When the Palette hasn't heard from the Easel yet, ping out with identifying information
    sprintf(specsheet, "%s %s %u %u %u %u", modelName, firmwareVersion, numOutputChannels, DACBitDepth, numInputChannels, ADCBitDepth);
    Serial.println(specsheet);  // Send specsheet with Newline/Carriage Return to end the transmission

    // Error code/waiting blinky for not hearing back from the Easel is ._ repeating
    digitalWrite(LED_BUILTIN, LOW);
    delay(100);
    digitalWrite(LED_BUILTIN, HIGH);
    delay(100);
    digitalWrite(LED_BUILTIN, LOW);
    delay(100);
    digitalWrite(LED_BUILTIN, HIGH);
    delay(200);

    return (0);
  }
}

void heartBlink() {
  analogWrite(LED_BUILTIN, (int(testLEDBrightness + 0.01)) % 255);  // Sawtooth increments whenever something happens
}