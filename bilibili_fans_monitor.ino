/**********************************************************************
 * 项目：bilibili粉丝数监视器
 * 硬件：适用于NodeMCU ESP8266 + MAX7219
 * 功能：连接WiFi后获取指定用户的哔哩哔哩实时粉丝数并在8位数码管上居中显示
 * 作者：flyAkari 会飞的阿卡林 bilibili UID:751219
 * 源作者：HiGeek工作室 BiliBili UID:4893237
 * 日期：2018/09/18
 **********************************************************************/
/*2018/12/18 V1.1 更新说明：
  上电后数码管显示初始化为"--------", 直到获取到粉丝数.
  可从串口监视器输入数字测试显示连接是否正常, 波特率选择119200.*/

/*
  2019/4/6 V1.2 更新说明“
  原作者HiGeek来维护了当时瞎鸡儿乱写的代码，逻辑错乱，功能死板。居然还有while(1)的傻吊操作。
  在今天刷BiliBili的时候无意中发现自己当初瞎吉儿乱写的代码居然在流通，顿时感觉脸红耳赤，非常丢人。于是乎fork下来，重新维护位于2018/8/23写的粪代码。

  ***加了ERRORCODE显示，如果出现错误会在数码管上显示错误代码
  Error--1 UID（用户ID）填写错误
  Error--2 API无法连接，请检查当前使用的无线网能否正常浏览BiliBili。或者有可能是API地址更新。

  ***加了开机简单的自建，用于检查数码管是否有坏块。

  ***加了WiFi连接过程的动画

  ***加了WiFi断线重连
*/

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

#define CS_PIN 5

//---------------修改此处""内的信息--------------------
const char* ssid     = "";  //WiFi名
const char* password = "";  //WiFi密码
String biliuid       = "";  //bilibili UID 用户ID
//----------------------------------------------------

DynamicJsonDocument jsonBuffer(400);
WiFiClient client;
Ticker blinker;

void setup()
{
  Serial.begin(119200);
  SPI.begin();

  Serial.println("bilibili fans monitor, version v1.2");

  pinMode(CS_PIN, OUTPUT);
  digitalWrite(CS_PIN, LOW);

  sendCommand(12, 1);               //显示控制 1使能 0失能
  sendCommand(15, 1);               //全亮测试
  delay(500);
  sendCommand(15, 0);
  sendCommand(10, 15);              //亮度 15是最大
  sendCommand(11, 7);               //扫描位数，八个数
  sendCommand(9, 0xff);             //解码模式，详情请百度MAX7219
  digitalWrite(CS_PIN, HIGH);

  blinker.attach_ms(1,checkInput);  //开启监听串口输入功能 （此功能是调度器调度）

  Serial.println("LED Ready");
  Serial.print("Connecting WiFi...");

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);       //连接WiFi

  // while (WiFi.status() != WL_CONNECTED) {
  //   delay(500);
  //   Serial.print(".");
  // }
  connectWiFi();

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void loop()
{
  if (WiFi.status() == WL_CONNECTED) //此处感谢av30129522视频作者的开源代码        (HiGeek工作室：我才是源码编写者，哭哭)
  {
    HTTPClient http;
    http.begin("http://api.bilibili.com/x/relation/stat?vmid=" + biliuid);
    int httpCode = http.GET();

    Serial.print("HttpCode:");
    Serial.print(httpCode);

    if (httpCode == 200) {
      String resBuff = http.getString();
      DeserializationError error = deserializeJson(jsonBuffer, resBuff);
      // if (error) { //这段异常处理没有必要，因为B站的API基本上不会变。所以接收到的数据不会有变化，也就不会出错。
      //   Serial.println("json error");
      //   errorCode(3);  //JSON反序列化错误，强行中止运行
      //   while (1);
      // }
      JsonObject root = jsonBuffer.as<JsonObject>();
      long code = root["code"];
      if (code != 0) {
        errorCode(1); //UID错误，强行中止运行
        while(1);
      }
      long fans = root["data"]["follower"];
      Serial.print("     Fans:");
      Serial.println(fans);
      displayNumber(fans);
      delay(1000);
    }
    else{
      errorCode(2); //API无法连接，请检查网络是否通常。或者有可能是API地址更新。
      while(1);
    }
  }
  else{
    connectWiFi();
  }
  
}

void connectWiFi(){
  while(WiFi.status() != WL_CONNECTED){
    sendCommand(9, 0x00);
    for(int i = 2; i < 0x80; i = i << 1) {
      for(int x = 1; x < 9; x++) {
        sendCommand(x, i);
      }
      delay(100);
    }
  }
  sendCommand(9, 0xff);
}

// void checkInput()
// {
//   if (Serial.available())   //Test number display
//   {
//     char ch = Serial.read();
//     if (ch == '\n')
//     {
//       displayNumber(number);
//       Serial.println(number);
//       number = 0;
//     }
//     else
//     {
//       number = (number * 10) + ch - '0';
//     }
//   }
// }

void checkInput() { // HiGeek更新
  static String number_str;

  if(Serial.available()) {
    char buff = Serial.read();

    if (buff == '\n') {
      displayNumber(number_str.toInt());
      Serial.println(number_str);
      number_str = "";
    }
    else {
      number_str += (char)Serial.read();
    }
    
  }
}

void sendCommand(int command, int value)
{
  digitalWrite(CS_PIN, LOW);
  SPI.transfer(command);
  SPI.transfer(value);
  digitalWrite(CS_PIN, HIGH);
}

void displayNumber(int number) //display number in the middle
{
  // if (number < 0 || number > 99999999) return;
  int x;
  int tmp = number;
  for (x = 1; tmp /= 10; x++);
  for(int i = 1; i < 9; i++) sendCommand(i, 0x0f);
  // for (int i = 1; i < 9; i++)
  // {
  //   if (i < (10 - x) / 2 || i >= (x / 2 + 5))
  //   {
  //     sendCommand(i, 0xf);
  //   }
  //   else
  //   {
  //     int character = number % 10;
  //     sendCommand(i, character);
  //     number /= 10;
  //   }
  // }
  int y = (8 - x) / 2;
  for(int i = 0; i < x; i++)
  {
    sendCommand(i+y+1, number%10);
    number /= 10;
  }
  
}

void errorCode(int e_code){
  sendCommand(9, 0x01);
  sendCommand(8, 0x4f);
  sendCommand(7, 0x05);
  sendCommand(6, 0x05);
  sendCommand(5, 0x1d);
  sendCommand(4, 0x05);
  sendCommand(3, 0x01);
  sendCommand(2, 0x01);
  sendCommand(1, e_code);
}
