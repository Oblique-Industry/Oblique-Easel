// Oblique Palette 0.4.5
// The Palette speaks with the Easel patch in plugdata.
// It announces its presence with its name, version number, and hardware configuration.
// The Easel sends back output values whenever it gets input values from the Palette
// The Palette sends back input values, (later, preceded by the measured time between communications, to the Easel)
// ( The Palette adjusts its sample rate according to the round trip time)
// ( The Easel adjusts it sample rate according to the round trip time)

// The  Palette opens the connection by sending its configuration to the Easel patch
// It then listens for a response. If it doesn't get one after a short timeout, it blinks the onboard LED and tries again.
// If it gets a response, it reads the data formatted as:
// 0|1|2|...((number of outputs)*(number of bits)), eg (8 output channels)*(12 bits)=96bits=12bytes
// The Palette then splits those into a buffer of 12 bit values to be sent to the DACs:
// 0|1|2|...(number of outputs), eg 8
// (If necessary, later it measures the microseconds to do that, then
// ... sends those values to the DACs distributed through time)

// Once it's done receiving, it then sends its input values to the Easel patch.
// It formats
// (0| # of microseconds since last received (so Easel and Palette know the effective sample rate))
// 1|2|...(number of channels)| value for each channel

#include <Arduino.h>

/**********************************
Hardware configuration of the Palette
***********************************/
const char modelName[] = "Palette";                                                              // Model name
const char firmwareVersion[] = "v0.4.5";                                                         // Version number to report
const float DACBitDepth = 12;                                                                    // Output/DAC resolution
const int numOutputChannels = 8;                                                                 // Number of output channels on the Palette
const float ADCBitDepth = 12;                                                                    // Input/ADC resolution
const int numInputChannels = 8;                                                                  // Number of input channels on the Palette
                                                                                                 //
const int numBytesFromEaselPerPacket = (DACBitDepth / 8 /*Bits per byte*/) * numOutputChannels;  // To spread output values over multiple bytes
const int numBytesToEaselPerPacket = (ADCBitDepth / 8 /*Bits per byte*/) * numInputChannels;     // To spread input values over multiple bytes
const int numSamplesFromEaselPerPacket = (numOutputChannels)*1;                                  // Increase this in accord with the Easel if we can receive faster than Control Rate
const int numSamplesToEaselPerPacket = (numInputChannels * ADCBitDepth) * 1;                     // Increase this in accord with the Easel if we can send faster than Control Rate

/********************************************************************
Serial buffer and the values to parse between Easel and Palette & back
********************************************************************/
byte mashedSampleFromEasel[numBytesFromEaselPerPacket];  // The buffer of combined byte values received from the Easel
int DACValues[numOutputChannels];                        // Buffer of DAC values to send to the Easel
                                                         //
int ADCValues[numInputChannels];                         // Buffer of samples for each channel
byte mashedSampleToEasel[numBytesToEaselPerPacket];      // The combined byte values to send to the Easel

/********************************************************************
Analytics for measuring performance live
********************************************************************/
int lastTime = micros();                 // The last time a packet was sent back out so we know how much time has passed and how to space out the incoming values.
int errorBlinkInterval = 50;             // How quick to blink the onboard LED during an error
int communicationTimeoutStart;           // The base from which to count time for timeout
int communicationTimeoutInterval = 100;  // How long in milliseconds to wait to see if the Easel picks up the phone

/********************************************************************
Functions, defined at the bottom (declared here for compatibility with other IDEs)
********************************************************************/
bool lookForEasel();                                                         // Announce configuration of the Palette when there's no connection so the Easel can find it.
void ThreeBytesTwoValues(byte byte1, byte byte2, byte byte3);                // Convert 3 bytes into two 12-bit values
int twoValues[2];                                                            // The two values it outputs
void twoValuesThreeBytes(u_int16_t mashingValue1, u_int16_t mashingValue2);  // Convert two 12-bit values into 3 bytes
byte threeBytes[3];                                                          // The three bytes it outputs
void errorTimeout();                                                         // What to do on communication error timeout

/********************************************************************
Main code
********************************************************************/

void setup() {
  for (int blinkTimes = 0; !(blinkTimes > 20); blinkTimes++) {
    digitalWrite(25, HIGH);
    delay(100);
    digitalWrite(25, LOW);
    delay(100);
  }
  Serial.begin(12000000);  // Baud setting is ignored, as it's handled by USB peripherals & OS
  lastTime = micros();     // Startup time stamp
  // while (!lookForEasel()) {
  //   digitalWrite(25, HIGH);
  //   delay (50);
  //   digitalWrite(25, LOW);
  //   delay (50);
  // }
}

void loop() {
  {
    if (lookForEasel()) {  // When there's incoming data (and if there's not, ping the Easel with configuration data)
      /*******************************
       Output from Easel, sent to DACs 
      ********************************/
      Serial.readBytes(mashedSampleFromEasel, numBytesFromEaselPerPacket);

      // Loop through the buffer 2 values at a time by combining 3 bytes into pairs of entries to incomingParsedSample[]
      for (int whichValue = 0; whichValue < numSamplesFromEaselPerPacket; whichValue += 2) {
        // Go through each trine of bytes to parse them into values and put them into incomingParsedSample[]
        for (byte whichByte = 0; whichByte < numBytesFromEaselPerPacket; whichByte += 3) {
          ThreeBytesTwoValues(mashedSampleFromEasel[whichByte], mashedSampleFromEasel[whichByte + 1], mashedSampleFromEasel[whichByte + 2]);
          for (byte whichChannel = 0; whichChannel < numOutputChannels; whichChannel += 2) {
            for (byte whichValue = 0; whichValue < 2; whichValue++) {
              DACValues[whichValue + whichChannel] = (twoValues[whichValue]);
            }
          }
        }
        // Send DACValues[] to DAC (2nd core?)

        /*******************************
         Input from ADCs, sent to Easel
        ********************************/
        /* Put all ADC values into ADCValues[] */
        for (byte whichADCValue = 0; whichADCValue < numInputChannels; whichADCValue++) {

          // TEST SIGNAL lists time since last loop in .0001 second precision (up to .4095 seconds)
          // if (micros() - round((micros() - lastTime) * 100) < 4096) {
          //   ADCValues[whichADCValue] = round((micros() - lastTime) * 100);
          // } else {
          //   ADCValues[whichADCValue] = 4095;
          // }

          // TEST SIGNAL repeat back the DAC values
          ADCValues[whichADCValue] = DACValues[whichADCValue];
        }
        // Break ADC values into trines of bytes and put them into mashedSampleToEasel
        for (byte thisADCByte = 0; thisADCByte < numBytesToEaselPerPacket; thisADCByte += 3) {
          // Break the 12-bit values into byte-sized pieces in the mashedSampleToEasel array
          for (byte thisADCValue = 0; thisADCValue < numInputChannels; thisADCValue += 2) {
            twoValuesThreeBytes(thisADCValue, thisADCValue + 1);
            for (int thisMashedByte = 0; thisMashedByte < 3; thisMashedByte += 3) {
              mashedSampleToEasel[thisMashedByte + thisADCByte] = threeBytes[thisMashedByte];
            }
          }
          // Send the mashedSampleToEasel array to the Easel
          for (byte thisOutputByte = 0; thisOutputByte < numBytesToEaselPerPacket; thisOutputByte++) {
            Serial.write(mashedSampleToEasel[thisOutputByte]);
          }
        }

        // Serial.println(micros() - lastTime); // time it took to receive the data and store it for use
        lastTime = micros();  // So we know how long it took to communicate in both directions
      }
    }
    // Send incomingParsedSample to all DACs. Do it here while serial communication is being pondered & responded by the Easel.
  }
}



/******************************************************************************************************
 Function definitions
*******************************************************************************************************/

// Call out for the Easel and blink the onboard LED if it's not found yet
bool lookForEasel() {
  // When the Palette hasn't heard from the Easel recently, ping out with identifying information
  if (Serial.available()) {
    digitalWrite(25, HIGH);
    communicationTimeoutStart = millis();
    return (1);

  } else {
    // if (millis() - communicationTimeoutStart > communicationTimeoutInterval) {  // Only send spec if the Palette hasn't heard from the Easel in TimeoutInterval (0.1) seconds.

    //   Serial.print(modelName);
    //   Serial.print(" ");
    //   Serial.print(firmwareVersion);
    //   Serial.print(" ");
    //   Serial.print(numOutputChannels);
    //   Serial.print(" ");
    //   Serial.print(DACBitDepth);
    //   Serial.print(" ");
    //   Serial.print(numInputChannels);
    //   Serial.print(" ");
    //   Serial.println(ADCBitDepth);

    //   digitalWrite(25, LOW);

    //   // If there's an error receiving from the Easel, the values will be out of sync. Clear them all.
    //   for (int outputValueToClear = 0; outputValueToClear < numBytesFromEaselPerPacket; outputValueToClear++) {
    //     mashedSampleFromEasel[outputValueToClear] = 0;
    //   }
    // }

    digitalWrite (25, HIGH);
    delay(100);
    digitalWrite (25, HIGH);
    delay(200);
  }
  return (0);
}

// Convert 3 bytes to 2 12-bit values
void ThreeBytesTwoValues(byte byte1, byte byte2, byte byte3) {
  twoValues[0] = (byte1 << 4) + (byte2 & 0b11110000 >> 4);  // Move the highest byte left 4, add the highest 4 bits of byte 2
  twoValues[1] = (byte2 & 0b00001111) + byte3;              // Add the lowest 4 bits of byte 2 to byte 3
}

// Convert 2 12-bit values to 3 bytes
void twoValuesThreeBytes(u_int16_t mashingValue1, u_int16_t mashingValue2) {
  threeBytes[0] = mashingValue1 >> 4;                                         // Chop off the last 4 bits of the 12-bit value
  threeBytes[1] = (mashingValue1 & 0b0000000000001111) + mashingValue2 >> 8;  // Add the last 4 bits of value 1 to the first 4 bits of value 2
  threeBytes[2] = mashingValue2 & 0b000011111111;                             // Chop off the first four bits of value 2
}
