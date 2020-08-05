/*
  Compatible with Wozniak v4 and first release of Wilkes software client
  Features:
    Read analog signals from three ADS1115 ADCs (output voltage from current follower circuit)
    Control gate electrode with MCP4921 DAC
  
  ADS gain settings:
                                                                ADS1015  ADS1115
                                                                -------  -------
  ads.setGain(GAIN_TWOTHIRDS);  // 2/3x gain +/- 6.144V  1 bit = 3mV      0.1875mV (default)
  ads.setGain(GAIN_ONE);        // 1x gain   +/- 4.096V  1 bit = 2mV      0.125mV
  ads.setGain(GAIN_TWO);        // 2x gain   +/- 2.048V  1 bit = 1mV      0.0625mV
  ads.setGain(GAIN_FOUR);       // 4x gain   +/- 1.024V  1 bit = 0.5mV    0.03125mV
  ads.setGain(GAIN_EIGHT);      // 8x gain   +/- 0.512V  1 bit = 0.25mV   0.015625mV
  ads.setGain(GAIN_SIXTEEN);    // 16x gain  +/- 0.256V  1 bit = 0.125mV  0.0078125mV
*/

#include <Arduino.h>          // Arduino library
#include <Wire.h>             // I2C communication protocol
#include <SPI.h>              // SPI communication protocol
#include <Adafruit_ADS1015.h> // ADS1115 header file in src folder

// Create three ADS1115 instances
Adafruit_ADS1115 ads1115_0(0x48); // Address at 0x48 (GND)
Adafruit_ADS1115 ads1115_1(0x49); // Address at 0x49 (5V)
Adafruit_ADS1115 ads1115_2(0x4B); // Address at 0x4B (SPI)

// const float multiplier = 0.1875e-3F; // GAIN_TWOTHIRDS
// const float multiplier = 0.125e-3F; // GAIN_ONE
// const float multiplier = 0.0625e-3F; // GAIN_TWO
const float multiplier = 0.03125e-3F; // GAIN_FOUR
// const float multiplier = 0.015625e-3F; // GAIN_EIGHT
// const float multiplier = 0.0078125e-3F; // GAIN_SIXTEEN

// Global variables for sensing measurements
float iSen[10];                                      // Initialize sensor current output array
const int iSenSize = sizeof(iSen) / sizeof(iSen[0]); // Length of output array
float CNT[2];                                        // Counter electrode recording

// DAC settings
uint16_t stepSize; // Step size for gate sweep

// Global variables for step time tracking
unsigned long tStart;
unsigned long timeCheck;

// Gating parameters
uint16_t indxTopLim = 2048;   // Gate top limit index (positive voltage input)
uint16_t indxBtmLim = 2048;   // Gate bottom limit index (negative voltage input)
uint16_t indxConstPot = 2048; // Constant potential index

// User input for setup
String readerSetting;
int medianUser;
int amplitudeUser;
int frequencyUser;

// Digital LED pin
int led = 13;

// DAC chip select pin assignment
const int chipSelectPin = 10;

//===========================================================================
//============================== Read ADCs ==================================
//===========================================================================
void readADC()
{
  int16_t adc[12];         // Initialize variable to store raw ADC measurements
  float v[12];             // Initialize variable to store voltage conversions
  const float rRef = 50e3; // Constant reference resistor value in current follower circuit
  const float vRef = 2.60; // Reference voltage for level shifter circuit

  // Step through four channels for each ADC
  for (int i = 0; i < 4; i++)
  {
    adc[i] = ads1115_0.readADC_SingleEnded(i);
    adc[i + 4] = ads1115_1.readADC_SingleEnded(i);
    adc[i + 8] = ads1115_2.readADC_SingleEnded(i);
  }

  // Convert ADC signal to voltage
  for (int j = 0; j < 12; j++)
  {
    v[j] = (float)adc[j] * multiplier;
  }

  // Calculate potentiostat current based on output voltage and reference resistor
  iSen[0] = v[0] / rRef * 1.0e6;
  iSen[1] = v[1] / rRef * 1.0e6;
  iSen[2] = v[2] / rRef * 1.0e6;
  iSen[3] = v[3] / rRef * 1.0e6;
  iSen[4] = v[4] / rRef * 1.0e6;
  iSen[5] = v[7] / rRef * 1.0e6;
  iSen[6] = v[8] / rRef * 1.0e6;
  iSen[7] = v[9] / rRef * 1.0e6;
  iSen[8] = v[10] / rRef * 1.0e6;
  iSen[9] = v[11] / rRef * 1.0e6;

  // Calculate gate potential
  CNT[0] = v[5] - vRef;
  CNT[1] = v[6] - vRef;
}

//===========================================================================
//============================== Write DAC ==================================
//===========================================================================
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

//===========================================================================
//================== Read setup info from serial buffer =====================
//===========================================================================
void serialReadSetup()
{
  String dataStr = ""; // Variable that stores entire data transmission
  char dataChar;       // Individual data character read from buffer
  char startMarker = '<';
  char endMarker = '>';
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

//===========================================================================
//============================== DAC setup ==================================
//===========================================================================
void dacSetup()
{
  uint16_t dacRes = 4096;                            // Resolution (minimum step size) of 12 bit DAC
  int vRefDAC = 1148;                                // Voltage reference for DAC
  int maxRange = 2 * vRefDAC;                        // Full range of gate sweep (mV)
  float smallStep = (float)maxRange / (float)dacRes; // Voltage increment based on DAC resolution
  float err = 1.0 * smallStep;                       // Assume error equal to smallStep value

  // Setup for constant non-zero potential setting
  if (readerSetting == "c")
  {
    float testVoltageConst = 0;
    const float constPotRef = (float)medianUser;

    while (testVoltageConst >= (constPotRef + err) || testVoltageConst <= (constPotRef - err))
    {
      if (testVoltageConst >= (constPotRef + err))
      {
        if (indxConstPot == 0)
        {
          break;
        }
        else
        {
          indxConstPot -= 1;
          testVoltageConst -= smallStep;
        }
      }
      else if (testVoltageConst <= (constPotRef - err))
      {
        if (indxConstPot == 4095)
        {
          break;
        }
        else
        {
          indxConstPot += 1;
          testVoltageConst += smallStep;
        }
      }
      else
      {
        break;
      }
    }
    // Serial.print("Constant reference: ");
    // Serial.println(constPotRef);
    // Serial.print("Constant index: ");
    // Serial.println(indxConstPot);
  }

  // Setup for sweep and transfer curve settings
  else if (readerSetting == "s" || readerSetting == "i")
  {
    // Initialize user input values
    const float userVoltageUpper = (float)medianUser + (float)amplitudeUser;
    const float userVoltageLower = (float)medianUser - (float)amplitudeUser;

    // Initialize test variables (start both at ground setting)
    float testVoltageUpper = 0;
    float testVoltageLower = 0;

    while (testVoltageUpper >= (userVoltageUpper + err) || testVoltageUpper <= (userVoltageUpper - err))
    {
      if (testVoltageUpper >= (userVoltageUpper + err))
      {
        indxTopLim -= 1;
        testVoltageUpper -= smallStep;
      }
      else if (testVoltageUpper <= (userVoltageUpper - err))
      {
        if (indxTopLim == 4095)
        {
          break;
        }
        else
        {
          indxTopLim += 1;
          testVoltageUpper += smallStep;
        }
      }
      else
      {
        break;
      }
    }

    while (testVoltageLower >= (userVoltageLower + err) || testVoltageLower <= (userVoltageLower - err))
    {
      if (testVoltageLower >= (userVoltageLower + err))
      {
        if (indxBtmLim == 0)
        {
          break;
        }
        else
        {
          indxBtmLim -= 1;
          testVoltageLower -= smallStep;
        }
      }
      else if (testVoltageLower <= (userVoltageLower - err))
      {
        indxBtmLim += 1;
        testVoltageLower -= smallStep;
      }
      else
      {
        break;
      }
    }

    // First, determine waveform period based on top and btm limit index values and microcontroller execution time
    // Execution time is the time required for the microcontroller to complete one round of ADC read, DAC write, and data transmission
    float indxRange = (float)indxTopLim - (float)indxBtmLim; // Calculate index range; note that topLim and btmLim are unsigned int types
    float cycle = 2 * indxRange;                             // Full cycle iterations with highest resolution
    const float exTime = 130e-3;                             // Execution time (sec) — varies slightly in reality
    float periodDAC = cycle * exTime;                        // Time to complete one DAC cycle (sec)

    // Base DAC step size on the ratio of high-res period to user-defined period
    float periodUser = 1.0 / ((float)frequencyUser / 1000.0);
    float stepSizeFloat = periodDAC / periodUser; // This imposes a bottom limit on user-defined frequency, because periodUser should be smaller than periodDAC
    stepSize = (uint16_t)stepSizeFloat;

    // Because stepSize is likely truncated, add 1 if stepSizeFloat is larger
    if (stepSizeFloat > stepSize)
    {
      stepSize += 1; // Note: if periodUser < periodDAC, then stepSize will equal 1, i.e., the highest resolution possible in the system
    }
    // Serial.print("Top limit index: ");
    // Serial.println(indxTopLim);
    // Serial.print("Bottom limit index: ");
    // Serial.println(indxBtmLim);
    // Serial.print("Step size: ");
    // Serial.println(stepSize);
  }
}

//===========================================================================
//========================== Data Transmission ==============================
//===========================================================================
void serialTransmission()
{
  Serial.print(timeCheck);
  Serial.print(',');
  for (int i = 0; i < (iSenSize); i++)
  {
    // if (i == iSenSize - 1)
    // {
    //   Serial.println(iSen[i], 3);
    // }
    // else
    // {
    //   Serial.print(iSen[i], 3); // Transmit data with three decimal point precision
    //   Serial.print(',');
    // }

    Serial.print(iSen[i], 3); // Transmit data with three decimal point precision
    Serial.print(',');
  }
  Serial.print(CNT[0], 3);
  Serial.print(',');
  Serial.println(CNT[1], 3);
}

//===========================================================================
//================================ Setup ====================================
//===========================================================================
void setup(void)
{
  // pinMode(led, OUTPUT);
  // digitalWrite(led, HIGH);

  // Initialize ADS1115 chips and set amplifier gain
  ads1115_0.begin();
  ads1115_0.setGain(GAIN_FOUR);
  ads1115_1.begin();
  ads1115_1.setGain(GAIN_FOUR);
  ads1115_2.begin();
  ads1115_2.setGain(GAIN_FOUR);

  // Initialize DAC communication
  SPI.begin();
  SPI.setClockDivider(SPI_CLOCK_DIV8);

  // Inform DAC no incoming data — SS: Slave Select
  digitalWrite(chipSelectPin, HIGH); // When LOW, microcontroller sends data
  writeDAC(indxConstPot, chipSelectPin);

  // Initialize serial communication
  Serial.begin(500000); // Set baud rate

  Serial.println("Ready to receive setup commands");
  delay(1000);

  if (Serial.available() > 0)
  //  Serial.println("Receiving setup commands...");
  {
    serialReadSetup(); // Read setup information
    dacSetup();
  }
  else
  {
    Serial.println("Reader setting error");
  }

  //  Serial.println("Microcontroller ready to transmit data");
}

//===========================================================================
//============================== Main Loop ==================================
//===========================================================================
void loop(void)
{
  bool dataCollection = true;

  // Hold counter electrode at constant potential
  if (readerSetting == "c")
  {
    writeDAC(indxConstPot, chipSelectPin);
    tStart = millis();
    while (dataCollection)
    {
      timeCheck = millis() - tStart;
      readADC();
      serialTransmission();
    }
  }

  // Sweep counter electrode
  else if (readerSetting == "s")
  {
    tStart = millis();
    while (dataCollection)
    {
      for (uint16_t i = indxBtmLim; i <= indxTopLim; i += stepSize)
      {
        timeCheck = millis() - tStart;
        writeDAC(i, chipSelectPin);
        readADC();
        serialTransmission();
      }
      for (uint16_t i = indxTopLim; i >= indxBtmLim; i -= stepSize)
      {
        timeCheck = millis() - tStart;
        writeDAC(i, chipSelectPin);
        readADC();
        serialTransmission();
      }
    }
  }

  // Sweep counter electrode across one cycle for sensor characterization
  if (readerSetting == "i")
  {
    tStart = millis();
    for (uint16_t i = indxBtmLim; i <= indxTopLim; i += stepSize)
    {
      timeCheck = millis() - tStart;
      writeDAC(i, chipSelectPin);
      readADC();
      serialTransmission();
    }
    for (uint16_t i = indxTopLim; i >= indxBtmLim; i -= stepSize)
    {
      timeCheck = millis() - tStart;
      writeDAC(i, chipSelectPin);
      readADC();
      serialTransmission();
    }
    while (true) // Do nothing after completing one cycle
      ;
  }
}
