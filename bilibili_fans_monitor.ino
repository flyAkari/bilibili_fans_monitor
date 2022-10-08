#include <Arduino.h>
/**********************************************************************
 * 项目：bilibili粉丝数监视器
 * 硬件：适用于NodeMCU ESP8266 + MAX7219
 * 功能：连接WiFi后获取指定用户的哔哩哔哩实时粉丝数并在8位数码管上居中显示
 * 作者：flyAkari 会飞的阿卡林 bilibili UID:751219
 * 日期：2018/09/18
 **********************************************************************/
/*2021/03/28更新说明:v1.3
  1. 新增Web配置页面.
  2. 降低请求频率，防止IP被封.*/
/* 使用说明：
 * 初次上电后，用任意设备连接热点WiFi：flyAkari，等待登录页弹出或浏览器
 * 输入192.168.4.1进入WiFi配置页面，输入待连接WiFi名、密码和uid，提交后
 * 如需更改，请先关闭WiFi.*/
/*2019/04/15更新说明:v1.2
  1. 删除串口监视器输入数字测试显示功能.
  2. 增加数码管显示故障码功能.
     E1--WiFi连接中断, 重连中.
     E2--HTTP请求错误, 当前网络不通畅或请求被拒.
     E3--API返回结果错误, 请检查输入的UID是否正确.
     E4--JSON格式错误, 请检查当前网络是否需要认证登陆.*/

/*2018/12/18更新说明:v1.1
  1. 上电后数码管显示初始化为"--------", 直到获取到粉丝数.
  2. 可从串口监视器输入数字测试显示连接是否正常, 波特率选择9600.*/

 //硬件连接说明：
 //MAX7219 --- ESP8266
 //  VCC   --- 3V(3.3V)
 //  GND   --- G (GND)
 //  DIN   --- D7(GPIO13)
 //  CS    --- D1(GPIO5)
 //  CLK   --- D5(GPIO14)

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <EEPROM.h>

char sta_ssid[32] = {0};          //暂存WiFi名
char sta_password[64] = {0};      //暂存WiFi密码
const char *AP_NAME = "flyAkari"; //自定义8266AP热点名

/*#################################### EEPROM #########################################*/
typedef struct
{                  //存储配置结构体
    char uid[11];
    char c_ssid[32];
    char c_pwd[64];
} config_type;
config_type config;

void saveConfig()
{ //存储配置到"EEPROM"
    Serial.println("save config");
    EEPROM.begin(sizeof(config));
    uint8_t *p = (uint8_t *)(&config);
    for (uint i = 0; i < sizeof(config); i++)
    {
        EEPROM.write(i, *(p + i));
    }
    EEPROM.commit(); //此操作会消耗flash写入次数
}

void loadConfig()
{ //从"EEPROM"加载配置
    Serial.println("load config");
    EEPROM.begin(sizeof(config));
    uint8_t *p = (uint8_t *)(&config);
    for (uint i = 0; i < sizeof(config); i++)
    {
        *(p + i) = EEPROM.read(i);
    }
    strcpy(sta_ssid, config.c_ssid);
    strcpy(sta_password, config.c_pwd);
}

/*#################################### Web配置 #########################################*/

//配网及目标日期设定html页面
const char *page_html = "\
<!DOCTYPE html>\r\n\
<html lang='en'>\r\n\
<head>\r\n\
  <meta charset='UTF-8'>\r\n\
  <meta name='viewport' content='width=device-width, initial-scale=1.0'>\r\n\
  <title>Document</title>\r\n\
</head>\r\n\
<body>\r\n\
  <h1>ESP8266配置页</h1>\r\n\
  <form name='input' action='/' method='POST'>\r\n\
    WiFi名称:\r\n\
    <input type='text' name='ssid'><br>\r\n\
    WiFi密码:\r\n\
    <input type='password' name='password'><br>\r\n\
    bilibili uid:<br>\r\n\
    <input type='text' name='uid'><br>\r\n\
    <input type='submit' value='提交'>\r\n\
    <br><br>\r\n\
    <a href='https://space.bilibili.com/751219'>FlyAkari</a>\r\n\
  </form>\r\n\
</body>\r\n\
</html>\r\n\
";
const byte DNS_PORT = 53;       //DNS端口号默认为53
IPAddress apIP(192, 168, 4, 1); //8266 APIP
DNSServer dnsServer;
ESP8266WebServer server(80);

void connectWiFi();

void handleRoot()
{
    server.send(200, "text/html", page_html);
}
void handleRootPost()
{
    Serial.println("handleRootPost");
    if (server.hasArg("ssid"))
    {
        Serial.print("ssid:");
        strcpy(sta_ssid, server.arg("ssid").c_str());
        strcpy(config.c_ssid, sta_ssid);
        Serial.println(sta_ssid);
    }
    else
    {
        Serial.println("[WebServer]Error, SSID not found!");
        server.send(200, "text/html", "<meta charset='UTF-8'>Error, SSID not found!"); //返回错误页面
        return;
    }
    if (server.hasArg("password"))
    {
        Serial.print("password:");
        strcpy(sta_password, server.arg("password").c_str());
        strcpy(config.c_pwd, sta_password);
        Serial.println(sta_password);
    }
    else
    {
        Serial.println("[WebServer]Error, PASSWORD not found!");
        server.send(200, "text/html", "<meta charset='UTF-8'>Error, PASSWORD not found!");
        return;
    }
    if (server.hasArg("uid"))
    {
        Serial.print("uid:");
        strcpy(config.uid, server.arg("uid").c_str());
        Serial.println(config.uid);
    }
    else
    {
        Serial.println("[WebServer]Error, uid not found!");
        server.send(200, "text/html", "<meta charset='UTF-8'>Error, UID not found!");
        return;
    }
    server.send(200, "text/html", "<meta charset='UTF-8'>提交成功"); //返回保存成功页面
    delay(2000);
    //一切设定完成，连接wifi
    saveConfig();
    connectWiFi();
}

void connectWiFi()
{
    WiFi.mode(WIFI_STA);       //切换为STA模式
    WiFi.setAutoConnect(true); //设置自动连接
    WiFi.begin(sta_ssid, sta_password);
    Serial.println("");
    Serial.print("Connect WiFi");
    int count = 0;
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(500);
        count++;
        if (count > 20)
        { //10秒过去依然没有自动连上，开启Web配网功能，可视情况调整等待时长
            Serial.println("Timeout! AutoConnect failed");
            WiFi.mode(WIFI_AP); //开热点
            WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
            if (WiFi.softAP(AP_NAME))
            {
                Serial.println("ESP8266 SoftAP is on");
            }
            server.on("/", HTTP_GET, handleRoot);      //设置主页回调函数
            server.onNotFound(handleRoot);             //设置无法响应的http请求的回调函数
            server.on("/", HTTP_POST, handleRootPost); //设置Post请求回调函数
            server.begin();                            //启动WebServer
            Serial.println("WebServer started!");
            if (dnsServer.start(DNS_PORT, "*", apIP))
            { //判断将所有地址映射到esp8266的ip上是否成功
                Serial.println("start dnsserver success.");
            }
            else
                Serial.println("start dnsserver failed.");
            Serial.println("Please reset your WiFi setting.");
            Serial.println("Connect the WiFi named flyAkari, the configuration page will pop up automatically, if not, use your browser to access 192.168.4.1");
            break; //启动WebServer后便跳出while循环，回到loop
        }
        Serial.print(".");
        if (WiFi.status() == WL_CONNECT_FAILED)
        {
            Serial.print("password:");
            Serial.print(WiFi.psk().c_str());
            Serial.println(" is incorrect");
        }
        if (WiFi.status() == WL_NO_SSID_AVAIL)
        {
            Serial.print("configured SSID:");
            Serial.print(WiFi.SSID().c_str());
            Serial.println(" cannot be reached");
        }
    }
    Serial.println("");
    if (WiFi.status() == WL_CONNECTED)
    {
        Serial.println("WiFi Connected!");
        Serial.print("IP address: ");
        Serial.println(WiFi.localIP());
        server.stop();
        dnsServer.stop();
        //WiFi连接成功后，热点便不再开启，无法再次通过web配网
        //若WiFi连接断开，ESP8266会自动尝试重新连接，直至连接成功，无需代码干预
        //如需要更换WiFi，请在关闭原WiFi后重启ESP8266，否则上电后会自动连接原WiFi，也就无法进入配网页面
    }
}


const unsigned long HTTP_TIMEOUT = 5000;
WiFiClient client;
HTTPClient http;
String response;
int follower = 0;
const int slaveSelect = 5;
const int scanLimit = 7;

void sendCommand(int command, int value);
void errorCode(byte errorcode)
{
    sendCommand(8, 0xa);
    sendCommand(7, 0xa);
    sendCommand(6, 0xa);
    sendCommand(5, 0xb);
    sendCommand(4, errorcode);
    sendCommand(3, 0xa);
    sendCommand(2, 0xa);
    sendCommand(1, 0xa);
}

bool getJson()
{
    bool r = false;
    http.setTimeout(HTTP_TIMEOUT);
    char apistr[60] = "http://api.bilibili.com/x/relation/stat?vmid=";
    strcat(apistr,config.uid);
    String myapistring(apistr);
    http.begin(myapistring);
    int httpCode = http.GET();
    if (httpCode > 0){
        if (httpCode == HTTP_CODE_OK){
            response = http.getString();
            //Serial.println(response);
            r = true;
        }
    }else{
        Serial.printf("[HTTP] GET JSON failed, error: %s\n", http.errorToString(httpCode).c_str());
        errorCode(0x2);
        r = false;
    }
    http.end();
    return r;
}

bool parseJson(String json)
{
    const size_t capacity = JSON_OBJECT_SIZE(4) + JSON_OBJECT_SIZE(5) + 70;
    DynamicJsonDocument doc(capacity);
    deserializeJson(doc, json);

    int code = doc["code"];
    const char *message = doc["message"];

    if (code != 0){
        Serial.print("[API]Code:");
        Serial.print(code);
        Serial.print(" Message:");
        Serial.println(message);
        errorCode(0x3);
        return false;
    }

    JsonObject data = doc["data"];
    unsigned long data_mid = data["mid"];
    int data_follower = data["follower"];
    if (data_mid == 0){
        Serial.println("[JSON] FORMAT ERROR");
        errorCode(0x4);
        return false;
    }
    Serial.print("UID: ");
    Serial.print(data_mid);
    Serial.print(" follower: ");
    Serial.println(data_follower);

    follower = data_follower;
    return true;
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
    if (number < 0 || number > 99999999)
        return;
    int x = 1;
    int tmp = number;
    for (x = 1; tmp /= 10; x++);
    for (int i = 1; i < 9; i++)
    {
        if (i < (10 - x) / 2 || i >= (x / 2 + 5)){
            sendCommand(i, 0xf);
        }else{
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


void setup()
{
    Serial.begin(115200);
    while (!Serial)
        continue;
    Serial.println("bilibili fans monitor, version v1.3");

    SPI.begin();
    pinMode(slaveSelect, OUTPUT);
    digitalWrite(slaveSelect, LOW);
    sendCommand(12, 1);         //Shutdown,open
    sendCommand(15, 0);         //DisplayTest,no
    sendCommand(10, 15);        //Intensity,15(max)
    sendCommand(11, scanLimit); //ScanLimit,8-1=7
    sendCommand(9, 255);        //DecodeMode,Code B decode for digits 7-0
    digitalWrite(slaveSelect, HIGH);
    initdisplay();
    Serial.println("LED Ready");
    loadConfig();
    Serial.print("Connecting WiFi...");
    WiFi.hostname("Smart-ESP8266");
    connectWiFi();
}

unsigned long oldtime=0;
void loop()
{
    server.handleClient();
    dnsServer.processNextRequest();
    if(millis() - oldtime >60000){ //60秒更新一次
        oldtime=millis();
        if (WiFi.status() == WL_CONNECTED){
            if (getJson()){
                if (parseJson(response)){
                    displayNumber(follower);
                }
            }
        }else{
            Serial.println("[WiFi] Waiting to reconnect...");
            errorCode(0x1);
        }
    }
}
