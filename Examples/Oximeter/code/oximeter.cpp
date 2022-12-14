#include <WiFiNINA.h>
#include <ConnectIoT.h>
#include "secrets.h"
#include "MAX30105.h"
#include "spo2_algorithm.h"
#include <U8g2lib.h>

char ssid[] = SECRET_SSID;
char pass[] = SECRET_PASS;
char serverAddress[] = SERVER_ADDR;
char nearPrivateKey[] = NEAR_PRIVATE_KEY;
char nearAccountId[] = NEAR_ACCOUNT_ID;
char registryName[] = NEW_REGISTRY;
char deviceName[] = ADD_DEVICE;
uint16_t port = SERVER_PORT;

WiFiClient wifi;
ConnectIoT contract = ConnectIoT(wifi, serverAddress, port, nearAccountId, nearPrivateKey);
int status = WL_IDLE_STATUS;

#define REPORTING_PERIOD_MS      1000
#define MAX_BRIGHTNESS 255

U8G2_SSD1306_128X32_UNIVISION_F_HW_I2C display(U8G2_R0);
MAX30105 particleSensor;

#if defined(__AVR_ATmega328P__) || defined(__AVR_ATmega168__)
//Arduino Uno doesn't have enough SRAM to store 100 samples of IR led data and red led data in 32-bit format
//To solve this problem, 16-bit MSB of the sampled data will be truncated. Samples become 16-bit data.
uint16_t irBuffer[100]; //infrared LED sensor data
uint16_t redBuffer[100];  //red LED sensor data
#else
uint32_t irBuffer[100]; //infrared LED sensor data
uint32_t redBuffer[100];  //red LED sensor data
#endif

const byte RATE_SIZE = 4; //Increase this for more averaging. 4 is good.
byte rates[RATE_SIZE]; //Array of heart rates
byte rateSpot = 0;
long lastBeat = 0; //Time at which the last beat occurred
bool initialized=false;
int32_t bufferLength; //data length
int32_t spo2; //SPO2 value
int8_t validSPO2; //indicator to show if the SPO2 calculation is valid
int32_t heartRate; //heart rate value
int8_t validHeartRate; //indicator to show if the heart rate calculation is valid
int beatAvg;

void get_data()
{
  bufferLength = 100; //buffer length of 100 stores 4 seconds of samples running at 25sps

  //read the first 100 samples, and determine the signal range
  for (byte i = 0 ; i < bufferLength ; i++)
  {
    while (particleSensor.available() == false) //do we have new data?
      particleSensor.check(); //Check the sensor for new data

    redBuffer[i] = particleSensor.getRed();
    irBuffer[i] = particleSensor.getIR();
    particleSensor.nextSample(); //We're finished with this sample so move to next sample

    Serial.print(F("red="));
    Serial.print(redBuffer[i], DEC);
    Serial.print(F(", ir="));
    Serial.println(irBuffer[i], DEC);
  }

  //calculate heart rate and SpO2 after first 100 samples (first 4 seconds of samples)
  maxim_heart_rate_and_oxygen_saturation(irBuffer, bufferLength, redBuffer, &spo2, &validSPO2, &heartRate, &validHeartRate);

  //Continuously taking samples from MAX30102.  Heart rate and SpO2 are calculated every 1 second
  while (1)
  {
    //dumping the first 75 sets of samples in the memory and shift the last 25 sets of samples to the top
    for (byte i = 75; i < 100; i++)
    {
      redBuffer[i - 75] = redBuffer[i];
      irBuffer[i - 75] = irBuffer[i];
    }

    //take 75 sets of samples before calculating the heart rate.
    for (byte i = 25; i < 100; i++)
    {
      while (particleSensor.available() == false) //do we have new data?
        particleSensor.check(); //Check the sensor for new data

      redBuffer[i] = particleSensor.getRed();
      irBuffer[i] = particleSensor.getIR();
      particleSensor.nextSample(); //We're finished with this sample so move to next sample

      /*Serial.print(F("red="));
      Serial.print(redBuffer[i], DEC);
      Serial.print(F(", ir="));
      Serial.print(irBuffer[i], DEC);
      Serial.print(F(", HR="));
      Serial.print(heartRate, DEC);
      Serial.print(F(", HRvalid="));
      Serial.print(validHeartRate, DEC);
      Serial.print(F(", SPO2="));
      Serial.print(spo2, DEC);
      Serial.print(F(", SPO2Valid="));
      Serial.println(validSPO2, DEC);*/
      print_data();
    }
    
    //After gathering 75 new samples recalculate HR and SP02
    maxim_heart_rate_and_oxygen_saturation(irBuffer, bufferLength, redBuffer, &spo2, &validSPO2, &heartRate, &validHeartRate);
    print_data();
    if(validHeartRate&&validSPO2 && (heartRate>50 and heartRate<220 and spo2>50 and spo2<100)){
      break;
    }
  }
}

void get_avg_bpm(){
  if (heartRate < 255 && heartRate > 20)
  {
    rates[rateSpot++] = (byte)heartRate; //Store this reading in the array
    rateSpot %= RATE_SIZE; //Wrap variable
   
      //Take average of readings
    beatAvg = 0;
    for (byte x = 0 ; x < RATE_SIZE ; x++)
      beatAvg += rates[x];
    beatAvg /= RATE_SIZE;
  }
}

void initial_display() 
{
  if (not initialized) 
  {
    if (WiFi.status() == WL_NO_MODULE) {
       display.clearBuffer();
      display.setCursor(15,12);
      display.setFont(u8g2_font_crox2hb_tr); 
      display.print("ERROR!"); 
      display.setFont(u8g2_font_crox2h_tr);
      display.setCursor(30,29);
      display.print("Oximeter damaged...");
      display.sendBuffer();
      while (true);
    }

      display.clearBuffer();
      display.setCursor(15,12);
      display.setFont(u8g2_font_crox2hb_tr); 
      display.print("Mexbalia"); 
      display.setFont(u8g2_font_crox2h_tr);
      display.setCursor(30,29);
      display.print("Initializing...");
      display.sendBuffer();
      delay(2000);
    
   
    initialized=true;

      display.clearBuffer();
      display.setFont(u8g2_font_crox2hb_tr); 
        if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
          display.setCursor(40,12);
          display.print("FAILED");
          display.setCursor(15,29);
          display.print("Check Sensor !");
          display.sendBuffer();
          while(1);
        } else {
          display.setCursor(15,12);
          display.print("INITIALIZED");
          display.setCursor(0,29);
          display.print("Push button to START...");
          display.sendBuffer(); 
        }
     delay(2000);
  }
}

void print_data(){
     display.clearBuffer();
     display.setFont(u8g2_font_crox2h_tr);
     display.setCursor(0,12);  
     display.print("HR");
     display.setCursor(75,12);  
     display.print("Bpm");
     display.setCursor(0,30);
     display.print("SpO2 ");
     display.setCursor(75,30);
     display.print("%"); 
     display.setFont(u8g2_font_fub11_tf); 
     display.setCursor(45,12);  
     display.print(heartRate);
     display.setCursor(45,30);  
     display.print(spo2);
     display.setFont(u8g2_font_cursor_tr);
     display.setCursor(118,10);
     display.print("^");
     display.sendBuffer();
}
bool createNewRegistryandaddDevice(){
 if (contract.createRegistry(registryName))
  {
    bool newDevice = contract.addDeviceToRegistry(
        registryName, deviceName, "Device for Arduiuno lib test.");
        return newDevice;
  
} else{ 
    Serial.println("Failed to create the registry");
    
      }
}
bool setDeviceData()
{     DynamicJsonDocument req(1024);
      req["HeartRate"] = heartRate;
      req["SPO2"] = spo2;
      contract.setDeviceData(registryName, deviceName, req);
      
      Serial.println("SPO2: "+contract.getDeviceDataParam(registryName, deviceName,"SPO2"));
      Serial.println("BPM: "+contract.getDeviceDataParam(registryName, deviceName,"HeartRate"));
      req.clear();
  /*
  if (contract.deleteDeviceFromRegistry(registryName, deviceName))
  {
    contract.deleteRegistry(registryName);
    Serial.println("Cleaned up");
  }else{
    Serial.println("Failed to delete the registry");
  }*/
}

void setup()
{
  Serial.begin(115200);
  while (!Serial)
  {
    ;
  }

  if (WiFi.status() == WL_NO_MODULE)
  {
    Serial.println("Communication with WiFi module failed!");
    while (true)
      ;
  }

  String fv = WiFi.firmwareVersion();
  if (fv < WIFI_FIRMWARE_LATEST_VERSION)
  {
    Serial.println("Please upgrade the firmware");
  }

  while (status != WL_CONNECTED)
  {
    Serial.print("Attempting to connect to SSID: ");
    Serial.println(ssid);
    Serial.println(pass);
    Serial.println(WL_CONNECTED);
    status = WiFi.begin(ssid, pass);
    Serial.println(status);
    delay(10000);
  }
  Serial.println("Connected to WiFi");
  createNewRegistryandaddDevice();
  pinMode(2, INPUT);
  display.begin();
  initial_display();
  //particleSensor.begin(Wire, I2C_SPEED_FAST);
  //Setup to sense a nice looking saw tooth on the plotte
  byte ledBrightness = 60; //Options: 0=Off to 255=50mA
  byte sampleAverage = 4; //Options: 1, 2, 4, 8, 16, 32
  byte ledMode = 2; //Options: 1 = Red only, 2 = Red + IR, 3 = Red + IR + Green
  byte sampleRate = 100; //Options: 50, 100, 200, 400, 800, 1000, 1600, 3200
  int pulseWidth = 411; //Options: 69, 118, 215, 411
  int adcRange = 4096; //Options: 2048, 4096, 8192, 16384

  particleSensor.setup(ledBrightness, sampleAverage, ledMode, sampleRate, pulseWidth, adcRange); //Configure sensor with these settings
}

void loop()
{
  if (digitalRead(2) == HIGH) {
      display.clearBuffer();
      display.setFont(u8g2_font_crox2hb_tr); 
      display.setCursor(15,12);
      display.print("Reading...");
      display.setCursor(10,29);
      display.print("Please wait");
      display.sendBuffer();
      get_data();
    
      if (heartRate>50 and heartRate<220 and spo2>50 and spo2<100) {
        display.clearBuffer();
        display.setFont(u8g2_font_crox2hb_tr); 
        display.setCursor(10,12);
        display.print("Uploading data");
        display.setCursor(8,29);
        display.print(" to NEAR bc...");
        display.sendBuffer();
        Serial.println("Sending request...");
        setDeviceData();
        print_data();
      }else{
        display.clearBuffer();
        display.setFont(u8g2_font_crox2hb_tr); 
        display.setCursor(40,12);
        display.print("FAILED");
        display.setCursor(15,29);
        display.print("Try again!");
        display.sendBuffer();
         }
  delay(30000);
}}

