// Simple demo to read a few bits from Siemens S7-1200 PLC over Ethernet
// There are a few switches and lights attached to the PLC and mapped to a datablock DB1:
// DB1.DBX0.0 : switch 1
// DB1.DBX0.1 : switch 2
// DB1.DBX0.2 : push button 1
// DB1.DBX0.3 : push button 2
// DB1.DBX0.4 : light 1
// DB1.DBX0.5 : light 2
// DB1.DBX0.6 : light 3
// DB1.DBX0.7 : light 4
//
// The logic reads this databock and updates an LED pixel strip based on the configuration of the switches
// The pixel strip is WS2812B, 150 pixels long, and it is conencted to GPIO53 (J1 pin 35) 
// Note: Watch out what pins are conencted to which power domain 
//   For example, VDD_IO_5 is connected to ESP_LDO_VO4 power rail, which is an output of the internal LDO (VDDO_4) , and it is configured to 1.3V
//   So we can't use GPIOs powered by VDD_IO_5 for this LED strip, becsue we need 3.3V for it


// ETH library has an internal varibale named ETH (ETHClass type) that we are using to configure Ethernet peripheral
#include <ETH.h>

// S7 library. There are a few ifdefines there to select the right TCP client (for us it is NetworkClient), but I just hardcoded it in the libaray 
#include "Settimino.h"

#include <Adafruit_NeoPixel.h>


// Constants and defines
// Defines for the Ethernet PHY
#define ETH_PHY_TYPE  ETH_PHY_TLK110
#define ETH_PHY_ADDR  1
#define ETH_PHY_MDC   31
#define ETH_PHY_MDIO  52
#define ETH_PHY_POWER 51
#define ETH_CLK_MODE  EMAC_CLK_EXT_IN

IPAddress s7_plc_ip(192, 168, 12, 35);  // IP address of the PLC


// Local variables
S7Client s7_plc_client; // Siemens S7 client variable
Adafruit_NeoPixel strip = Adafruit_NeoPixel(150, 53, NEO_GRB + NEO_KHZ800); // RGB strip is connected to GPIO53, and it is 150 pixels long


// Ethernet callback to handle events
// WARNING: onEventEthernet is called from a separate FreeRTOS task (thread)!
void onEventEthernet(arduino_event_id_t event) 
{
  switch (event) 
  {
    case ARDUINO_EVENT_ETH_START:
      Serial.println("Ethernet Started");

      // The hostname must be set after the interface is started, but needs
      // to be set before DHCP, so set it from the event handler thread.
      ETH.setHostname("esp32p4-ethernet");
      break;
    
    case ARDUINO_EVENT_ETH_CONNECTED:
      Serial.println("Ethernet Connected");
      break;
    
    case ARDUINO_EVENT_ETH_GOT_IP:
      Serial.print("Ethernet Got IP Adress: ");
      Serial.println(ETH.localIP());
      break;
    
    case ARDUINO_EVENT_ETH_LOST_IP:
      Serial.println("Ethernet Lost IP");
      break;
    
    case ARDUINO_EVENT_ETH_DISCONNECTED:
      Serial.println("Ethernet Disconnected");
      break;
    
    case ARDUINO_EVENT_ETH_STOP:
      Serial.println("Ethernet Stopped");
      break;
    
    default:
      Serial.print("Ethernet Unknown Event: ");
      Serial.println(event);
      break;
  }
}


// Setup
void setup() 
{
  Serial.begin(115200);
  delay(1000);
  Serial.println("Setup begin");

  // Configure Ethernet
  Network.onEvent(onEventEthernet);
  ETH.begin(ETH_PHY_TYPE, ETH_PHY_ADDR, ETH_PHY_MDC, ETH_PHY_MDIO, ETH_PHY_POWER, ETH_CLK_MODE);

  // Initialize pixels
  strip.begin();

  // The whimpy power supply can't handle a full power, limit brightness a bit
  strip.setBrightness(127); // 0 = off; 255=max brightness
  colorSet(0, 0, 0);

  Serial.println("Setup end");
}


// Loop
void loop() 
{
  // Confirm that etherenet is connected
  if (!ETH.connected() || !ETH.hasIP())
  {
    Serial.println("Ethernet is not connected");
    colorSet(100, 0, 0);
    delay(10000);
    return;
  }

  if (!s7_plc_client.Connected)
  {
    // Try to conenct to the PLC
    Serial.print("Connecting to: ");
    Serial.println(s7_plc_ip);

    // Parameters: IP, Rack number, Slot number
    int res = s7_plc_client.ConnectTo(s7_plc_ip, 0, 1);
    if (res==0) 
    {
      Serial.print("Connected. PDU Length: ");
      Serial.println(s7_plc_client.GetPDULength());
    }
    else
    {
      Serial.print("Connect Failure: ");
      Serial.println(res);
      delay(10000);
      return;
    }
  }

  Serial.println("Reading from PLC"); 
  // DB number, start byte, length in bytes, pointer to the destination to read data to, if NULL then it uses an internal buffer
  int res = s7_plc_client.ReadArea(S7AreaDB, 1, 0, 1, NULL);
  if (res == 0)
  {
    Serial.print("Read Ok. Data: ");
    // Use S7 helper functions to read from the internal buffer
    Serial.println(S7.ByteAt(0), HEX);
    if (S7.BitAt(0, 4))
    {
      // Button pressed
      colorWipe(255, 0, 0, 20);
    }
    else if (!S7.BitAt(0, 6) && !S7.BitAt(0, 7))
    {
      // Switches: 00
      colorWipe(0, 0, 0, 20);
    }
    else if (S7.BitAt(0, 6) && !S7.BitAt(0, 7))
    {
      // Switches: 01
      colorWipe(0, 0, 255, 20);
    }
    else
    {
      // Turn the strip off by default
      colorWipe(0, 0, 0, 20);
    }
  }
  else
  {
    Serial.print("Read Failure: ");
    Serial.println(res);
    delay(10000);
  }

  // Short delay
  delay(1000);
}


// Set all LEDs to the same color
void colorSet(uint8_t r, uint8_t g, uint8_t b) 
{
  for(uint16_t i = 0; i < strip.numPixels(); i++) 
  {
      strip.setPixelColor(i, r, g, b);
  }
  strip.show();
}


// Fill the dots one after the other with a color
void colorWipe(uint8_t r, uint8_t g, uint8_t b, uint8_t wait) 
{
  for(uint16_t i = 0; i < strip.numPixels(); i++) 
  {
      strip.setPixelColor(i, r, g, b);
      strip.show();
      delay(wait);
  }
}
