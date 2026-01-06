// Simple demo to read a few bits from a Siemens S7-1200 PLC over Ethernet and control an RGB LED pixel strip based on these bits
// There are 2 switches and 2 buttons attached to the PLC, mapped to data block DB1:
// DB1.DBX0.0 : switch 1 - on/off
// DB1.DBX0.1 : switch 2 - chase effect on/off
// DB1.DBX0.2 : push button 1 - color green
// DB1.DBX0.3 : push button 2 (normally closed) - color red
//
// The logic reads this data block and updates an RGB LED pixel strip based on the switches' configuration.
// The pixel strip uses typical WS2812B addressable LEDs, is 150 pixels long, and is connected to GPIO53 (J1 pin 35 of the ESP32-P4 dev kit).
// Note: Watch which pins are connected to which power domain.
//   For example, VDD_IO_5 is connected to the ESP_LDO_VO4 power rail, which is an output of the internal LDO (VDDO_4), and it is configured to 1.3V.
//   So we can't use GPIOs powered by VDD_IO_5 for this LED strip, because we need 3.3V for it


// ETH library has an internal variable named ETH (ETHClass type) that we are using to configure the Ethernet peripheral
#include <ETH.h>

// S7 library. There are a few ifdefs there to select the right TCP client (for us it is NetworkClient), but I just hardcoded it in the library 
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

// IP address of the PLC
IPAddress s7_plc_ip(192, 168, 12, 35);


// ========================================
// Local variables

// Siemens S7 client variable
S7Client s7_plc_client; 

// RGB LED_strip variable. 
// Parameters: pixel count, data pin, pixel type
//   150 pixels
//   GPIO53 is used as the data output for the WS2812B string on this ESP32-P4 dev kit
//   NEO_GRB + NEO_KHZ800 are the ordering and timing flags for typical WS2812B LEDs
Adafruit_NeoPixel LED_strip = Adafruit_NeoPixel(150, 53, NEO_GRB + NEO_KHZ800); 

// Lights chase tasks to update lights effect in background, so it is not depended on the main logic
TaskHandle_t handle_task_chase = NULL;
uint8_t chase_r, chase_g, chase_b;
bool chase_enable = false;


// ========================================
// Setup
void setup() 
{
  Serial.begin(115200);
  delay(1000);
  Serial.println("Setup begin");

  // Register Ethernet event handler and initialize the MAC/PHY with board-specific pins
  // ETH.begin parameters depend on the Ethernet PHY wiring on the dev board
  Network.onEvent(onEventEthernet);
  ETH.begin(ETH_PHY_TYPE, ETH_PHY_ADDR, ETH_PHY_MDC, ETH_PHY_MDIO, ETH_PHY_POWER, ETH_CLK_MODE);

  // Initialize pixels
  LED_strip.begin();

  // Limit the brightness a bit, to limit the current
  // 0 = off; 255 = max brightness
  LED_strip.setBrightness(127); 
  colorSet(0, 0, 0);

  // Start a task for the light chase effect, so it is independent from the main logic
  xTaskCreate(ChaseTask, "ChaseTask", 2048, NULL, 1, &handle_task_chase);

  Serial.println("Setup end");
}


// ========================================
// Loop
void loop() 
{
  // Be sure that the ethernet is connected
  if (!ETH.connected() || !ETH.hasIP())
  {
    Serial.println("Ethernet is not connected");
    delay(1000);
    return;
  }

  if (!s7_plc_client.Connected)
  {
    // Try to connect to the PLC
    Serial.print("Connecting to: ");
    Serial.println(s7_plc_ip);

    // S7 connect parameters: IP, Rack number, Slot number
    // S7-1200/1500 are typically use Rack 0, Slot 1 but this depends on the PLC configuration
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
      delay(1000);
      return;
    }
  }

  // At this point we are connected to the PLC
  //Serial.println("Reading from PLC");
  // DB number, start byte, length in bytes, pointer to the destination to read data to; if NULL then it uses an internal buffer
  int res = s7_plc_client.ReadArea(S7AreaDB, 1, 0, 1, NULL);
  if (res != 0)
  {
    Serial.print("Read Failure: ");
    Serial.println(res);
    s7_plc_client.Disconnect();
    delay(1000);
    return;
  }

  // Read data from PLC and parsing it. Use S7 helper functions to read from the internal buffer
  //Serial.print("Read Ok. Data: ");
  //Serial.println(S7.ByteAt(0), HEX);
  
  // Decide the color based on the buttons (bits 2 and 3)
  uint8_t r, g, b;
  if (S7.BitAt(0, 2))
  {
    // Bit 2. Green button is pressed
    r = 0; g = 255; b = 0;
  }
  else if (!S7.BitAt(0, 3))
  {
    // Bit 3. Red button is pressed; it is a normally-closed contact so active when the bit is 0
    r = 255; g = 0; b = 0;
  }
  else
  {
    // No buttons pressed - white color by default
    r = 255; g = 255; b = 255;
  }

  // Decide the mode based on the switches (bits 0 and 1)
  // Bit 0: on/off master switch. Bit 1: chase effect enable. Both live in DB1.DBB0
  if (!S7.BitAt(0, 0))
  {
    // On/off switch is off. Turn the strip off
    colorSet(0, 0, 0);
  } else if (S7.BitAt(0, 1))
  {
    // Switch 2 (effect) is on - run chase light effect
    chase_r = r; chase_g = g; chase_b = b;
    chase_enable = true;
  }
  else
  {
    // Switch 2 (effect) is off - just set the selected color
    colorSet(r, g, b);
  }
  
  // Short delay
  delay(100);
}


// ========================================
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


// ========================================
// Set all LEDs to the same color
void colorSet(uint8_t r, uint8_t g, uint8_t b) 
{
  // Disable chase effect if it is enabled
  if (chase_enable)
  {
    chase_enable = false;
    // Wait to be sure that chase got disabled 
    delay(300);
  }

  for(uint16_t i = 0; i < LED_strip.numPixels(); i++) 
  {
      LED_strip.setPixelColor(i, r, g, b);
  }
  LED_strip.show();
}


// ========================================
// Chasing lights effect
void ChaseTask(void *pvParameters)
{
    while (true)
    {
        if (!chase_enable)
        {
          delay(50);
          continue;
        }

        // Chase efefct in enabled
        for (int i = 0; i < 3; i++) 
        {
          // Turn every third pixel on (phase-shifted by i each iteration)
          for (int j = 0; j < LED_strip.numPixels(); j = j + 3) 
          {
            LED_strip.setPixelColor(i + j, chase_r, chase_g, chase_b);
          }
          LED_strip.show();   
          delay(100);

          // Turn every third pixel off
          for (int j = 0; j < LED_strip.numPixels(); j = j + 3) 
          {
            LED_strip.setPixelColor(i + j, 0, 0, 0);
          }
        }
    }
}

// Fucntion to configure light chase task
void Chase(uint8_t r, uint8_t g, uint8_t b) 
{

}
