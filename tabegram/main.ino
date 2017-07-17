#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <NTP.h>
#include <SPI.h>

#include <ThingSpeak.h>

#include <Adafruit_NeoPixel.h>
//#include <avr/power.h>

#define PIN 4
#define NUMPIXELS 1

Adafruit_NeoPixel pixels = Adafruit_NeoPixel(NUMPIXELS, PIN, NEO_GRB + NEO_KHZ800);

//#define CS 15
#define CS 5

WiFiClient client;

// Wifi info (下記を書き換えれば他のWifiにも接続可能)
const char* ssid = ""; // TODO WifiのSSIDの設定
const char* password = ""; // TODO Wifiのpass設定

// Request info
const char* host = ""; // TODO webscript.ioのhost設定
const int port = 80;

// Device info
const String deviceid = "hogeageagehuga"; // デバイス固有のID (もし動的に取れるのであればそちらを利用)

// initial info
const int empty_weight = 130; // お茶碗がからの時の計測値 (茶碗によって変化するがデモ用)

const int heavy_value = 200; // 大盛り値 TODO 要変更

const int middle_value = 150; // 中盛り値 TODO 要変更

const int light_value = 100; // 小盛り値 TODO 要変更

const int finish_time = 15; // ご馳走様判断時間 (180秒) 一旦は30秒にしておくけど最大180秒とする

int previous_empty_time = 0;

unsigned long myChannelNumber = 0; // TODO Sperkの番号セット
const char * myWriteAPIKey = ""; // TODO SpeakのAPI Keyのセット

void setup() {
  pinMode(CS, OUTPUT);
  //Initialize serial and wait for port to open:
  Serial.begin(9600);
  delay(1500);

  pixels.begin();
  showLed();

  // Connect Wifi-------
  Serial.println();
  Serial.print("Connecting to ");
  Serial.print(ssid);
  Serial.print(" ");

  WiFi.begin(ssid, password);
  delay(1000);
  int dot = 0;
  while(WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
    dot++;
    if (dot > 40) {
      dot = 0;
      Serial.println();
      Serial.println("Connection Failed. Rebooting...");
      ESP.restart();
      delay(1000);
    }
  }
  Serial.println();
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  ntp_begin(2390);

  ThingSpeak.begin(client);
}

int current_weight = 0;
int previous_weight = 0;
int max_weight = 0;
int total_weight = 0;

void loop() {
  int current_time = now();

  // 重さを計測 (analogInput)
  int sample[3];
  int data = 0;
  for (byte j = 0; j < 3; j++) {
    for (byte i = 0; i < 3; i++) {
      SPI.begin();
      digitalWrite(CS, LOW);
      //ch0
      byte highbyte = SPI.transfer(0b01101000);
      //ch1
      byte lowbyte = SPI.transfer(0x00);
      digitalWrite(CS, HIGH);
      SPI.end();
      sample[i] = ((highbyte << 8) + lowbyte) & 0x03FF;
      delay(100);
    }
    data += med3(sample[0], sample[1], sample[2]);
  }
  data /= 3;
  Serial.print("data = ");
  Serial.println(data);

  // 最小値より大きい時だけ計測
  if (data > empty_weight) {
    if (max_weight == 0) {
      max_weight = data;
      Serial.print("max_weight is ");
      Serial.println(max_weight);
    } else {
      if (data > max_weight) {
        max_weight = data;
        Serial.print("max_weight is ");
        Serial.println(max_weight);
      }
    }
  } else {
    total_weight += max_weight;
    Serial.print("current_time is ");
    Serial.println(current_time);
    Serial.print("previous_empty_time is ");
    Serial.println(previous_empty_time);
    if (previous_empty_time == 0 || max_weight != 0) {
      previous_empty_time = current_time;
    } else if((previous_empty_time + finish_time) < current_time && total_weight != 0) {
      // 180秒経過したら通信する
      httpRequestWebScript(total_weight);
      Serial.println("httpRequestWebScript!!");
      Serial.println(total_weight);
      previous_empty_time = current_time;
      total_weight = 0;
    }
    max_weight = 0;
  }
  delay(5000);
  requestThingSpeak(data);
}

/**
 * LEDを消灯させる
 */
void showLed() {
  pixels.setPixelColor( 0, pixels.Color(255, 0, 0));
  pixels.show();
  delay(2000);
  // 全てのLEDを消灯
  pixels.clear();
  pixels.show();
  delay(1000);
}

/**
 * webscript.ioへのリクエスト
 * Arduinoはhttpsがいけないので一旦webscriptを挟む
 */
void httpRequestWebScript(int weight) {
  Serial.println("httpRequestWebScript Start");
  Serial.println(host);

  // AWS API Gatewayへ接続
  if (!client.connect(host, port)) {
    Serial.println("connection failed");
    return;
  }
  Serial.println("connection success");

  time_t n = now();

  // HTTP Request Body
  String body = "{\"data\":{\"deviceId\":\""+deviceid+"\", \"weight\":\""+String(weight)+"\", \"measureTimestamp\":\""+String(n)+"\"}}";

  // HTTP Request Header
  client.println("POST /logs HTTP/1.1");
  client.println("Host: " + String(host));
  client.println("User-Agent: arduino");
  client.println("Accept: */*");
  client.println("Connection: close");
  client.println("Content-Type: application/json");
  client.print("Content-Length: ");
  client.println(body.length());
  client.println();
  client.println(body);
  Serial.println(client.available());
  while(client.available()){
    String line = client.readStringUntil('\r');
    Serial.print(line);
  }
  client.stop();
}

/**
 * 中間値を返す
 */
int med3(int a, int b, int c)
{
  if (a >= b)
    if (b >= c)
      return b;
    else if (a <= c)
      return a;
    else
      return c;
  else if (a > c)
    return a;
  else if (b > c)
    return c;
  else
    return b;
}

/**
 * Thing Speakにデータを送る
 */
void requestThingSpeak(int data) {
  ThingSpeak.setField(1, float(data));
  ThingSpeak.writeFields(myChannelNumber, myWriteAPIKey);
}

/**
 * webscript.ioへのリクエスト
 * Arduinoはhttpsがいけないので一旦webscriptを挟む
 */
void httpRequestTmpWebScript(int weight) {
  Serial.println("httpRequestTmpWebScript Start");
  Serial.println(host);

  // AWS API Gatewayへ接続
  if (!client.connect(host, port)) {
    Serial.println("connection failed");
    return;
  }
  Serial.println("connection success");

  time_t n = now();

  // HTTP Request Body
  String body = "{\"data\":{\"deviceId\":\""+deviceid+"\", \"weight\":\""+String(weight)+"\", \"measureTimestamp\":\""+String(n)+"\"}}";

  // HTTP Request Header
  client.println("POST /tmp HTTP/1.1");
  client.println("Host: " + String(host));
  client.println("User-Agent: arduino");
  client.println("Accept: */*");
  client.println("Connection: close");
  client.println("Content-Type: application/json");
  client.print("Content-Length: ");
  client.println(body.length());
  client.println();
  client.println(body);
  Serial.println(client.available());
  /*
  while(client.available()){
    String line = client.readStringUntil('\r');
    Serial.print(line);
  }
  */
  client.stop();
}
