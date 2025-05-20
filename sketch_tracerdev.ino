#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <TinyGPS++.h>

// OLED参数
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// GPS参数
#define GPS_RX 16  // ESP32的RX引脚
#define GPS_TX 17  // ESP32的TX引脚
HardwareSerial gpsSerial(1);
TinyGPSPlus gps;

void setup() {
  Serial.begin(115200);
  Serial.println("Starting GPS Test...");
  
  // 初始化GPS串口
  gpsSerial.begin(9600, SERIAL_8N1, GPS_RX, GPS_TX);
  Serial.println("GPS Serial initialized");

  // 初始化OLED
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;);
  }
  
  // 清屏
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  
  // 显示启动信息
  display.setCursor(0,0);
  display.println("GPS Tracker");
  display.println("Initializing...");
  display.display();
  delay(2000);
}

void loop() {
  // 读取GPS数据
  while (gpsSerial.available() > 0) {
    char c = gpsSerial.read();
    gps.encode(c);
  }

  display.clearDisplay();
  display.setCursor(0,0);

  if (gps.location.isValid()) {
    display.println("GPS Signal OK");
    display.print("Lat: ");
    display.println(gps.location.lat(), 6);
    display.print("Lng: ");
    display.println(gps.location.lng(), 6);
    display.print("Alt: ");
    display.print(gps.altitude.meters());
    display.println("m");
  } else {
    display.println("Waiting for GPS...");
    display.println("Please wait...");
  }

  if (gps.date.isValid() && gps.time.isValid()) {
    display.print("Time: ");
    display.print(gps.date.year());
    display.print("-");
    if (gps.date.month() < 10) display.print("0");
    display.print(gps.date.month());
    display.print("-");
    if (gps.date.day() < 10) display.print("0");
    display.print(gps.date.day());
    display.print(" ");
    if (gps.time.hour() < 10) display.print("0");
    display.print(gps.time.hour());
    display.print(":");
    if (gps.time.minute() < 10) display.print("0");
    display.print(gps.time.minute());
    display.print(":");
    if (gps.time.second() < 10) display.print("0");
    display.println(gps.time.second());
  }

  // 显示卫星数量
  display.print("Satellites: ");
  display.println(gps.satellites.value());

  display.display();
  delay(1000);
}