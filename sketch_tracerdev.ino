#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <TinyGPS++.h>
#include <SD.h>
#include <SPI.h>
#include <esp_sleep.h>
#include <Adafruit_AHTX0.h>  // AHT10传感器库

// OLED参数
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// 显示超时参数
const unsigned long DISPLAY_TIMEOUT = 300000;  // 5分钟后关闭显示
unsigned long displayStartTime = 0;  // 显示开始时间
bool displayActive = true;  // 显示状态标志

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
const unsigned long DEBOUNCE_TIME = 50;  // 防抖时间（毫秒）
const unsigned long LEVEL_UPDATE_INTERVAL = 2000;  // 震动等级更新间隔（毫秒）

// SD卡参数
#define SD_CS 5    // SD卡CS引脚连接到GPIO5
#define SD_MOSI 23 // SD卡MOSI引脚连接到GPIO23
#define SD_MISO 19 // SD卡MISO引脚连接到GPIO19
#define SD_SCK 18  // SD卡CLK引脚连接到GPIO18
File dataFile;
bool sdCardAvailable = false;

const unsigned long SAVE_INTERVAL = 60000;  // 每10秒保存一次数据
unsigned long lastSaveTime = 0;

// 显示更新参数
const unsigned long DISPLAY_UPDATE_INTERVAL = 5000;  // 显示每5秒更新一次
unsigned long lastDisplayUpdate = 0;  // 上次显示更新时间

// GPS更新参数
const unsigned long GPS_UPDATE_INTERVAL = 5000;  // GPS每5秒更新一次
unsigned long lastGPSUpdate = 0;  // 上次GPS更新时间

// 电源管理参数
const unsigned long SLEEP_CHECK_INTERVAL = 30000;  // 每30秒检查一次是否需要进入睡眠
unsigned long lastSleepCheck = 0;  // 上次睡眠检查时间
const unsigned long INACTIVITY_TIMEOUT = 300000;  // 5分钟无活动后进入睡眠
unsigned long lastActivityTime = 0;  // 上次活动时间

// AHT10传感器参数
Adafruit_AHTX0 aht;
float temperature = 0;
float humidity = 0;

void setup() {
  Serial.begin(115200);
  
  // 初始化GPS串口
  gpsSerial.begin(9600, SERIAL_8N1, GPS_RX, GPS_TX);
  Serial.println("GPS Serial initialized");

  // 初始化震动传感器
  pinMode(VIBRATION_PIN, INPUT);
  
  // 初始化SD卡
  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  if(!SD.begin(SD_CS)) {
    Serial.println("SD Card initialization failed!");
    sdCardAvailable = false;
  } else {
    Serial.println("SD Card initialized.");
    sdCardAvailable = true;
  }
  
  // 初始化OLED
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;);
  }

  // 初始化AHT10传感器
  if (!aht.begin()) {
    Serial.println("Could not find AHT10 sensor!");
  } else {
    Serial.println("AHT10 sensor initialized");
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
  
  // 记录显示开始时间
  displayStartTime = millis();
  displayActive = true;

  // 初始化活动时间
  lastActivityTime = millis();
}

void checkVibration() {
  int vibrationValue = digitalRead(VIBRATION_PIN);
  unsigned long currentTime = millis();
  
  // 检测到震动（HIGH表示检测到震动）
  if (vibrationValue == HIGH) {
    if (currentTime - lastVibrationTime > DEBOUNCE_TIME) {  // 防抖
      vibrationCount++;
      lastVibrationTime = currentTime;
      lastActivityTime = currentTime;  // 更新活动时间
    }
  }
  
  // 定期更新震动等级
  static unsigned long lastLevelUpdate = 0;
  if (currentTime - lastLevelUpdate > LEVEL_UPDATE_INTERVAL) {
    vibrationLevel = vibrationCount;
    vibrationCount = 0;  // 重置计数
    lastLevelUpdate = currentTime;
  }
}

void saveData() {
  if (!sdCardAvailable || !gps.location.isValid()) return;
  
  unsigned long currentTime = millis();
  if (currentTime - lastSaveTime < SAVE_INTERVAL) return;
  lastSaveTime = currentTime;
  
  String fileName = "/data_" + String(gps.date.year()) + 
                   String(gps.date.month()) + 
                   String(gps.date.day()) + ".csv";
  
  // 如果文件不存在，创建文件并写入表头
  if(!SD.exists(fileName)) {
    dataFile = SD.open(fileName, FILE_WRITE);
    if(dataFile) {
      dataFile.println("Time,Latitude,Longitude,Altitude,Satellites,Vibration,Temperature,Humidity");
      dataFile.close();
    }
  }
  
  dataFile = SD.open(fileName, FILE_APPEND);
  if (dataFile) {
    // 构建CSV行
    String dataString = "";
    
    // 时间
    if (gps.time.isValid()) {
      dataString += String(gps.time.hour()) + ":" +
                   String(gps.time.minute()) + ":" +
                   String(gps.time.second()) + ",";
    } else {
      dataString += "N/A,";
    }
    
    // 位置数据
    dataString += String(gps.location.lat(), 6) + "," +
                 String(gps.location.lng(), 6) + "," +
                 String(gps.altitude.meters()) + "," +
                 String(gps.satellites.value()) + "," +
                 String(vibrationLevel) + "," +
                 String(temperature) + "," +
                 String(humidity);
    
    dataFile.println(dataString);
    dataFile.close();
  }
}

void checkSleep() {
  unsigned long currentTime = millis();
  if (currentTime - lastSleepCheck >= SLEEP_CHECK_INTERVAL) {
    lastSleepCheck = currentTime;
    
    // 检查是否超过无活动时间
    if (currentTime - lastActivityTime >= INACTIVITY_TIMEOUT) {
      // 准备进入深度睡眠
      display.clearDisplay();
      display.setCursor(0,0);
      display.println("Entering sleep mode...");
      display.display();
      delay(1000);
      
      // 关闭不需要的外设
      display.clearDisplay();
      display.display();
      display.ssd1306_command(SSD1306_DISPLAYOFF);
      
      // 配置唤醒源（震动传感器）
      esp_sleep_enable_ext0_wakeup((gpio_num_t)VIBRATION_PIN, HIGH);
      
      // 进入深度睡眠
      esp_deep_sleep_start();
    }
  }
}

void loop() {
  // 检查显示超时
  if (displayActive && (millis() - displayStartTime >= DISPLAY_TIMEOUT)) {
    display.clearDisplay();
    display.display();
    display.ssd1306_command(SSD1306_DISPLAYOFF);
    displayActive = false;
  }

  // 读取GPS数据（降低频率）
  unsigned long currentTime = millis();
  if (currentTime - lastGPSUpdate >= GPS_UPDATE_INTERVAL) {
    lastGPSUpdate = currentTime;
    while (gpsSerial.available() > 0) {
      char c = gpsSerial.read();
      gps.encode(c);
    }
  }

  // 检查震动
  checkVibration();
  
  // 检查是否需要进入睡眠模式
  checkSleep();

  // 定期保存数据到SD卡
  if (currentTime - lastSaveTime >= SAVE_INTERVAL) {
    lastSaveTime = currentTime;

    // 更新温湿度数据
    sensors_event_t humidity_event, temp_event;
    if (aht.getEvent(&humidity_event, &temp_event)) {
      temperature = temp_event.temperature;
      humidity = humidity_event.relative_humidity;
    }
    
    // 保存数据到SD卡
    saveData();
  }

  // 只在显示激活时更新显示
  if (displayActive && currentTime - lastDisplayUpdate >= DISPLAY_UPDATE_INTERVAL) {
    lastDisplayUpdate = currentTime;
    
    display.clearDisplay();
    display.setCursor(0,0);

    // 显示温湿度
    display.print("T: ");
    display.print(temperature, 1);
    display.print("°C");
    display.print("H: ");
    display.print(humidity, 1);
    display.println("%");

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
  }
}