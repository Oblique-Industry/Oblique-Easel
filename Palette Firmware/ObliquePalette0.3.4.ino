/************************************************
Firmware for the Oblique Palette IO board. It has an Input stage and an Output stage.
It is built from the easily-accessible Arduino Pro Micro and four MCP4822 Digital/Analog Converters (DACs)

It communicates with an Oblique Easel, which can be any computer running the plugdata graphical programing language
with Oblique Easel objects using a serial connection via USB.

The Easel Input object in plugdata represents and passes on values from the Palette.

The Easel Output object in plugdata accepts values that your plugdata patch generates according to
your creative whims, then passes them through a serial connection to the Palette, coded here.

**** The Palette ****
The Palette's Input stage uses the Arduino's built-in Analog/Digital Converters to read values that can come from
any outside source, including built-in potentiometers and switches, as well as analog voltage control
like that from analog synthesizers or other experimental sources such as analog sensors.

The Palette's Output stage uses four MCP4822 Digital/Analog converters to convert the values that the Easel has sent
to analog values in 4096 steps of precision.
/************************************************/

// Library for the MCP4822 (and others of the family) 2-channel 12-bit SPI DAC
#include <MCP48xx.h>

// Define the hardware inputs using an Arduino Pro Micro's onboard 12-bit ADCs
const int numberOfInputPins = 8;                                             // Built-in Analog input pins
const int ADCBitDepth = 10;                                                  // Arduino Pro Micro is 10 bit, with 1024 values from 0:1023
int analogInputValue[numberOfInputPins];                                     // A list of all the current input values
int analogInputPin[numberOfInputPins] = { A0, A1, A2, A3, A6, A7, A8, A9 };  // A list of all used Arduino analog input pins

// Digital/Analog Converters
const int numberOfDACChannelsPerChip = 2;                                 // Each 4822 has 2 channels, A and B
const int numberOfDACChips = 4;                                           // The Palette has 4 chips for a total of 8 channels
#define numberOfDACChannels numberOfDACChannelsPerChip* numberOfDACChips  // rather than calculating this over and over
const int DACBitDepth = 12;                                               // The 4822 is 12-bit, with 4096 levels from 0:4095

const int serialBufferSize = (numberOfDACChannels * (4 + 1)) + 2;  // A buffer big enough for each of the values, plus the value delimiter, plus the newline delimiter and its delimiter
char serialBuffer[serialBufferSize];

// Error and debugging checks
const bool errorFeedback = 0;  //set to 1 to send info back to the Palette for analysis
int errorAlert;
int debugValue;


// Assign 4822 chips to spots in an array so we can access them by index.
MCP4822 DACChip[] = { MCP4822(2), MCP4822(3), MCP4822(5), MCP4822(7) };  //Each 4822 DAC, listed with the Arduino Chip Select pin to identify it
                                                                         // Additionally, all chips need to connect SCK (Serial Clock) to Arduino pin 15 
                                                                         // and SDI (Serial Data In) to Arduino pin 16.

int analogOutputValue[numberOfDACChannels];  // An array of each of the output values

void setup() {
  Serial.begin(115200);  // Communicate quickly enough with the Easel so no values get perceptably missed

  // Set up each of the DAC chips so we can communicate with it
  for (int thisChip = 0; thisChip < numberOfDACChips; thisChip++) {
    // Initialize the instance of each DAC chip
    DACChip[thisChip].init();
    // DACChip[0].init();

    // The channels are turned off at startup so we need to turn on the channel we need
    DACChip[thisChip].turnOnChannelA();
    DACChip[thisChip].turnOnChannelB();

    // We configure the channels in High gain
    // This is the default.
    DACChip[thisChip].setGainA(MCP4822::High);
    DACChip[thisChip].setGainB(MCP4822::High);
  }

  DACChip[0].setVoltage(4095, 0);  // Initialized values so it's clear that the DACs are working
  DACChip[0].setVoltage(2047, 1);
  DACChip[0].updateDAC();
}

void loop() {

  /******************************************** Send input values to Easel ********************************************/
  // Put each channel in a space-separated list (to debug: don't measure the last one insert it afterward as feedback)
  for (int whichInput = 0; whichInput < numberOfInputPins; whichInput++) {
    analogInputValue[whichInput] = analogRead(analogInputPin[whichInput]);

    // Debug so Channel 8 can be used to feed back debug info to the Easel
    if ((errorFeedback) && (whichInput == numberOfDACChannels - 1)) {
      analogInputValue[7] = debugValue;
    }
    // end debug

    Serial.print(analogInputValue[whichInput]);
    Serial.print(' ');
  }
  // Finish sending this row of input values from Palette to Easel
  Serial.println();

  /**************************** Receive output from Easel to send to each DAC channel ****************************/
  if (Serial.available()) {

    Serial.readStringUntil(13);  // Clear the buffer until we get to the beginning of the list of Output values
    for (int whichValue = 0; whichValue < numberOfDACChannels; whichValue++) {
      analogOutputValue[whichValue] = Serial.parseInt();  // Fill the Output array with each of the Output values
    }
    // Send each value from the array of output values to each channel of each DAC chip.
    for (int thisChip = 0; thisChip < numberOfDACChips; thisChip++) {                       // Choose the DAC chip
      for (int thisChannel = 0; thisChannel < numberOfDACChannelsPerChip; thisChannel++) {  // Choose which DAC channel to write to

        /***** Output debug feedback: set the debugValue to the value of the first output, to be fed back to the last INPUT channel. To turn on change errorFeedback to 1 *****/
        if (errorFeedback && thisChannel == (numberOfDACChannels - 1)) {
          debugValue = (analogOutputValue[(thisChip * 2) + thisChannel]);
        }
        /***** end debug *****/

        DACChip[thisChip].setVoltage(analogOutputValue[(thisChip * 2) + thisChannel], thisChannel);  // Write  the channel value to the channel
      }
      DACChip[thisChip].updateDAC();  // Update the DAC for both channels of this chip.
    }
  } else {  // If there's no serial connection, pulse both channels of chip 0
    if (errorAlert < 4095) {
      errorAlert = errorAlert + 16;
    } else {
      errorAlert = 0;
    }
    DACChip[0].setVoltageA((errorAlert + 1023) % 4095);
    DACChip[0].setVoltageB(errorAlert);
    DACChip[0].updateDAC();
  }
}
