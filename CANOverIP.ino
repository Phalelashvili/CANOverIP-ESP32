#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <EEPROM.h>
#include <ESP32CAN.h>
#include <CAN_config.h>
#include "Logger.h"

#define newLine 0x0A     // new line character to separate frames
#define idSeparator 'x'
// #define dataLength 8

const char* ssid = "Error 404: SSID Not Found";
const char* password = "";
const char middlemanServer[26] = "http://192.168.0.7:5000";

CAN_device_t CAN_cfg;

WiFiServer Server(3000); // act as host for other devices
WebServer Web(80); // web server for error reporting/debugging
WiFiClient Client;

short deviceID;

CAN_frame_t canFrame;
unsigned int canID;
unsigned int canBuffer[8];
unsigned int dataLength;

bool skipConnect;
int bufferCount;

int lastFrameSent;

Logger Log;

void connectToOtherDevice(bool dropped = false)
{
    Client.stop(); // stop current connection

    if (dropped)
    {
        Log.println("Connection dropped, reconnecting to other device...");

        Client.flush(); // flush outgoing buffer
        while(Client.available()) // flush incoming buffer
            Client.read();
    }
    else
    {
        Log.println("Connecting to other device...");
    }

    HTTPClient http;
    IPAddress otherDeviceIP;
    String ipAddrStr;

    http.begin((String)middlemanServer + "/update?id=" + deviceID + "&localIP=" + WiFi.localIP().toString());
    http.setConnectTimeout(2000); // http request timeout in milliseconds

    const int responseCode = http.GET();
    Log.println("   HTTP code: " + (String)responseCode);
    
    skipConnect = true; // changes value of http connection is successful
    switch(responseCode)
    {
        case 200:
            ipAddrStr = http.getString();
            otherDeviceIP.fromString(ipAddrStr);
            Log.println("   Connecting to " + ipAddrStr);
            skipConnect = false;
            break;
        case 500:
            Log.println("   " + (String)middlemanServer + " Returned Interval Server Error (500):\n" + http.getString());
            break;
        default:
            if(responseCode < 0)
            {
                Log.println("   " + (String)middlemanServer + " is offline or can't be reached");
            }
            else
            {
                Log.println("   Uncaught case, server response:\n" + http.getString());
            }
            break;
    }
    http.end();

    if (skipConnect || !Client.connect(otherDeviceIP, 3000, 2000)) // second parameter is predefined port for connection, third parameter is timeout in milliseconds
    { // connection failed, this device will now act as host
        Log.println("   Client connection skipped, refused or timed out, Waiting for connection...");

        while (!Client.connected()) {
            Client = Server.available(); // wait until other device connects
            HandleWebRequests();
            delay(100);
        }
        Log.println("   " + Client.remoteIP().toString() + " connected");
    }
}

void HandleWebRequests() // TODO: thread handling web requests
{
    Web.handleClient();
}

void HomePage()
{
   Web.send(200, "text/plain", Log.GetLog(Client.available()));
}

void CANSetup()
{
    CAN_cfg.speed=CAN_SPEED_500KBPS;
    CAN_cfg.tx_pin_id = GPIO_NUM_17; // GREEN
    CAN_cfg.rx_pin_id = GPIO_NUM_16; // BLUE

    CAN_cfg.rx_queue = xQueueCreate(10,sizeof(CAN_frame_t)); // create a queue for CAN receiving

    ESP32Can.CANInit();

    // set constant values in can frame
    canFrame.FIR.B.FF = CAN_frame_std;
    canFrame.FIR.B.DLC = 8;

    Log.println("CAN Initialized");
}

void NetworkSetup()
{
    Serial.println("--------------------");
    Log.print("DeviceID: ");
    Log.println(String(deviceID));

    Log.println("Connecting to " + (String)ssid);

    WiFi.begin(ssid, password);
    WiFi.setSleep(false);
    while (WiFi.status() != WL_CONNECTED) delay(100);
    WiFi.setHostname("Device #" + deviceID); // TODO: fix this shit, doesn't set hostname but it returns true
    Log.println("   Device IP: " + WiFi.localIP().toString());

    Server.begin();

    Web.on("/", HomePage);
    Web.begin();

    connectToOtherDevice();
}

void EEPROMSetup()
{
    EEPROM.begin(sizeof(deviceID));
    // EEPROM.write(0, DEVICEID); // assign id to device
    EEPROM.commit();
    delay(250);
    deviceID = EEPROM.read(0);
    if(deviceID == 255)
    {
        while (!Serial.available())
        {
            Serial.println("Device has no ID, enter new device ID"); // write hex value from serial port 
            delay(1000);
        };
        EEPROM.write(0, Serial.read()); // assign id to device
        EEPROM.commit();
    }
}

void setup()
{
    Serial.begin(115200); // for debugging

    EEPROMSetup();    
    NetworkSetup();
    CANSetup();
}

void buildCANFrame()
{
    canFrame.MsgID = canID;
    canFrame.FIR.B.DLC = dataLength;
    for (int i = 0; i < dataLength; i++)
        canFrame.data.u8[i] = canBuffer[i];
}

void receive()
{
    if (Client.available() < 10) return; // newline, canid, data (8)
 
    if (Client.read() != newLine) return; // out of sync, restart loop
    canID = Client.readStringUntil(idSeparator).toInt();

    dataLength = Client.read();
    if (dataLength > 8) 
    {   // data can not be more than 8 nibbles
        // if data received incorrectly device will be stuck in infinite loop if incorrect data length is more than serial buffer can store 
        Serial.print("INVALID DATA LENGTH ");
        Serial.print(dataLength);
        Serial.print(" ID:");
        Serial.println(canID);
        return;
    }

    bufferCount = 0;
    while (Client.available())
    {
        char c = Client.read();
        if (c == newLine)
            break; // wait for frame end
        
        canBuffer[bufferCount] = c;
        
        if (++bufferCount == dataLength) // when buffercount becomes 7 (one off dataLength) buffer is full
            break;
    }

    if (bufferCount != 8) 
    {   // make sure data is valid, if it isn't, cancel processing current frame
        Serial.print("INVALID DATA ");
        Serial.print(bufferCount);
        Serial.print("/");
        Serial.println(dataLength);
        return;
    }

    // build frame and send it to can transceiver
    buildCANFrame(); // everything needed is global variable, no need for arguments
    ESP32Can.CANWriteFrame(&canFrame);

    // update logs

    Log.SetLastMessage("< " + (String)canID + " { ", millis());
    for (int i = 0; i < dataLength; i++)
        Log.AppendLastMessage(String(canBuffer[i], HEX) + " ");
    Log.AppendLastMessage("}\n");
}

void writeToClient(uint8_t data)
{
    lastFrameSent = millis();
    if (!Client.write(data))
    { // reconnect if other device dropped connection (written bytes returned are 0)
        connectToOtherDevice(true);
        writeToClient(data);
    }
}

void send(){
    if(xQueueReceive(CAN_cfg.rx_queue, &canFrame, 3*portTICK_PERIOD_MS) == pdTRUE)
    { // if can has data available
        canID = canFrame.MsgID;
        dataLength = canFrame.FIR.B.DLC;

        writeToClient(newLine);
        Log.SetLastMessage("> " + (String)canID + " { ", millis());
        Client.print(canID);
        Client.write(idSeparator);
        // writeToClient(canID);
        writeToClient(dataLength);
        
        for(int i = 0; i < dataLength; i++)
        {
            writeToClient(canFrame.data.u8[i]);
            Log.AppendLastMessage(String(canFrame.data.u8[i], HEX) + " ");
        }
        Log.AppendLastMessage("}\n");
    }
}

void send_test()
{ // send data to change RPM
    canID = 0x280;
    dataLength = 8;
    
    unsigned char canBuffer[dataLength] = {0x49, 0x0E, 0xDD, 0x0D, 0x0E, 0x00, 0x1B, 0x0E};

    writeToClient(newLine);
    Client.print(canID);
    Client.write(idSeparator);
    writeToClient(dataLength);
    for (int i = 0; i < dataLength; i++)
        writeToClient(canBuffer[i]);

    Log.SetLastMessage("> test RPM data\n", millis());
}

void receive_test()
{
    canID = 0x280;
    dataLength = 8;
    unsigned char canBuffer[dataLength] = {0x49, 0x0E, 0xDD, 0x0D, 0x0E, 0x00, 0x1B, 0x0E};

    buildCANFrame();
    ESP32Can.CANWriteFrame(&canFrame);
}

void loop()
{
    // if nothing has been sent in 3 seconds, ping it just to check if connection is alive
    // it will be just ignored in receive function since it waits for newLine 
    if (millis() - lastFrameSent > 3000)
        writeToClient(0xFF);

    HandleWebRequests();

    receive();
    
    send();

    // if (deviceID == 0)
    //     send_test();
}
