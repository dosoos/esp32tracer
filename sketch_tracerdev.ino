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

const unsigned long SAVE_INTERVAL = 60000;  // 每60秒保存一次数据
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
const unsigned long SLEEP_DURATION = 60000000;  // 睡眠时间60秒（微秒）

// AHT10传感器参数
#define ENABLE_AHT10 false  // 设置为false，因为传感器未连接
Adafruit_AHTX0 aht;
float temperature = 0;
float humidity = 0;

void setup() {
  Serial.begin(115200);
  
  // 检查是否是唤醒后的启动
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  
  if (wakeup_reason == ESP_SLEEP_WAKEUP_TIMER) {
    // 如果是定时器唤醒，立即采集并保存数据
    while (gpsSerial.available() > 0) {
      char c = gpsSerial.read();
      gps.encode(c);
    }
    
    if (ENABLE_AHT10) {
      sensors_event_t humidity_event, temp_event;
      if (aht.getEvent(&humidity_event, &temp_event)) {
        temperature = temp_event.temperature;
        humidity = humidity_event.relative_humidity;
      }
    }
    
    saveData();
    
    // 重新进入睡眠
    esp_sleep_enable_timer_wakeup(SLEEP_DURATION);
    esp_sleep_enable_ext0_wakeup((gpio_num_t)VIBRATION_PIN, HIGH);
    esp_deep_sleep_start();
  }
  
  // 初始化GPS串口
  gpsSerial.begin(9600, SERIAL_8N1, GPS_RX, GPS_TX);
  Serial.println("GPS Serial initialized");

  // 初始化震动传感器
  pinMode(VIBRATION_PIN, INPUT);
  
  // 初始化SD卡
  Serial.println("Initializing SD card...");

  // 配置SPI
  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  
  // 初始化SD卡
  if(!SD.begin(SD_CS)) {
    Serial.println("SD Card initialization failed after multiple attempts!");
    Serial.println("Please check:");
    Serial.println("1. SD card is properly inserted");
    Serial.println("2. All connections are secure");
    Serial.println("3. SD card module is powered (3.3V)");
    Serial.println("4. SD card is formatted as FAT32");
    sdCardAvailable = false;
  } else {
    Serial.println("SD Card initialized successfully.");
    sdCardAvailable = true;
  }
  
  // 初始化OLED
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;);
  }

  // 初始化AHT10传感器（仅在启用时）
  if (ENABLE_AHT10) {
    if (!aht.begin()) {
      Serial.println("Could not find AHT10 sensor!");
    } else {
      Serial.println("AHT10 sensor initialized");
    }
  } else {
    Serial.println("AHT10 sensor disabled");
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
  Serial.println("Saving data...");
  Serial.println("SD Card available: " + String(sdCardAvailable));
  Serial.println("GPS Status:");
  Serial.println("- Location valid: " + String(gps.location.isValid()));
  Serial.println("- Date valid: " + String(gps.date.isValid()));
  Serial.println("- Time valid: " + String(gps.time.isValid()));
  Serial.println("- Satellites: " + String(gps.satellites.value()));
  if (gps.location.isValid()) {
    Serial.println("- Latitude: " + String(gps.location.lat(), 6));
    Serial.println("- Longitude: " + String(gps.location.lng(), 6));
  }
  
  // 修改保存条件：只要有SD卡就保存，即使GPS数据不完整
  if (!sdCardAvailable) {
    Serial.println("SD Card not available, skipping save");
    return;
  }

  unsigned long currentTime = millis();
  if (currentTime - lastSaveTime < SAVE_INTERVAL) {
    Serial.println("Save interval not reached, " + String(currentTime - lastSaveTime) + "ms");
    return;
  }
  lastSaveTime = currentTime;
  
  // 构建文件名
  String fileName = "/data_";
  if (gps.date.isValid()) {
    fileName += String(gps.date.year()) + 
               String(gps.date.month()) + 
               String(gps.date.day());
  } else {
    fileName += "unknown";
  }
  fileName += ".csv";
  
  Serial.println("Attempting to save to file: " + fileName);
  
  // 如果文件不存在，创建文件并写入表头
  if(!SD.exists(fileName)) {
    Serial.println("File does not exist, creating file...");
    dataFile = SD.open(fileName, FILE_WRITE);
    if(dataFile) {
      Serial.println("File created successfully");
      dataFile.println("Time,Latitude,Longitude,Altitude,Satellites,Vibration,Temperature,Humidity");
      dataFile.close();
    } else {
      Serial.println("Error creating file!");
      return;
    }
  }
  
  // 打开文件进行追加
  dataFile = SD.open(fileName, FILE_APPEND);
  if (!dataFile) {
    Serial.println("Error opening file for append!");
    return;
  }
  
  Serial.println("File opened successfully");
  
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
  if (gps.location.isValid()) {
    dataString += String(gps.location.lat(), 6) + "," +
                 String(gps.location.lng(), 6) + "," +
                 String(gps.altitude.meters()) + ",";
  } else {
    dataString += "N/A,N/A,N/A,";
  }

  // 卫星数量
  dataString += String(gps.satellites.value()) + ",";

  // 震动等级
  dataString += String(vibrationLevel) + ",";

  // 温湿度
  dataString += String(temperature) + "," +
               String(humidity);
  
  // 写入数据
  if (dataFile.println(dataString)) {
    Serial.println("Data written successfully");
  } else {
    Serial.println("Error writing data!");
  }
  
  // 确保文件被正确关闭
  dataFile.close();
  Serial.println("File closed");
}

void checkSleep() {
  unsigned long currentTime = millis();
  if (currentTime - lastSleepCheck >= SLEEP_CHECK_INTERVAL) {
    lastSleepCheck = currentTime;
    
    // 检查是否超过无活动时间
    if (currentTime - lastActivityTime >= INACTIVITY_TIMEOUT) {
      
      // 配置定时器唤醒
      esp_sleep_enable_timer_wakeup(SLEEP_DURATION);
      
      // 配置震动传感器唤醒
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

  // 定期保存数据到SD卡
  if (currentTime - lastSaveTime >= SAVE_INTERVAL) {
    lastSaveTime = currentTime;

    // 更新温湿度数据
    if (ENABLE_AHT10) {
      sensors_event_t humidity_event, temp_event;
      if (aht.getEvent(&humidity_event, &temp_event)) {
        temperature = temp_event.temperature;
        humidity = humidity_event.relative_humidity;
      }
    }
    
    // 保存数据到SD卡
    saveData();
  }
  
  // 检查是否需要进入睡眠模式
  checkSleep();

  // 只在显示激活时更新显示
  if (displayActive && currentTime - lastDisplayUpdate >= DISPLAY_UPDATE_INTERVAL) {
    lastDisplayUpdate = currentTime;
    
    display.clearDisplay();
    display.setCursor(0,0);

    // 显示温湿度
    display.print("T: ");
    display.print(temperature, 1);
    display.print("C  ");
    display.print("H: ");
    display.print(humidity, 1);
    display.println("%");

    // 显示震动等级
    display.print("Vibration: ");
    display.println(vibrationLevel);

    // 显示GPS状态
    display.print("GPS: ");
    display.println(gps.location.isValid() ? "OK" : "Searching");
    display.print("Sats: ");
    display.println(gps.satellites.value());

    if (gps.location.isValid()) {
      // 显示纬度
      display.print("Lat: ");
      display.println(gps.location.lat(), 6);

      // 显示经度
      display.print("Lng: ");
      display.println(gps.location.lng(), 6);

      // 显示海拔高度
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

    display.display();
  }
}