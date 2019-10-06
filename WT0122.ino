//#define ESP8266
//#define ETHERNET
//#define DEBUG

#include <PubSubClient.h> 

#ifdef ESP8266
  #define ledPin 16  //NodeMCU: Onboard LED = digital pin 16 
  #define inputPin 15 //NodeMCU: Pin D8 = 15
  #include <Wire.h>  // Only needed for Arduino 1.6.5 and earlier
  #include "SSD1306Wire.h" // legacy include: `#include "SSD1306.h"`
  
  #ifdef ETHERNET
    #include <ESP8266WiFi.h>
    #define EthernetClient WiFiClient
    #define EthernetServer WiFiServer 
  #endif
  
  // Initialize the OLED display using Wire library
  SSD1306Wire  display(0x3c, D1, D2);
  const char* ssid     = "xxxxxx";
  const char* password = "xxxxxxxxxxxxxxxx";

#else
  #ifdef ETHERNET
    #include <Ethernet.h>
    byte mac[] = {0xDE, 0xED, 0xBA, 0xFE, 0xEE, 0xE6};
  #endif   

  #define ledPin 13  //Arduino: Onboard LED = digital pin 13 
  #define inputPin 3
#endif 

#ifdef ETHERNET
  unsigned char MQTTServerIp[4] = {192, 168, 144, 99};
  unsigned short MQTTServerPort = 1883;
  EthernetClient ethClient;
  PubSubClient MQTT_Client(ethClient); 
  unsigned long lastReconnectAttempt = 0; 
  bool isJustConnected = false;
#endif

#define DATA_SIZE 120
#define SEPARATOR_HIGH 9500 //Delay for the high part of the PREAMBLE in microseconds
#define SEPARATOR_LOW 4500 //Delay for the low part of the PREAMBLE in microseconds
#define NOISE_TRESHOLD 200 //Minimum delay of a pulse in microseconds
#define SEP_WAIT 11000

unsigned long t1, t0;
unsigned short durationH;
unsigned short durationL;
unsigned int pwL[DATA_SIZE];
unsigned int pwH[DATA_SIZE];
unsigned char resDec[DATA_SIZE/4];
unsigned long lastReception = 0;

/******************************************************************************/
/* It calculates the two different pulse width based on 16 samples            */
/* Out params: min and max lengths of the pulses                              */
/* Return value: pulse width limit distinguish single/double or 0/1           */ 
/******************************************************************************/
unsigned short CalculateLimits(unsigned short* zeroH, unsigned short* oneH)
{
  unsigned short sampling[16] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
  unsigned short max = 0;
  unsigned short min = 0xFFFF;
  unsigned short retVal = 0;
  
  for(int i=0, j=0; (i < DATA_SIZE) && (j < 16); i++, j++)
  {
    if ((pwH[i] > NOISE_TRESHOLD) && (pwH[i] < SEPARATOR_HIGH))
      sampling[j] = pwH[i];
    else
      j=-1;
  }

  if (sampling[15] != 0)
  {
    for(int j=0; j < 16; j++)
    {
      if (max < sampling[j])
        max = sampling[j];
      if (min > sampling[j])
        min = sampling[j];
    }
    retVal = (min + max) / 2;
    *zeroH = min;
    *oneH = max;
    #ifdef DEBUG
        Serial.print("Zero High Pulse Width:");
        Serial.println(min);
        Serial.print("One High Pulse Width:");
        Serial.println(max);
    #endif
  }
  else
  {
    #ifdef DEBUG
      Serial.print("Incorrect data sequence. ");
    #endif
    retVal = 0;
  }
  if (max < min + (min / 10)) //Diff is lees than 10% betwen the two values
  {
    #ifdef DEBUG
      Serial.print("SHORT (");
      Serial.print(min);
      Serial.print(") and LONG (");
      Serial.print(max);
      Serial.println(") pulse cannot be distinguished.");
    #endif
    retVal = 0;
  }
  return retVal;
}

/******************************************************************************/
/* It decodes a simple BCD coding, calculates of the temperature and 
/* sets the MQTT var.          */
/* Parameter: PW_limit - pulse width limit distinguish 0/1                    */ 
/******************************************************************************/
void BCDDecode(unsigned short PW_limit)
{
  int j = 0;
  unsigned char decVal = 0;
  int decIdx = 0;
  memset(resDec,0,sizeof(resDec));
   
  #ifdef DEBUG
    Serial.print("BCD: ");
  #endif
  for(int i=0; (i < DATA_SIZE) && (pwH[i] > NOISE_TRESHOLD); i++)
  {
    if ((pwH[i] > SEPARATOR_HIGH))
    {
      #ifdef DEBUG
        Serial.println("");
      #endif
      if (decIdx != 0)
      {
        #ifdef DEBUG
           Serial.print("0x"); 
           for (int k = 0; k <= decIdx; k++)
             Serial.print(resDec[k],HEX);
           Serial.println("");
        #endif
        decIdx = 0;
      }
      #ifdef DEBUG
        Serial.print("h(");Serial.print(pwH[i]);Serial.print(")");
      #endif
      j = 0;
      decVal = 0;
    }
    else if (pwH[i] > PW_limit)
    {
      #ifdef DEBUG
        Serial.print(1);
      #endif
      j++;
      decVal = decVal * 2 + 1;
    }
    else if (pwH[i] > NOISE_TRESHOLD)
    {
      #ifdef DEBUG
        Serial.print(0);
      #endif
      j++;
      decVal = decVal * 2;
    }
    if (j%4 == 0)
    {
      #ifdef DEBUG
        Serial.print(" ");
      #endif
      if (j != 0)
      {
        resDec[decIdx] = decVal;
        decIdx++;
        decVal = 0;
      }
    }
  }
  #ifdef DEBUG
    Serial.println("");
  #endif
  if (decIdx != 0)
  {
     Serial.print("0x"); 
     for (int k = 0; k <= decIdx; k++)
       Serial.print(resDec[k],HEX);
     float tVal = (367 - (resDec[4] * 0x10 + resDec[5])) / 10.0;
     Serial.println();
     Serial.println(tVal);
     #ifdef ESP8266
       display.setFont(ArialMT_Plain_16);
       display.drawString(0, 16, String(tVal));
       display.display();
     #endif
     #ifdef ETHERNET
       lastReception = millis();
       if (MQTT_Client.connected())
         MQTT_Client.publish("home/pool/set", String(tVal).c_str(), true);
     #endif
  }
}

void ShowResults()
{
  unsigned short zeroH = 0, oneH = 0;
  unsigned short PW_limit = CalculateLimits(&zeroH, &oneH);
  if (PW_limit && (zeroH > 650) && (zeroH < 900) && (oneH > 1800) && (oneH < 2500))
  {
    #ifdef DEBUG
      Serial.print("PW_limit:"); Serial.println(PW_limit);
    #endif
    BCDDecode(PW_limit);
  }
  Serial.println("-------------");
}

#ifdef ETHERNET
/******************************************************************************/
/* Manages MQTT updates                                                       */
/* no Parameters                                                              */
/* no return value                                                            */
/******************************************************************************/
void MQTT_Process()
{
  if (!MQTT_Client.connected()) 
  { // Client not connected
    long now = millis();
    if (now - lastReconnectAttempt > 5000) 
    {
      lastReconnectAttempt = now;
      // Attempt to reconnect
      if (MQTT_Client.connect("WT0122BridgeClient")) 
      {
        Serial.println("MQTT connected");
        isJustConnected = true;
        //MQTT_Client.subscribe("home/blinds/set");
        lastReconnectAttempt = 0;
        MQTT_Client.loop();
      }
      else
      {
        Serial.print("MQTT connection failed, rc=");
        Serial.print(MQTT_Client.state());
      }
    }
  } 
  else 
  { // Client connected
    MQTT_Client.loop();
  }
} 

int EthernetInit()
{
  bool isEthernetConnected = false;
  #ifdef ESP8266
    WiFi.begin(ssid, password);
  #endif
  for(int i=0; (i < 5) && (isEthernetConnected == false); i++)
  { 
    #ifdef ESP8266
      if (WiFi.status() != WL_CONNECTED) 
    #else
      if (Ethernet.begin(mac) == 0) 
    #endif
    {
      Serial.print("Failed to configure network using DHCP.");
      #ifdef ESP8266
        Serial.print("Wifi status is: ");
        Serial.println(WiFi.status());
      #endif
    }
    else
    {
      isEthernetConnected = true;
      Serial.println("Connected to network using DHCP");
    }
    // Allow the hardware to sort itself out
    delay(1000);
  }
  Serial.print("IP address: ");    
  #ifdef ESP8266
    Serial.println(WiFi.localIP());
  #else
    Serial.println(Ethernet.localIP());
  #endif

  // MQTT Initilaization
  MQTT_Client.setServer(MQTTServerIp, MQTTServerPort);
  lastReconnectAttempt = 0; 
  
  return 0;
} 
#endif
 
void setup() {
  Serial.begin(115200);
  Serial.println("Start...");
  Serial.println();

  pinMode(inputPin, INPUT);

#ifdef ESP8266
  // Initialising the UI will init the display too.
  display.init();

  display.flipScreenVertically();
  display.setFont(ArialMT_Plain_10);
#endif

  //Ethernet Initilaization
#ifdef ETHERNET
  (void)EthernetInit();
#endif
  delay(200);

#ifdef ESP8266
  display.clear();
  display.drawString(0, 0, WiFi.localIP().toString());
  display.display();
  delay(500);
#endif
}

void loop()
{
  bool headReceived = false;
  bool bStop = false;
  int i = 0;
  memset(pwH, 0, sizeof(pwL));
  memset(pwL, 0, sizeof(pwH));
  do
  {
    durationH = pulseIn(inputPin, HIGH);
    t1 = micros();
    durationL = t1 - t0 - durationH;
    t0 = t1;
    if (headReceived == true)
    {
      if (durationH > NOISE_TRESHOLD)
      {  // correct pulse saved
        pwL[i] = durationL;
        pwH[i] = durationH;
        i++;
      }
      else 
      { // incorrect pulse received
        bStop = true;
        i++;
      }
    }
        
    if ((durationH > SEPARATOR_HIGH) && (durationH < SEPARATOR_HIGH + 500) && (headReceived == false))
    { // header detected
      headReceived = true;
      pwL[i] = durationL;
      pwH[i] = durationH;
      i++;
#ifdef ESP8266
  digitalWrite(ledPin, LOW);  //Turn LED ON 
#else
  digitalWrite(ledPin, HIGH);  //Turn LED ON 
#endif 
    }
  }while((i < DATA_SIZE) && !bStop);

#ifdef ETHERNET
  MQTT_Process(); 
  #ifdef ETHERNET
    if (MQTT_Client.connected())
    {
//      if ((lastReception != 0) && (lastReception > millis()-600000))
      {
        MQTT_Client.publish("home/pool/set", "", true);
        lastReception = 0;
      }
    }
  #endif
#endif
  if (i > 24) // more than 24 bit received
    ShowResults(); 

#ifdef ESP8266
    digitalWrite(ledPin, HIGH);  //Turn LED OFF
#else
    digitalWrite(ledPin, LOW);  //Turn LED OFF
#endif 
}
