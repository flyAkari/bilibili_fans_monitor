/**********************************************************************
 * 项目：bilibili粉丝数监视器
 * 硬件：适用于NodeMCU ESP8266 + MAX7219
 * 功能：连接WiFi后获取指定用户的哔哩哔哩实时粉丝数并在8位数码管上居中显示
 * 作者：flyAkari 会飞的阿卡林 bilibili UID:751219
 * 日期：2018/09/18
 **********************************************************************/
/*2019/04/15更新说明:v1.2
  1. 删除串口监视器输入数字测试显示功能.
  2. 串口波特率改为115200.
  3. 增加数码管显示故障码功能.
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
#include <ArduinoJson.h>
#include <SPI.h>
//---------------修改此处""内的信息-----------------------
const char *ssid = "WiFi_SSID";          //WiFi名
const char *password = "WiFi_Password";  //WiFi密码
String biliuid = "bilibili_UID";         //bilibili UID
//-------------------------------------------------------

const unsigned long HTTP_TIMEOUT = 5000;
WiFiClient client;
HTTPClient http;
String response;
int follower = 0;
const int slaveSelect = 5;
const int scanLimit = 7;

void setup()
{
    Serial.begin(115200);
    while (!Serial)
        continue;
    Serial.println("bilibili fans monitor, version v1.2");

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

    Serial.print("Connecting WiFi...");
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED){
        delay(500);
        Serial.print(".");
    }
    Serial.println("");
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
}

bool getJson()
{
    bool r = false;
    http.setTimeout(HTTP_TIMEOUT);
    http.begin("http://api.bilibili.com/x/relation/stat?vmid=" + biliuid);
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

void loop()
{
    if (WiFi.status() == WL_CONNECTED){
        if (getJson()){
            if (parseJson(response)){
                displayNumber(follower);
            }
        }
    }else{
        Serial.println("[WiFi] Waiting for reconnect...");
        errorCode(0x1);
    }
    delay(1000);
}

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
