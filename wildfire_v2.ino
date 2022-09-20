#include <Arduino.h>
#include "disk91_LoRaE5.h"
#include "sensirion_common.h"
#include "sgp30.h"
#include <SensirionI2CSht4x.h>
#include "TFT_eSPI.h"

SensirionI2CSht4x sht4x; //SHT sensor

TFT_eSPI tft;           //TFT display
 
Disk91_LoRaE5 lorae5(&Serial); // Where the AT command and debut traces are printed

#define Frequency DSKLORAE5_ZONE_AU915
/*
Select your frequency band here.
DSKLORAE5_ZONE_EU868
DSKLORAE5_ZONE_US915
DSKLORAE5_ZONE_AS923_1
DSKLORAE5_ZONE_AS923_2
DSKLORAE5_ZONE_AS923_3
DSKLORAE5_ZONE_AS923_4
DSKLORAE5_ZONE_KR920
DSKLORAE5_ZONE_IN865
DSKLORAE5_ZONE_AU915
 */

char deveui[] = "70B3D57ED005378C";
char appeui[] = "0000000000001111";
char appkey[] = "EDB9FC220438F1701386542BCEACCFE1";


void data_decord(int val_1, int val_2, int val_3, int val_4, uint8_t data[8])
{ 
  //enconde to bytes TVOC & CO2eq

  data[0] = val_1 >> 8 & 0xFF;
  data[1] = val_1 & 0xFF;
  data[2] = val_2 >> 8 & 0xFF;
  data[3] = val_2 & 0xFF;

  //encode to bytes temp & humidity
  int val[] = {val_3, val_4};

  for(int i = 0, j = 4; i < 2; i++, j += 2)
  {
    if(val[i] < 0)
    {
      val[i] = ~val[i] + 1;  //two's complement
      data[j] = val[i] >> 8 | 0x80;
      data[j+1] = val[i] & 0xFF;
    }
    else
    {
      data[j] = val[i] >> 8 & 0xFF;
      data[j+1] = val[i] & 0xFF;
    }
  }
 
}


 
void setup(void)
{ 
  Serial.begin(9600);

  uint32_t start = millis();
  while ( !Serial && (millis() - start) < 1500 );  // Open the Serial Monitor to get started or wait for 1.5"

  //setup SHT sensor

  Wire.begin();

  uint16_t error;
  char errorMessage[256];

  sht4x.begin(Wire);

  uint32_t serialNumber;
  
  error = sht4x.serialNumber(serialNumber);
  delay(5000);
  if (error) {
      Serial.print("Error trying to execute serialNumber(): ");
      errorToString(error, errorMessage, 256);
      Serial.println(errorMessage);
  } else {
      Serial.print("Serial Number: ");
      Serial.println(serialNumber);
  }

  //setup SGP sensor
  s16 err;
  u32 ah = 0;
  u16 scaled_ethanol_signal, scaled_h2_signal;
  
  while (sgp_probe() != STATUS_OK) {
      Serial.println("SGP failed");
      while (1);
  }
  err = sgp_measure_signals_blocking_read(&scaled_ethanol_signal,
                                          &scaled_h2_signal);
  if (err == STATUS_OK) {
      Serial.println("get ram signal!");
  } else {
      Serial.println("error reading signals");
  }
  sgp_set_absolute_humidity(13000);
  err = sgp_iaq_init();

  // init the library, search the LORAE5 over the different WIO port available
  if ( ! lorae5.begin(DSKLORAE5_SWSERIAL_WIO_P2) ) {
    Serial.println("LoRa E5 Init Failed");
    while(1); 
  }

  // Setup the LoRaWan Credentials
  if ( ! lorae5.setup(
        Frequency,
        deveui,
        appeui,
        appkey
     ) ){
    Serial.println("LoRa E5 Setup Failed");
    while(1);         
  }

   // Display set up
  tft.begin();
  tft.setRotation(3);
  tft.setTextColor(TFT_YELLOW);
  tft.setTextSize(2);
  tft.fillScreen(TFT_BLACK); 
  
  
}
 
void loop(void)
{


 //read SHT sensor

 uint16_t error;
    char errorMessage[256];

    float temperature;
    float humidity;
    int int_temp, int_humi;

    error = sht4x.measureHighPrecision(temperature, humidity);
    if (error) {
        Serial.print("Error trying to execute measureHighPrecision(): ");
        errorToString(error, errorMessage, 256);
        Serial.println(errorMessage);
    } else {
        Serial.print("Temperature:");
        Serial.print(temperature);
        Serial.print("\t");
        Serial.print("Humidity:");
        Serial.println(humidity);
    }

    int_temp = temperature*100;
    int_humi = humidity*100;

  //end reading sensor

  // read SGP sensor

  s16 err = 0;
  u16 tvoc_ppb, co2_eq_ppm;
  err = sgp_measure_iaq_blocking_read(&tvoc_ppb, &co2_eq_ppm);

  Serial.print("tvoc_ppb: "); Serial.println(tvoc_ppb);
  Serial.print("co2_eq_ppm: "); Serial.println(co2_eq_ppm);

  //end reading sensor

  static uint8_t data[8] = { 0x00 };  //Use the data[] to store the values of the sensors
  data_decord(tvoc_ppb, co2_eq_ppm, int_temp, int_humi, data);  //call to enconde data
  

  //Printing to display
  tft.fillScreen(TFT_BLACK);
  tft.drawString("Gases", 5, 20);
  tft.drawString("eCO2: ", 10, 40);
  tft.drawString("VOC: ", 10, 60);
  tft.drawString("Temp & Hum", 5, 120);
  tft.drawString("Temp: ", 10, 140);
  tft.drawString("Hum: ", 10, 160);
 // tft.fillRect(50,40,72,35,TFT_BLACK);
//  tft.fillRect(50,140,72,55,TFT_BLACK);
  tft.drawString(String(co2_eq_ppm), 100, 40);
  tft.drawString(String(tvoc_ppb), 100, 60);
  tft.drawString(String(temperature), 100, 140);
  tft.drawString(String(humidity), 100, 160);
//  tft.drawString(String(z_values), 50, 180);

  //send data to lora
  if ( lorae5.send_sync(              //Sending the sensor values out
        8,                            // LoRaWan Port
        data,                         // data array
        sizeof(data),                 // size of the data
        false,                        // we are not expecting a ack
        7,                            // Spread Factor
        14                            // Tx Power in dBm
       ) 
  ) {
      Serial.println("Uplink done");
      if ( lorae5.isDownlinkReceived() ) {
        Serial.println("A downlink has been received");
        if ( lorae5.isDownlinkPending() ) {
          Serial.println("More downlink are pending");
        }
      }
  }

  

  delay(15000);
}