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

// 震动传感器参数
#define VIBRATION_PIN 34  // 震动传感器DO引脚连接到GPIO34
#define VIBRATION_THRESHOLD 3  // 震动检测阈值
int vibrationCount = 0;  // 震动计数
unsigned long lastVibrationTime = 0;  // 上次震动时间
int vibrationLevel = 0;  // 震动等级

void setup() {
  Serial.begin(115200);
  Serial.println("Starting GPS Test...");
  
  // 初始化GPS串口
  gpsSerial.begin(9600, SERIAL_8N1, GPS_RX, GPS_TX);
  Serial.println("GPS Serial initialized");

  // 初始化震动传感器
  pinMode(VIBRATION_PIN, INPUT);
  
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

void checkVibration() {
  int vibrationValue = digitalRead(VIBRATION_PIN);
  unsigned long currentTime = millis();
  
  // 检测到震动
  if (vibrationValue == LOW) {  // 震动传感器在检测到震动时输出LOW
    if (currentTime - lastVibrationTime > 100) {  // 防抖
      vibrationCount++;
      lastVibrationTime = currentTime;
    }
  }
  
  // 每5秒更新一次震动等级
  static unsigned long lastLevelUpdate = 0;
  if (currentTime - lastLevelUpdate > 5000) {
    vibrationLevel = vibrationCount;
    vibrationCount = 0;
    lastLevelUpdate = currentTime;
  }
}

void loop() {
  // 读取GPS数据
  while (gpsSerial.available() > 0) {
    char c = gpsSerial.read();
    gps.encode(c);
  }

  // 检查震动
  checkVibration();

  display.clearDisplay();
  display.setCursor(0,0);

  // 显示震动等级
  display.print("Vibration: ");
  display.println(vibrationLevel);

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