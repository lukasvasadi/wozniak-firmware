/*
  Firmware for Wozniak v5 series
  ADS gain settings (5V logic):
                                                                ADS1015  ADS1115
                                                                -------  -------
  ads.setGain(GAIN_TWOTHIRDS);  // 2/3x gain +/- 6.144V  1 bit = 3mV      0.1875mV (default)
  ads.setGain(GAIN_ONE);        // 1x gain   +/- 4.096V  1 bit = 2mV      0.125mV
  ads.setGain(GAIN_TWO);        // 2x gain   +/- 2.048V  1 bit = 1mV      0.0625mV
  ads.setGain(GAIN_FOUR);       // 4x gain   +/- 1.024V  1 bit = 0.5mV    0.03125mV
  ads.setGain(GAIN_EIGHT);      // 8x gain   +/- 0.512V  1 bit = 0.25mV   0.015625mV
  ads.setGain(GAIN_SIXTEEN);    // 16x gain  +/- 0.256V  1 bit = 0.125mV  0.0078125mV
*/

#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_ADS1015.h>

Adafruit_ADS1115 ads1115(0x48); // Instantiate ADS1115

// const float multiplier = 0.1875e-3F;    // GAIN_TWOTHIRDS
// const float multiplier = 0.125e-3F;     // GAIN_ONE
const float multiplier = 0.0625e-3F; // GAIN_TWO
// const float multiplier = 0.03125e-3F;   // GAIN_FOUR
// const float multiplier = 0.015625e-3F;  // GAIN_EIGHT
// const float multiplier = 0.0078125e-3F; // GAIN_SIXTEEN

// Initialize values for signal acquisition
const float rRef = 22e3; // Reference resistor in current follower
int16_t adc;             // Readout from ADC channel
float v;                 // Converted voltage value
float iSen;              // Sensor current measurement

unsigned long timeStart, timeExperiment; // Time tracking variables

// User input for setup
String readerSetting;
int medianUser;
int amplitudeUser;
int frequencyUser;

// DAC and gating parameters
uint16_t dacRes = 4096;      // Resolution (minimum step size) of 12 bit DAC
uint16_t indexGround = 2048; // Ground potential index
uint16_t indexMedian;        // Constant potential index
uint16_t indexTopLim;        // Gate top limit index (positive voltage input)
uint16_t indexBtmLim;        // Gate bottom limit index (negative voltage input)
uint16_t indexDAC;           // Index value for DAC output
uint16_t stepSize;           // Step size for gate sweep

// Variables for computing DAC index along waveform
float periodUser;
float interval;
int phase1;
int phase2;
int phase3;
int phase4;

const int chipSelectPin = 10; // DAC chip select pin

float readADC()
{
  adc = ads1115.readADC_SingleEnded(0); // Read ADC Channel 0
  v = (float)adc * multiplier;          // Calculate voltage using multiplier
  return v / rRef * 1.0e6;              // Convert signal to current based on output voltage and reference resistor
}

void writeDAC(uint16_t data, uint8_t chipSelectPin)
{
  // Take top 4 bits of config and top 4 valid bits (data is actually a 12 bit number) and OR them together
  uint8_t config = 0x30;
  uint8_t topMsg = (config & 0xF0) | (0x0F & (data >> 8));

  uint8_t lowerMsg = (data & 0x00FF); // Take the bottom octet of data

  digitalWrite(chipSelectPin, LOW); // Select DAC, active LOW

  SPI.transfer(topMsg);   // Send first 8 bits
  SPI.transfer(lowerMsg); // Send second 8 bits

  digitalWrite(chipSelectPin, HIGH); // Deselect DAC
}

void setupDAC()
{
  float vRefDAC = 1890.0;                     // Determine value of vRef for the DAC
  float maxRange = 2.0 * vRefDAC;             // Full range of gate sweep (mV)
  float smallStep = maxRange / (float)dacRes; // Voltage increment based on DAC resolution

  // Serial.print("vRefDAC: ");
  // Serial.println(vRefDAC);
  // Serial.print("smallStep: ");
  // Serial.println(smallStep);

  // indexMedian must be determined for both constant and sweep states
  indexMedian = indexGround + (int)((float)medianUser / smallStep); // Even though smallStep is a float, value becomes an int

  // Serial.print("Index median: ");
  // Serial.println(indexMedian);

  // Setup for sweep and transfer curve settings
  if (readerSetting == "s")
  {
    indexTopLim = indexMedian + (int)((float)amplitudeUser / smallStep);
    indexBtmLim = indexMedian - (int)((float)amplitudeUser / smallStep);

    phase1 = indexTopLim - indexMedian;
    phase2 = indexMedian - indexTopLim;
    phase3 = indexBtmLim - indexMedian;
    phase4 = indexMedian - indexBtmLim;

    periodUser = 1.0e6 / (float)frequencyUser; // Period in milliseconds

    // Serial.print("Index top limit: ");
    // Serial.println(indexTopLim);
    // Serial.print("Index bottom limit: ");
    // Serial.println(indexBtmLim);

    // Serial.print("Phase1: ");
    // Serial.println(phase1);
    // Serial.print("Phase2: ");
    // Serial.println(phase2);
    // Serial.print("Phase3: ");
    // Serial.println(phase3);
    // Serial.print("Phase4: ");
    // Serial.println(phase4);
    // Serial.print("Period: ");
    // Serial.println(periodUser, 3);
  }
}

uint16_t sweepIndex(unsigned long timeExperiment)
{
  interval = fmod(timeExperiment, periodUser) / periodUser; // Find point in waveform
  // Serial.print("Interval: ");
  // Serial.println(interval, 3);

  // Map interval to corresponding index
  if (interval <= 0.25)
  {
    indexDAC = (uint16_t)round((float)indexMedian + (interval / 0.25 * phase1));
  }
  else if ((interval > 0.25) && (interval <= 0.5))
  {
    indexDAC = (uint16_t)round((float)indexMedian + ((interval - 0.5) / 0.25 * phase2));
  }
  else if ((interval > 0.5) && (interval <= 0.75))
  {
    indexDAC = (uint16_t)round((float)indexMedian + ((interval - 0.5) / 0.25 * phase3));
  }
  else
  {
    indexDAC = (uint16_t)round((float)indexMedian + ((interval - 1.0) / 0.25 * phase4));
  }

  // Serial.print("DAC index: ");
  // Serial.println(indexDAC);

  return indexDAC;
}

void serialReadSetup()
{
  String dataStr = "";    // Variable that stores entire data transmission
  char dataChar;          // Individual data character read from buffer
  char startMarker = '<'; // Indicates beginning of message
  char endMarker = '>';   // Indicates end of message
  static boolean receiveInProgress = false;

  // While data in serial buffer...
  while (Serial.available() > 0)
  {
    // Read individual character from stream, in sequence
    dataChar = Serial.read();

    if (receiveInProgress == true)
    {
      if (dataChar != endMarker)
      {
        dataStr += dataChar;
      }
      else
      {
        receiveInProgress = false;
      }
    }
    else if (dataChar == startMarker)
    {
      receiveInProgress = true;
    }
  }

  // Extract data from transmission
  int firstDelim = dataStr.indexOf(';');
  readerSetting = dataStr.substring(0, firstDelim);

  int secondDelim = dataStr.indexOf(';', firstDelim + 1);
  String medianInput = dataStr.substring(firstDelim + 1, secondDelim);
  medianUser = medianInput.toInt();

  int thirdDelim = dataStr.indexOf(';', secondDelim + 1);
  String amplitudeInput = dataStr.substring(secondDelim + 1, thirdDelim);
  amplitudeUser = amplitudeInput.toInt();

  String frequencyInput = dataStr.substring(thirdDelim + 1, -1);
  frequencyUser = frequencyInput.toInt();

  // Serial.print("Setting: ");
  // Serial.println(readerSetting);
  // Serial.print("Median: ");
  // Serial.println(medianUser);
  // Serial.print("Amplitude: ");
  // Serial.println(amplitudeUser);
  // Serial.print("Frequency: ");
  // Serial.println(frequencyUser);
}

void serialTransmission(unsigned long timeExperiment, float iSen)
{
  Serial.print(timeExperiment);
  Serial.print(',');
  Serial.println(iSen, 3);
}

void setup()
{
  // Initialize ADS1115 and set amplifier gain
  ads1115.begin();
  ads1115.setGain(GAIN_TWO);

  // Initialize SPI communication (DAC)
  SPI.begin();
  SPI.setClockDivider(SPI_CLOCK_DIV8);

  pinMode(chipSelectPin, OUTPUT);       // Set SPI CS pin as output
  digitalWrite(chipSelectPin, HIGH);    // Initialize CS pin in default state
  writeDAC(indexGround, chipSelectPin); // Immediately set to ground potential

  Serial.begin(500000); // Set baud rate for serial communication

  while (Serial.available() < 1)
  {
    ; // Delay until incoming message from user
  }

  serialReadSetup();
  setupDAC();
}

void loop()
{
  // Option 1: hold counter electrode at steady potential
  if (readerSetting == "c")
  {
    writeDAC(indexMedian, chipSelectPin);
    timeStart = millis();
    while (true)
    {
      timeExperiment = millis() - timeStart;
      iSen = readADC();
      serialTransmission(timeExperiment, iSen);
    }
  }

  // Option 2: sweep counter electrode
  else if (readerSetting == "s")
  {
    timeStart = millis();
    while (true)
    {
      timeExperiment = millis() - timeStart;
      indexDAC = sweepIndex(timeExperiment);
      writeDAC(indexDAC, chipSelectPin);
      iSen = readADC();
      serialTransmission(timeExperiment, iSen);
    }
  }
}