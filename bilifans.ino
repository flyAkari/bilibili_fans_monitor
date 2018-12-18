/**********************************************************************
 * 项目：bilibili粉丝数监视器v1.1
 * 硬件：适用于NodeMCU ESP8266 + MAX7219
 * 功能：连接WiFi后获取指定用户的哔哩哔哩实时粉丝数并在8位数码管上居中显示
 * 作者：flyAkari 会飞的阿卡林 bilibili UID:751219
 * 日期：2018/09/18
 **********************************************************************/
/*2018/12/18更新说明：
  上电后数码管显示初始化为"--------", 直到获取到粉丝数.
  可从串口监视器输入数字测试显示连接是否正常, 波特率选择9600.*/

 //硬件连接说明：
 //MAX7219 --- ESP8266
 //  VCC   --- 3V(3.3V)
 //  GND   --- G (GND)
 //  DIN   --- D7(GPIO13)
 //  CS    --- D1(GPIO5)
 //  CLK   --- D5(GPIO14)
#include <SPI.h>
#include <ESP8266WiFi.h>
#include <ArduinoJson.h>
#include <ESP8266HTTPClient.h>
#include <Ticker.h>
//---------------修改此处""内的信息--------------------
const char* ssid     = "YourWiFiName";     //WiFi名
const char* password = "YourWiFiPassword"; //WiFi密码
String biliuid       = "751219";           //bilibili UID
//----------------------------------------------------
DynamicJsonDocument jsonBuffer(200);
WiFiClient client;
Ticker blinker;

const int slaveSelect = 5;
const int scanLimit = 7;

int number = 0;

void setup()
{
  Serial.begin(9600);
  Serial.println("bilibili fans monitor, version v1.1");
  SPI.begin();
  pinMode(slaveSelect, OUTPUT);
  digitalWrite(slaveSelect, LOW);
  sendCommand(12, 1);              //Shutdown,open
  sendCommand(15, 0);              //DisplayTest,no
  sendCommand(10, 15);             //Intensity,15(max)
  sendCommand(11, scanLimit);      //ScanLimit,8-1=7
  sendCommand(9, 255);             //DecodeMode,Code B decode for digits 7-0
  digitalWrite(slaveSelect, HIGH);
  initdisplay();
  blinker.attach_ms(1,checkInput); //开启监听串口输入功能
  Serial.println("LED Ready");
  Serial.print("Connecting WiFi...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void loop()
{
  if (WiFi.status() == WL_CONNECTED) //此处感谢av30129522视频作者的开源代码
  {
    HTTPClient http;
    http.begin("http://api.bilibili.com/x/relation/stat?vmid=" + biliuid);
    auto httpCode = http.GET();
    Serial.print("HttpCode:");
    Serial.print(httpCode);
    if (httpCode > 0) {
      String resBuff = http.getString();
      DeserializationError error = deserializeJson(jsonBuffer, resBuff);
      if (error) {
        Serial.println("json error");
        while (1);
      }
      JsonObject root = jsonBuffer.as<JsonObject>();
      long code = root["code"];
      Serial.print("     Code:");
      Serial.print(code);
      long fans = root["data"]["follower"];
      Serial.print("     Fans:");
      Serial.println(fans);
      displayNumber(fans);
      delay(1000);
    }
  }
}

void checkInput()
{
  if (Serial.available())   //Test number display
  {
    char ch = Serial.read();
    if (ch == '\n')
    {
      displayNumber(number);
      Serial.println(number);
      number = 0;
    }
    else
    {
      number = (number * 10) + ch - '0';
    }
  }
}

void sendCommand(int command, int value)
{
  digitalWrite(slaveSelect, LOW);
  SPI.transfer(command);
  SPI.transfer(value);
  digitalWrite(slaveSelect, HIGH);
}

void displayNumber(int number) //display number in the middle
{
  if (number < 0 || number > 99999999) return;
  int x = 1;
  int tmp = number;
  for (x = 1; tmp /= 10; x++);
  for (int i = 1; i < 9; i++)
  {
    if (i < (10 - x) / 2 || i >= (x / 2 + 5))
    {
      sendCommand(i, 0xf);
    }
    else
    {
      int character = number % 10;
      sendCommand(i, character);
      number /= 10;
    }
  }
}

void initdisplay()
{
  sendCommand(8, 0xa);
  sendCommand(7, 0xa);
  sendCommand(6, 0xa);
  sendCommand(5, 0xa);
  sendCommand(4, 0xa);
  sendCommand(3, 0xa);
  sendCommand(2, 0xa);
  sendCommand(1, 0xa);
}
