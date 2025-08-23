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
const unsigned long DISPLAY_TIMEOUT = 1000 * 60 * 5;  // 5分钟后关闭显示
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
const unsigned long DEBOUNCE_TIME = 50;  // 防抖时间（毫秒）

// SD卡参数
#define SD_CS 5    // SD卡CS引脚连接到GPIO5
#define SD_MOSI 23 // SD卡MOSI引脚连接到GPIO23
#define SD_MISO 19 // SD卡MISO引脚连接到GPIO19
#define SD_SCK 18  // SD卡CLK引脚连接到GPIO18
File dataFile;

const unsigned long SAVE_INTERVAL = 1000 * 60;  // 每60秒保存一次数据

// 显示更新参数
const unsigned long DISPLAY_UPDATE_INTERVAL = 1000 * 1;  // 显示每1秒更新一次

// GPS更新参数
const unsigned long GPS_UPDATE_INTERVAL = 1000 * 10;  // GPS每10秒更新一次

// 电源管理参数
const unsigned long SLEEP_CHECK_INTERVAL = 1000 * 30;  // 每30秒检查一次是否需要进入睡眠
const unsigned long INACTIVITY_TIMEOUT = 1000 * 60 * 5;  // 5分钟无活动后进入睡眠
const unsigned long SLEEP_DURATION = 1000 * 60;  // 睡眠时间60秒

// AHT10传感器参数
bool AHT10_AVAILABLE = false;  // 设置为false，因为传感器未初始化
Adafruit_AHTX0 aht;
const unsigned long AHT_UPDATE_INTERVAL = 1000 * 5;  // AHT每5秒更新一次

// RTC内存中的系统时钟参数
RTC_DATA_ATTR struct SystemTime {
  unsigned long startMillis;  // 系统启动时的毫秒数
  unsigned long lastSyncMillis;  // 上次同步时的毫秒数
  int year;
  int month;
  int day;
  int hour;
  int minute;
  int second;
  bool isSynced;  // 是否已与GPS同步
} sysTime;

// RTC内存中的其他关键变量
RTC_DATA_ATTR struct SystemState {
  unsigned long lastSaveTime;  // 上次保存数据时间
  unsigned long lastGPSUpdate;  // 上次GPS更新时间
  unsigned long lastDisplayUpdate;  // 上次显示更新时间
  unsigned long lastAHTUpdate;  // 上次AHT更新时间
  unsigned long lastSleepCheck;  // 上次睡眠检查时间
  unsigned long lastActivityTime;  // 上次活动时间
  int vibrationLevel;  // 震动等级
  float temperature;  // 温度
  float humidity;  // 湿度
  bool sdCardAvailable;  // SD卡可用状态
} sysState;

// 更新系统时间的公共函数
void updateSystemTimeValues(int newYear, int newMonth, int newDay, int newHour, int newMinute, int newSecond) {
  sysTime.year = newYear;
  sysTime.month = newMonth;
  sysTime.day = newDay;
  sysTime.hour = newHour;
  sysTime.minute = newMinute;
  sysTime.second = newSecond;
  sysTime.lastSyncMillis = millis();
  sysTime.isSynced = true;
}

// 处理时间进位和日期变更
void handleTimeOverflow() {
  // 处理秒进位
  if (sysTime.second >= 60) {
    sysTime.minute += sysTime.second / 60;
    sysTime.second %= 60;
  }
  // 处理分钟进位
  if (sysTime.minute >= 60) {
    sysTime.hour += sysTime.minute / 60;
    sysTime.minute %= 60;
  }
  // 处理小时进位
  if (sysTime.hour >= 24) {
    sysTime.day += sysTime.hour / 24;
    sysTime.hour %= 24;
  }
  
  // 处理月份天数
  int daysInMonth[] = {31,28,31,30,31,30,31,31,30,31,30,31};
  // 处理闰年
  if (sysTime.year % 4 == 0 && (sysTime.year % 100 != 0 || sysTime.year % 400 == 0)) {
    daysInMonth[1] = 29;
  }
  
  while (sysTime.day > daysInMonth[sysTime.month - 1]) {
    sysTime.day -= daysInMonth[sysTime.month - 1];
    sysTime.month++;
    if (sysTime.month > 12) {
      sysTime.month = 1;
      sysTime.year++;
      // 更新闰年
      if (sysTime.year % 4 == 0 && (sysTime.year % 100 != 0 || sysTime.year % 400 == 0)) {
        daysInMonth[1] = 29;
      } else {
        daysInMonth[1] = 28;
      }
    }
  }
}

// 初始化系统时钟
void initSystemTime() {
  // 检查是否是唤醒后的启动
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  
  if (wakeup_reason == ESP_SLEEP_WAKEUP_TIMER) {
    // 如果是定时器唤醒，更新系统时间
    unsigned long sleepDuration = SLEEP_DURATION / 1000; // 转换为秒
    sysTime.second += sleepDuration;
    handleTimeOverflow();
    Serial.println("Time updated after sleep");
  } else {
    // 如果是首次启动或非定时器唤醒，初始化时间
    sysTime.startMillis = millis();
    sysTime.lastSyncMillis = sysTime.startMillis;
    sysTime.isSynced = false;
    // 设置默认时间（0000-00-00 00:00:00）
  }
}

// 同步系统时钟到GPS时间
void syncTimeWithGPS() {
  if (gps.date.isValid() && gps.time.isValid()) {
    // 校验时间有效性
    if (gps.date.year() < 2025 || gps.date.year() > 2035) {
      Serial.println("Invalid year detected: " + String(gps.date.year()) + ", skipping sync");
      return;
    }
    if (gps.date.month() < 1 || gps.date.month() > 12) {
      Serial.println("Invalid month detected: " + String(gps.date.month()) + ", skipping sync");
      return;
    }
    if (gps.date.day() < 1 || gps.date.day() > 31) {
      Serial.println("Invalid day detected: " + String(gps.date.day()) + ", skipping sync");
      return;
    }
    if (gps.time.hour() < 0 || gps.time.hour() > 23) {
      Serial.println("Invalid hour detected: " + String(gps.time.hour()) + ", skipping sync");
      return;
    }
    if (gps.time.minute() < 0 || gps.time.minute() > 59) {
      Serial.println("Invalid minute detected: " + String(gps.time.minute()) + ", skipping sync");
      return;
    }
    if (gps.time.second() < 0 || gps.time.second() > 59) {
      Serial.println("Invalid second detected: " + String(gps.time.second()) + ", skipping sync");
      return;
    }

    updateSystemTimeValues(
      gps.date.year(),
      gps.date.month(),
      gps.date.day(),
      gps.time.hour(),
      gps.time.minute(),
      gps.time.second()
    );
    Serial.println("Time synchronized with GPS");
  }
}

// 更新系统时钟
void updateSystemTime() {

  unsigned long currentMillis = millis();
  
  // 计算实际经过的毫秒数
  unsigned long elapsedMillis = currentMillis - sysTime.lastSyncMillis;
  
  // 转换为秒数
  int elapsedSeconds = elapsedMillis / 1000;
  
  if (elapsedSeconds > 0) {
    // 更新当前系统时间
    sysTime.lastSyncMillis = currentMillis;

    // 更新秒数
    sysTime.second += elapsedSeconds;
    
    // 处理时间进位
    handleTimeOverflow();

    // 调试信息
    // if (sysTime.second % 10 == 0) {
    //   Serial.println("Update time: " + getCurrentTimeString());
    // }
  }
}

// 获取当前时间字符串
String getCurrentTimeString() {
  char timeStr[30];
  sprintf(timeStr, "%04d-%02d-%02d %02d:%02d:%02d", 
          sysTime.year, sysTime.month, sysTime.day,
          sysTime.hour, sysTime.minute, sysTime.second);
  return String(timeStr);
}

// 检查GPS数据是否有效
bool isGPSDataValid() {
  return (gps.location.isValid() && gps.date.isValid() && gps.time.isValid());
}

// 更新GPS数据
void updateGPSData() {
  while (gpsSerial.available() > 0) {
    char c = gpsSerial.read();
    gps.encode(c);
  }
}

// 初始化系统状态
void initSystemState() {
  // 检查是否是唤醒后的启动
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  
  if (wakeup_reason == ESP_SLEEP_WAKEUP_TIMER) {
    // 如果是定时器唤醒，更新系统时间
    unsigned long sleepDuration = SLEEP_DURATION / 1000; // 转换为秒
    sysTime.second += sleepDuration;
    handleTimeOverflow();
    Serial.println("Time updated after sleep");
  } else {
    // 如果是首次启动或非定时器唤醒，初始化所有状态
    sysState.lastSaveTime = 0;
    sysState.lastGPSUpdate = 0;
    sysState.lastDisplayUpdate = 0;
    sysState.lastAHTUpdate = 0;
    sysState.lastSleepCheck = 0;
    sysState.lastActivityTime = 0;
    sysState.vibrationLevel = 0;
    sysState.temperature = 0;
    sysState.humidity = 0;
    sysState.sdCardAvailable = false;  // 初始化为false，等待SD卡初始化
  }
}

void setup() {
  Serial.begin(115200);
  
  // 初始化系统时钟
  initSystemTime();

  // 初始化系统参数
  initSystemState();
  
  // 检查是否是唤醒后的启动
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  
  if (wakeup_reason == ESP_SLEEP_WAKEUP_TIMER) {
    // 如果是定时器唤醒，立即采集并保存数据（浅度睡眠保持外设状态）
    Serial.println("Wakeup by timer at " + getCurrentTimeString());

    // 读取GPS数据
    while (gpsSerial.available() > 0) {
      char c = gpsSerial.read();
      gps.encode(c);
    }
    
    // 读取温湿度数据
    if (AHT10_AVAILABLE) {
      sensors_event_t humidity_event, temp_event;
      if (aht.getEvent(&humidity_event, &temp_event)) {
        sysState.temperature = temp_event.temperature;
        sysState.humidity = humidity_event.relative_humidity;
      }
    }
    
    // 保存数据
    saveData();
    
    // 重新进入睡眠
    Serial.println("Entering light sleep...");
    esp_sleep_enable_timer_wakeup(SLEEP_DURATION * 1000);
    esp_sleep_enable_ext0_wakeup((gpio_num_t)VIBRATION_PIN, HIGH);
    esp_light_sleep_start();
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
  if(SD.begin(SD_CS)) {
    Serial.println("SD Card initialized successfully.");
    sysState.sdCardAvailable = true;
  } else {
    Serial.println("SD Card initialization failed after multiple attempts!");
    sysState.sdCardAvailable = false;
  }
  
  // 初始化OLED
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for(;;);
  }

  // 初始化AHT10传感器
  if (aht.begin()) {
    Serial.println("AHT10 sensor initialized");
    AHT10_AVAILABLE = true;
  } else {
    Serial.println("Could not find AHT10 sensor!");
    AHT10_AVAILABLE = false;
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
  sysState.lastActivityTime = millis();
}

// 检查震动
void checkVibration() {
  int vibrationValue = digitalRead(VIBRATION_PIN);
  unsigned long currentTime = millis();
  
  // 检测到震动（HIGH表示检测到震动）
  if (vibrationValue == HIGH) {
    if (currentTime - lastVibrationTime > DEBOUNCE_TIME) {  // 防抖
      vibrationCount++;
      lastVibrationTime = currentTime;
      sysState.lastActivityTime = currentTime;  // 更新活动时间
    }
  }
}

// 保存数据
void saveData() {

  // 保存数据不在打印未符合条件的日志
  unsigned long currentTime = millis();
  unsigned long timeSinceLastSave = currentTime - sysState.lastSaveTime;
  
  if (timeSinceLastSave < SAVE_INTERVAL) {
    return;
  }

  // 在保存数据前更新震动等级
  sysState.vibrationLevel = vibrationCount;
  vibrationCount = 0;  // 重置计数
  Serial.println("Vibration level: " + String(sysState.vibrationLevel));

  Serial.println("Saving data...");
  
  // 检查SD卡
  if (!sysState.sdCardAvailable) {
    Serial.println("SD Card not available, skipping save");
    return;
  }

  // 检查日期年份, 小于2025年不保存, 大于2035年不保存
  if (sysTime.year < 2025 || sysTime.year > 2035) {
    Serial.println("Invalid year detected: " + String(sysTime.year) + ", skipping save");
    return;
  }

  // 构建文件名 data_20250529.csv, 0年补位0000, 5月补位05, 1日补位01, 使用sprintf格式化
  String fileName = "/data_";
  // 年份补位0000
  char yearStr[5];
  sprintf(yearStr, "%04d", sysTime.year);
  fileName += String(yearStr);
  // 月份补位05
  char monthStr[3];
  sprintf(monthStr, "%02d", sysTime.month);
  fileName += String(monthStr);
  // 日期补位01
  char dayStr[3];
  sprintf(dayStr, "%02d", sysTime.day);
  fileName += String(dayStr);
  // 文件名后缀.csv
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
  
  // 构建CSV行
  String dataString = getCurrentTimeString() + ",";
  
  // 位置数据
  if (isGPSDataValid()) {
    dataString += String(gps.location.lat(), 6) + "," +
                 String(gps.location.lng(), 6) + "," +
                 String(gps.altitude.meters()) + ",";
  } else {
    dataString += "N/A,N/A,N/A,";
  }

  // 卫星数量
  dataString += String(gps.satellites.value()) + ",";

  // 震动等级
  dataString += String(sysState.vibrationLevel) + ",";

  // 温湿度
  dataString += String(sysState.temperature) + "," +
               String(sysState.humidity);
  
  // 写入数据
  if (dataFile.println(dataString)) {
    Serial.println("Data written successfully at " + getCurrentTimeString());
    sysState.lastSaveTime = currentTime;  // 只有在成功保存数据后才更新lastSaveTime
  } else {
    Serial.println("Error writing data!");
  }
  
  // 确保文件被正确关闭
  dataFile.close();
}

// 检查睡眠
void checkSleep() {
  unsigned long currentTime = millis();
  if (currentTime - sysState.lastSleepCheck >= SLEEP_CHECK_INTERVAL) {
    sysState.lastSleepCheck = currentTime;
    
    // 检查是否超过无活动时间
    if (currentTime - sysState.lastActivityTime >= INACTIVITY_TIMEOUT) {
      
      // 配置定时器唤醒
      esp_sleep_enable_timer_wakeup(SLEEP_DURATION * 1000);
      
      // 配置震动传感器唤醒
      esp_sleep_enable_ext0_wakeup((gpio_num_t)VIBRATION_PIN, HIGH);
      
      // 进入浅度睡眠（保持外设状态，包括SD卡连接）
      esp_light_sleep_start();
    }
  }
}

void loop() {
  unsigned long currentTime = millis();
  
  // 检查显示超时
  if (displayActive && (currentTime - displayStartTime >= DISPLAY_TIMEOUT)) {
    display.clearDisplay();
    display.display();
    display.ssd1306_command(SSD1306_DISPLAYOFF);
    displayActive = false;
  }

  // 读取GPS数据（降低频率）
  if (currentTime - sysState.lastGPSUpdate >= GPS_UPDATE_INTERVAL) {
    sysState.lastGPSUpdate = currentTime;
    updateGPSData();
    
    // 调试GPS状态
    if (isGPSDataValid()) {
      Serial.println("Update GPS Status: Valid: " + String(isGPSDataValid()) + ", Satellites: " + String(gps.satellites.value()) + ", Altitude: " + String(gps.altitude.meters()) + ", Latitude: " + String(gps.location.lat(), 6) + ", Longitude: " + String(gps.location.lng(), 6));
    } else {
      Serial.println("Update GPS Status: Valid: " + String(isGPSDataValid()) + ", Satellites: " + String(gps.satellites.value()) + ", Altitude: " + String(gps.altitude.meters()) + ", Latitude: N/A, Longitude: N/A");
    }
    
    // 尝试同步时间
    if (!sysTime.isSynced || (currentTime - sysTime.lastSyncMillis > 3600000)) {  // 每小时尝试同步一次
      syncTimeWithGPS();
    }
  }
  
  // 读取温湿度数据(降低频率)
  if (AHT10_AVAILABLE && currentTime - sysState.lastAHTUpdate >= AHT_UPDATE_INTERVAL) {
    sysState.lastAHTUpdate = currentTime;
    sensors_event_t humidity_event, temp_event;
    if (aht.getEvent(&humidity_event, &temp_event)) {
      Serial.println("Update AHT10 Status: Temperature: " + String(temp_event.temperature) + "C, Humidity: " + String(humidity_event.relative_humidity) + "%");
      sysState.temperature = temp_event.temperature;
      sysState.humidity = humidity_event.relative_humidity;
    }
  }
  
  // 检查震动
  checkVibration();

  // 更新系统时钟 - 每次循环都更新
  updateSystemTime();
  
  // 定期保存数据到SD卡
  saveData();
  
  // 检查是否需要进入睡眠模式
  checkSleep();

  // 只在显示激活时更新显示
  if (displayActive && currentTime - sysState.lastDisplayUpdate >= DISPLAY_UPDATE_INTERVAL) {
    sysState.lastDisplayUpdate = currentTime;
    
    display.clearDisplay();
    display.setCursor(0,0);
    
    // 显示当前时间
    display.println(getCurrentTimeString());
    if (!sysTime.isSynced) {
      display.println("(Not synced with GPS)");
    }
    
    // 显示温湿度
    display.print("T: ");
    display.print(sysState.temperature, 1);
    display.print("C  ");
    display.print("H: ");
    display.print(sysState.humidity, 1);
    display.println("%");

    // 显示震动等级
    display.print("Vibration: ");
    display.println(sysState.vibrationLevel);

    // 显示GPS状态
    display.print("GPS: ");
    display.println(isGPSDataValid() ? "OK" : "Searching");
    display.print("Sats: ");
    display.println(gps.satellites.value());

    if (isGPSDataValid()) {
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

    display.display();
  }
}