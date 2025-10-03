// Blinker mode definition (must be before including Blinker.h)
#define BLINKER_WIFI
#define BLINKER_PRINT Serial

#include <WiFi.h>
#include <WiFiManager.h>   // WiFi配网管理库
#include <Preferences.h>   // ESP32 NVS存储库
#include <Blinker.h>
#include "driver/ledc.h"
#include "esp_task_wdt.h"  // Add watchdog timer header
#include "esp_wifi.h"      // Add WiFi advanced control
#include "esp_sleep.h"     // ESP32深度睡眠库
#include <U8g2lib.h>       // U8g2图形库，支持多种单色显示屏
#include <SPI.h>           // SPI通信库 - LCD9648使用SPI接口

// WiFi Configuration (使用WiFiManager进行配网)
// const char* ssid = "CU_204";        // 注释掉硬编码的WiFi配置
// const char* password = "wj990518."; // 注释掉硬编码的WiFi配置

// WiFiManager实例
WiFiManager wifiManager;
Preferences preferences;

// Blinker Configuration
const char* auth = "88897b5b6069";  // 请替换为您的Blinker设备密钥

// WiFi配网相关变量
bool shouldSaveConfig = false;
bool wifiConfigMode = false;
const int CONFIG_BUTTON_PIN = 0;  // 使用Boot按钮作为配网按钮

// Network optimization variables
unsigned long lastWiFiCheck = 0;
unsigned long lastBlinkerUpdate = 0;
const unsigned long WIFI_CHECK_INTERVAL = 10000;    // Check WiFi every 10 seconds
const unsigned long BLINKER_UPDATE_INTERVAL = 500;  // Update Blinker every 500ms (reduced from frequent updates)
int wifiReconnectAttempts = 0;
const int MAX_WIFI_RECONNECT_ATTEMPTS = 3;
bool networkStable = false;
int lastRSSI = 0;

// ESP32-S3 optimized pin definitions (avoid internal conflict pins)
#define MOTOR_IN1         1   // GPIO1 - Left motor direction 1
#define MOTOR_IN2         2   // GPIO2 - Left motor direction 2
#define MOTOR_IN3         42  // GPIO42 - Right motor direction 1
#define MOTOR_IN4         41  // GPIO41 - Right motor direction 2
#define MOTOR_ENA         3   // GPIO3 - Left motor enable (PWM)
#define MOTOR_ENB         40  // GPIO40 - Right motor enable (PWM)
#define ULTRASONIC_TRIG   4   // GPIO4 - Ultrasonic trigger
#define ULTRASONIC_ECHO   5   // GPIO5 - Ultrasonic echo
#define SERVO_PIN         6   // GPIO6 - Servo control (PWM)
#define LED_PIN           48  // GPIO48 - Status LED
// #define BUZZER_PIN        7   // GPIO7 - Buzzer

// LCD9648 (ST7565) 显示屏引脚定义 - SPI接口
#define LCD_RST           14  // GPIO8 - LCD复位信号 (Reset)
#define LCD_CS            13   // GPIO7 - LCD片选信号 (Chip Select)
#define LCD_RS            12   // GPIO9 - LCD数据/命令选择 (A0/DC)           10  // GPIO10 - LCD时钟信号 (Serial Clock)
#define LCD_SDA           11  // GPIO11 - LCD数据信号 (Serial Data)
#define LCD_SCL           10  // GPIO10 - LCD时钟信号 (Serial Clock)


// Global variables
int motorSpeed = 200;        // Motor speed (50-255)
int speedPercent = 78;       // Speed percentage (20-100%)
unsigned long lastWebUpdate = 0;
unsigned long lastDistanceCheck = 0;
const float SAFE_DISTANCE = 10.0;  // Reduced safe distance from 15cm to 10cm for better mobility

// Sensor optimization variables
float cachedDistance = 999.0;
unsigned long lastSensorRead = 0;
const unsigned long SENSOR_READ_INTERVAL = 200;  // Read sensor every 200ms instead of every call

// LCD9648 (ST7565) 显示屏对象和变量
U8G2_ST7565_ERC12864_ALT_F_4W_SW_SPI u8g2(U8G2_R0, LCD_SCL, LCD_SDA, LCD_CS, LCD_RS, LCD_RST);
unsigned long lastLCDUpdate = 0;
const unsigned long LCD_UPDATE_INTERVAL = 500;  // 提高LCD更新频率：1秒->0.5秒，增强实时性
bool lcdInitialized = false;

// Blinker button definitions
BlinkerButton ButtonF("btn-f");      // 前进按钮
BlinkerButton ButtonB("btn-b");      // 后退按钮
BlinkerButton ButtonL("btn-l");      // 左转按钮
BlinkerButton ButtonR("btn-r");      // 右转按钮
BlinkerButton ButtonSleep("btn-sleep"); // 睡眠开关按钮
// BlinkerButton ButtonS("btn-s");      // 停止按钮 - 已移除
// BlinkerButton ButtonAuto("btn-auto"); // 自动模式按钮 - 已移除
BlinkerSlider SliderSpeed("slider-speed"); // 速度滑块
BlinkerSlider SliderServo("slider-servo"); // 舵机角度滑块
BlinkerNumber NumberDistance("distance");  // 距离显示
BlinkerNumber NumberSpeed("speed");        // 速度显示
BlinkerText TextIP("ip");                  // IP地址显示

// Web server (commented out due to AsyncTCP conflict with Blinker)
// AsyncWebServer server(80);

// WiFi management functions for network stability
bool checkWiFiConnection() {
  if (WiFi.status() != WL_CONNECTED) {
    // Serial.println("WiFi disconnected, attempting reconnection...");
    networkStable = false;
    return false;
  }
  
  // Check signal strength
  int currentRSSI = WiFi.RSSI();
  if (currentRSSI != lastRSSI) {
    // Serial.printf("WiFi RSSI: %d dBm\n", currentRSSI);
    lastRSSI = currentRSSI;
  }
  
  // Consider connection unstable if signal is very weak
  if (currentRSSI < -80) {
    // Serial.println("Warning: Weak WiFi signal detected");
    networkStable = false;
  } else {
    networkStable = true;
  }
  
  return true;
}

void reconnectWiFi() {
  if (wifiReconnectAttempts >= MAX_WIFI_RECONNECT_ATTEMPTS) {
    Serial.println("Max WiFi reconnection attempts reached, restarting system...");
    systemRestart("WiFi connection failed");
    return;
  }
  
  wifiReconnectAttempts++;
  // Serial.printf("WiFi reconnection attempt %d/%d\n", wifiReconnectAttempts, MAX_WIFI_RECONNECT_ATTEMPTS);
  
  // 使用WiFiManager重新连接
  WiFi.disconnect();
  delay(1000);
  
  // 尝试使用保存的凭据重新连接
  preferences.begin("wifi-config", true);
  String savedSSID = preferences.getString("ssid", "");
  String savedPassword = preferences.getString("password", "");
  preferences.end();
  
  if (savedSSID.length() > 0 && savedPassword.length() > 0) {
    WiFi.begin(savedSSID.c_str(), savedPassword.c_str());
    
    // Wait for connection with timeout
    int timeout = 0;
    while (WiFi.status() != WL_CONNECTED && timeout < 20) {
      delay(500);
      // Serial.print(".");
      timeout++;
      esp_task_wdt_reset();
    }
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    // Serial.println("\nWiFi reconnected successfully!");
    wifiReconnectAttempts = 0;  // Reset counter on successful connection
    networkStable = true;
  } else {
    // Serial.println("\nWiFi reconnection failed");
    networkStable = false;
  }
}

// System restart function
void systemRestart(const char* reason) {
  Serial.printf("%s, system will restart in 3 seconds...\n", reason);
  delay(1000);
  // Blinker.notify(String(reason) + ", system will restart");
  delay(2000);
  ESP.restart();
}

// LCD显示用的距离缓存变量
float lastValidDistance = 0.0;
unsigned long lastValidDistanceTime = 0;
const unsigned long DISTANCE_CACHE_TIMEOUT = 1500; // 缩短缓存超时：3秒->1.5秒，增强实时性

// LCD9648显示功能函数
void updateLCDDisplay() {
  if (!lcdInitialized) return;
  
  u8g2.clearBuffer();
  
  // 使用更粗的字体以增加浓度和可读性
  u8g2.setFont(u8g2_font_6x10_tf);  // 使用粗体字体
  
  // 第一行：系统状态
  String wifiStatus = WiFi.status() == WL_CONNECTED ? "WiFi:OK" : "WiFi:--";
  String networkStatus = networkStable ? " Net:OK" : " Net:--";
  u8g2.drawStr(0, 9, (wifiStatus + networkStatus).c_str());
  
  // 第二行：距离信息 - 使用智能缓存机制
  float currentDistance = getDistance();
  String distanceStr;
  
  // 调试输出
  // Serial.print("LCD显示距离: ");
  // Serial.println(currentDistance);
  
  // 智能缓存逻辑：如果当前读数有效，使用并缓存；如果无效但缓存未过期，使用缓存
  if (currentDistance >= 2.0 && currentDistance < 600.0) {
    // 当前读数有效，更新缓存
    lastValidDistance = currentDistance;
    lastValidDistanceTime = millis();
    distanceStr = "Dist:" + String(currentDistance, 1) + "cm";
  } else {
    // 当前读数无效，检查缓存
    unsigned long currentTime = millis();
    if (lastValidDistance > 0 && (currentTime - lastValidDistanceTime) < DISTANCE_CACHE_TIMEOUT) {
      // 使用缓存的有效距离
      distanceStr = "Dist:" + String(lastValidDistance, 1) + "cm";
      // Serial.print("使用缓存距离: ");
      // Serial.println(lastValidDistance);
    } else {
      // 缓存也过期了，显示错误状态
      distanceStr = "Dist:---cm";
    }
  }
  
  u8g2.drawStr(0, 21, distanceStr.c_str());
  
  // 第三行：速度信息
  String speedStr = "Speed:" + String(speedPercent) + "%";
  u8g2.drawStr(0, 33, speedStr.c_str());
  
  // 第四行：当前动作状态（简化显示）
  String actionStr = "Status:READY";
  u8g2.drawStr(0, 45, actionStr.c_str());
  
  u8g2.sendBuffer();
}

void displayLCDMessage(const char* line1, const char* line2 = "", const char* line3 = "", const char* line4 = "") {
  if (!lcdInitialized) return;
  
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_7x13B_tf);  // 使用与updateLCDDisplay相同的粗体字体
  
  if (strlen(line1) > 0) u8g2.drawStr(0, 14, line1);
  if (strlen(line2) > 0) u8g2.drawStr(0, 28, line2);
  if (strlen(line3) > 0) u8g2.drawStr(0, 42, line3);
  if (strlen(line4) > 0) u8g2.drawStr(0, 56, line4);
  
  u8g2.sendBuffer();
}

void clearLCDDisplay() {
  if (!lcdInitialized) return;
  
  u8g2.clearBuffer();
  u8g2.sendBuffer();
}

// Status indication
void setLED(bool state) {
  digitalWrite(LED_PIN, state);
}

// Buzzer control
// void beep(int duration = 100) {
//   digitalWrite(BUZZER_PIN, HIGH);
//   delay(duration);
//   digitalWrite(BUZZER_PIN, LOW);
// }

// Enhanced motor control functions with safety check
bool checkSafeToMoveForward() {
  float distance = getDistance();
  
  // Add debug output to help diagnose the issue
  // Serial.print("Distance reading: ");
  // Serial.print(distance);
  // Serial.print(" cm, Safe distance: ");
  // Serial.print(SAFE_DISTANCE);
  // Serial.println(" cm");
  
  // Check for invalid readings (999.0 indicates sensor error)
  if (distance >= 999.0) {
    // Serial.println("Warning: Invalid distance reading, allowing movement");
    return true;  // Allow movement if sensor reading is invalid
  }
  
  bool isSafe = distance > SAFE_DISTANCE;
  // Serial.printf("Safety check: %.2f > %.2f = %s\n", 
  //   distance, SAFE_DISTANCE, isSafe ? "SAFE" : "BLOCKED");
  
  if (!isSafe) {
    // Serial.printf("Movement BLOCKED: obstacle at %.2f cm (< %.2f cm safe distance)\n", 
    //   distance, SAFE_DISTANCE);
  } else {
    // Serial.printf("Movement ALLOWED: clear path at %.2f cm (> %.2f cm safe distance)\n", 
    //   distance, SAFE_DISTANCE);
  }
  
  return isSafe;
}

// Motor control functions (performance optimized)
void setMotorSpeed(int leftSpeed, int rightSpeed) {
  // Serial.printf("=== setMotorSpeed called: left=%d, right=%d ===\n", leftSpeed, rightSpeed);
  
  // Batch set GPIO status to reduce function call overhead
  digitalWrite(MOTOR_IN1, leftSpeed > 0 ? HIGH : LOW);
  digitalWrite(MOTOR_IN2, leftSpeed > 0 ? LOW : HIGH);
  digitalWrite(MOTOR_IN3, rightSpeed > 0 ? HIGH : LOW);
  digitalWrite(MOTOR_IN4, rightSpeed > 0 ? LOW : HIGH);
  
  // Serial.printf("GPIO states - IN1:%d, IN2:%d, IN3:%d, IN4:%d\n", 
  //   leftSpeed > 0 ? 1 : 0, leftSpeed > 0 ? 0 : 1,
  //   rightSpeed > 0 ? 1 : 0, rightSpeed > 0 ? 0 : 1);
  
  // Use ESP32-S3 high-speed PWM
  ledcWrite(0, abs(leftSpeed));   // ENA: PWM channel 0
  ledcWrite(1, abs(rightSpeed));  // ENB: PWM channel 1
  
  // Serial.printf("PWM values - Channel 0 (ENA): %d, Channel 1 (ENB): %d\n", 
  //   abs(leftSpeed), abs(rightSpeed));
}

// Motor direction control with safety check
void moveForward() {
  // Serial.println("=== moveForward() called ===");
  if (!checkSafeToMoveForward()) {
    // Serial.println("Forward blocked - obstacle detected");
    //beep(100);
    displayLCDMessage("BLOCKED!", "Obstacle", "Detected", "");  // 显示障碍物检测信息
    delay(1000);
    return;
  }
  setMotorSpeed(motorSpeed, motorSpeed);
  // Serial.printf("Forward - Motor speed: %d\n", motorSpeed);
  
  // 立即更新LCD显示当前动作
  if (lcdInitialized) {
    lastLCDUpdate = 0; // 强制立即更新
  }
}

void moveBackward() {
  setMotorSpeed(-motorSpeed, -motorSpeed);
  // Serial.println("Backward");
  
  // 立即更新LCD显示当前动作
  if (lcdInitialized) {
    lastLCDUpdate = 0; // 强制立即更新
  }
}

void turnLeft() {
  setMotorSpeed(motorSpeed, -motorSpeed);
  // Serial.println("Turn Left");
  
  // 立即更新LCD显示当前动作
  if (lcdInitialized) {
    lastLCDUpdate = 0; // 强制立即更新
  }
}

void turnRight() {
  setMotorSpeed(-motorSpeed, motorSpeed);
  // Serial.println("Turn Right");
  
  // 立即更新LCD显示当前动作
  if (lcdInitialized) {
    lastLCDUpdate = 0; // 强制立即更新
  }
}

// Mecanum wheel strafe movement functions (left/right side movement)
void moveLeftSide() {
  // Mecanum wheel left strafe: 
  // Front-left and rear-right wheels forward, front-right and rear-left wheels backward
  setMotorSpeed(-motorSpeed, motorSpeed);
  // Serial.println("Mecanum Strafe Left");
}

void moveRightSide() {
  // Mecanum wheel right strafe:
  // Front-right and rear-left wheels forward, front-left and rear-right wheels backward  
  setMotorSpeed(motorSpeed, -motorSpeed);
  // Serial.println("Mecanum Strafe Right");
}

// In-place rotation functions - 180 degree turn (optimized for non-blocking)
void rotateLeft() {
  setMotorSpeed(-motorSpeed, motorSpeed);
  // Serial.println("Rotate Left 180° (In-place)");
  
  // Calculate rotation time based on speed
  // Approximate time for 180° rotation (adjust based on testing)
  int rotationTime = map(motorSpeed, 51, 255, 2000, 1000); // 1-2 seconds based on speed
  
  // Non-blocking delay with watchdog reset
  unsigned long startTime = millis();
  while (millis() - startTime < rotationTime) {
    esp_task_wdt_reset();  // Reset watchdog during rotation
    delay(10);  // Small delay to prevent tight loop
  }
  
  stopMotors();
  // Serial.println("Left 180° rotation completed");
}

void rotateRight() {
  setMotorSpeed(motorSpeed, -motorSpeed);
  // Serial.println("Rotate Right 180° (In-place)");
  
  // Calculate rotation time based on speed
  // Approximate time for 180° rotation (adjust based on testing)
  int rotationTime = map(motorSpeed, 51, 255, 2000, 1000); // 1-2 seconds based on speed
  
  // Non-blocking delay with watchdog reset
  unsigned long startTime = millis();
  while (millis() - startTime < rotationTime) {
    esp_task_wdt_reset();  // Reset watchdog during rotation
    delay(10);  // Small delay to prevent tight loop
  }
  
  stopMotors();
  // Serial.println("Right 180° rotation completed");
}

void stopMotors() {
  setMotorSpeed(0, 0);
  // Serial.println("Stop");
  
  // 立即更新LCD显示当前动作
  if (lcdInitialized) {
    lastLCDUpdate = 0; // 强制立即更新
  }
}

// Speed control functions
void setSpeedPercent(int percent) {
  speedPercent = constrain(percent, 0, 100);
  motorSpeed = map(speedPercent, 0, 100, 0, 255);
  // Serial.printf("Speed set to %d%% (%d/255)\n", speedPercent, motorSpeed);
}

// Enhanced ultrasonic distance measurement with high precision algorithm
// Based on HC-SR04 high precision example

// 超声波传感器变量定义 (使用宏定义的引脚)
// EchoPin 和 TrigPin 已在上面通过 ULTRASONIC_ECHO 和 ULTRASONIC_TRIG 定义
unsigned long Time_Echo_us = 0;
//Len_mm X100 = length*100
unsigned long Len_mm_X100 = 0;
unsigned long Len_Integer = 0; //
unsigned int Len_Fraction = 0;

float getDistance() {
  // 优化的超声波测距，平衡实时性和稳定性
  const int SAMPLE_COUNT = 3;  // 减少采样次数：5->3，提高响应速度
  float validReadings[SAMPLE_COUNT];
  int validCount = 0;
  
  // 进行多次测量
  for (int i = 0; i < SAMPLE_COUNT; i++) {
    // 确保引脚状态正确
    pinMode(ULTRASONIC_TRIG, OUTPUT);
    pinMode(ULTRASONIC_ECHO, INPUT);
    
    // 触发超声波脉冲 (严格按照HC-SR04时序)
    digitalWrite(ULTRASONIC_TRIG, LOW);
    delayMicroseconds(2);  // 减少稳定时间：5->2微秒
    digitalWrite(ULTRASONIC_TRIG, HIGH);
    delayMicroseconds(10);
    digitalWrite(ULTRASONIC_TRIG, LOW);
    
    // 测量回波时间，设置30ms超时（对应约5米最大距离）
    Time_Echo_us = pulseIn(ULTRASONIC_ECHO, HIGH, 30000);
    
    // 检查测量是否有效
    if (Time_Echo_us > 150 && Time_Echo_us < 30000) {
      // HC-SR04超声波距离计算
      Len_mm_X100 = (Time_Echo_us * 34) / 2;
      Len_Integer = Len_mm_X100 / 100;
      Len_Fraction = Len_mm_X100 % 100;
      
      // 转换为cm
      float distance_cm = Len_Integer / 10.0 + Len_Fraction / 1000.0;
      
      // 范围检查 (2cm - 600cm)
      if (distance_cm >= 2.0 && distance_cm <= 600.0) {
        validReadings[validCount] = distance_cm;
        validCount++;
      }
    }
    
    // 缩短采样间隔：20ms->10ms，提高响应速度
    if (i < SAMPLE_COUNT - 1) {
      delay(10);  
    }
  }
  
  // 如果没有有效读数，返回错误值
  if (validCount == 0) {
    return 999.0;
  }
  
  // 计算有效读数的平均值
  float sum = 0;
  for (int i = 0; i < validCount; i++) {
    sum += validReadings[i];
  }
  float avgDistance = sum / validCount;
  
  // 更新缓存变量（保持兼容性）
  cachedDistance = avgDistance;
  lastSensorRead = millis();
  
  return avgDistance;
}

// Global variable to track current servo angle (with 0.09 degree precision)
float currentServoAngle = 90.0;
bool servoActive = false;

// 高精度舵机控制函数 (基于HC-SR04测距例程优化)
void servopulse(float angle) {
  // 限制角度范围
  angle = constrain(angle, 0.0, 180.0);
  
  // 高精度PWM映射 - 使用微秒级精确控制
  // 0度 = 500μs, 90度 = 1500μs, 180度 = 2500μs
  // 使用更精确的计算公式
  unsigned long pulsewidth_us = 500 + (unsigned long)(angle * 1000.0 / 90.0);
  
  // 使用数字写入实现精确脉宽控制
  digitalWrite(SERVO_PIN, HIGH);
  delayMicroseconds(pulsewidth_us);
  digitalWrite(SERVO_PIN, LOW);
  delayMicroseconds(20000 - pulsewidth_us);  // 20ms周期
}

void setServoAngle(float angle) {
  // 限制角度范围
  angle = constrain(angle, 0.0, 180.0);
  
  // 设置死区为0.09度以平衡精度和稳定性
  if (abs(angle - currentServoAngle) < 0.09 && servoActive) {
    return; // 如果变化太小则跳过
  }
  
  currentServoAngle = angle;
  servoActive = true;
  
  // 优化脉冲序列 - 减少阻塞时间
  for (int i = 0; i < 5; i++) {  // 减少脉冲数量从10到5
    servopulse(angle);
    delay(20);  // 20ms间隔
    esp_task_wdt_reset();  // 重置看门狗
  }
  
  // 减少稳定延时
  delay(50);  // 从100ms减少到50ms
  
  // Serial.print("Present angle is: ");
  // Serial.print(angle, 2);
  // Serial.println(" degrees");
}

// 停止舵机PWM信号
void stopServo() {
  digitalWrite(SERVO_PIN, LOW);
  servoActive = false;
  // Serial.println("Servo stopped");
}

// Automatic obstacle avoidance logic
// Remove autoAvoidance function - no longer needed

// Blinker callback functions
/*
void buttonFCallback(const String &state) { 
  if (state == "tap") {
    autoMode = false;
    moveForward();
  }
}

void buttonBCallback(const String &state) { 
  if (state == "tap") {
    autoMode = false;
    moveBackward();
  }
}

void buttonLCallback(const String &state) { 
  if (state == "tap") {
    autoMode = false;
    turnLeft();
  }
}

void buttonRCallback(const String &state) { 
  if (state == "tap") {
    autoMode = false;
    turnRight();
  }
}

void buttonSCallback(const String &state) { 
  if (state == "tap") {
    autoMode = false;
    stopMotors();
  }
}

void buttonAutoCallback(const String &state) {
  if (state == "tap") {
    autoMode = !autoMode;
    if (autoMode) {
      Blinker.print("auto-status", "Auto mode enabled");
      setLED(true);
      beep(100);
    } else {
      Blinker.print("auto-status", "Manual mode");
      setLED(false);
      stopMotors();
    }
  }
}


void sliderSpeedCallback(int32_t value) {
  motorSpeed = constrain(value, 50, 255);
  Blinker.print("speed-value", motorSpeed);
  Serial.printf("Speed setting: %d\n", motorSpeed);
}
*/

/*
// Optimized Web API handling (commented out due to AsyncTCP conflict with Blinker)
void handleWebAPI(AsyncWebServerRequest *request) {
  // 添加看门狗重置，防止处理时间过长
  esp_task_wdt_reset();
  
  // 检查参数是否存在，避免空指针异常
  if (!request->hasParam("action")) {
    request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing action parameter\"}");
    return;
  }
  
  String action = request->getParam("action")->value();
  
  // 使用静态字符串减少内存分配
  static char response[200]; // 预分配响应缓冲区
  
  if (action == "forward") {
    moveForward();
    strcpy(response, "{\"status\":\"ok\",\"action\":\"forward\"}");
  }
  else if (action == "backward") {
    moveBackward();
    strcpy(response, "{\"status\":\"ok\",\"action\":\"backward\"}");
  }
  else if (action == "left") {
    turnLeft();
    strcpy(response, "{\"status\":\"ok\",\"action\":\"left\"}");
  }
  else if (action == "right") {
    turnRight();
    strcpy(response, "{\"status\":\"ok\",\"action\":\"right\"}");
  }
  else if (action == "leftside") {
    moveLeftSide();
    strcpy(response, "{\"status\":\"ok\",\"action\":\"leftside\"}");
  }
  else if (action == "rightside") {
    moveRightSide();
    strcpy(response, "{\"status\":\"ok\",\"action\":\"rightside\"}");
  }
  else if (action == "rotateleft") {
    rotateLeft();
    strcpy(response, "{\"status\":\"ok\",\"action\":\"rotateleft\"}");
  }
  else if (action == "rotateright") {
    rotateRight();
    strcpy(response, "{\"status\":\"ok\",\"action\":\"rotateright\"}");
  }
  else if (action == "stop") {
    stopMotors();
    strcpy(response, "{\"status\":\"ok\",\"action\":\"stop\"}");
  }
  else if (action == "speedpercent") {
    if (!request->hasParam("value")) {
      request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing value parameter\"}");
      return;
    }
    int percent = request->getParam("value")->value().toInt();
    setSpeedPercent(percent);
    snprintf(response, sizeof(response), "{\"status\":\"ok\",\"action\":\"speedpercent\",\"percent\":%d,\"speed\":%d}", speedPercent, motorSpeed);
  }
  else if (action == "speed") {
    if (!request->hasParam("value")) {
      request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing value parameter\"}");
      return;
    }
    int speed = request->getParam("value")->value().toInt();
    motorSpeed = constrain(speed, 50, 255);
    speedPercent = map(motorSpeed, 51, 255, 20, 100);
    snprintf(response, sizeof(response), "{\"status\":\"ok\",\"action\":\"speed\",\"speed\":%d,\"percent\":%d}", motorSpeed, speedPercent);
  }
  else if (action == "distance") {
    float distance = getDistance();
    
    // 增强的错误处理和状态报告
    if (distance >= 999.0) {
      // 超出范围或传感器错误
      snprintf(response, sizeof(response), 
        "{\"status\":\"error\",\"action\":\"distance\",\"distance\":999,\"message\":\"Out of range or sensor error\"}");
    } else if (distance < SAFE_DISTANCE) {
      // 距离过近
      snprintf(response, sizeof(response), 
        "{\"status\":\"warning\",\"action\":\"distance\",\"distance\":%.2f,\"safe_distance\":%.1f,\"message\":\"Distance too close\"}", distance, SAFE_DISTANCE);
    } else {
      // 正常测量
      snprintf(response, sizeof(response), 
        "{\"status\":\"ok\",\"action\":\"distance\",\"distance\":%.2f,\"safe_distance\":%.1f}", distance, SAFE_DISTANCE);
    }
  }
  else if (action == "servo") {
    if (!request->hasParam("value")) {
      request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing value parameter\"}");
      return;
    }
    float angle = request->getParam("value")->value().toFloat();
    angle = constrain(angle, 0.0, 180.0);
    
    // 添加看门狗重置，防止舵机控制时间过长
    esp_task_wdt_reset();
    setServoAngle(angle);
    esp_task_wdt_reset();
    
    snprintf(response, sizeof(response), "{\"status\":\"ok\",\"action\":\"servo\",\"servo_angle\":%.2f}", angle);
  }
  else if (action == "status") {
    // 获取当前状态信息
    float distance = getDistance();
    snprintf(response, sizeof(response), 
      "{\"status\":\"ok\",\"action\":\"status\",\"distance\":%.2f,\"speed\":%d,\"percent\":%d,\"safe_distance\":%.1f}", 
      distance, motorSpeed, speedPercent, SAFE_DISTANCE);
  }
  else {
    request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Unknown action\"}");
    return;
  }
  
  request->send(200, "application/json", response);
}

// New RESTful API handlers
void handleCarMovement(AsyncWebServerRequest *request, const char* action) {
  esp_task_wdt_reset();
  
  static char response[200];
  
  if (strcmp(action, "forward") == 0) {
    moveForward();
  } else if (strcmp(action, "backward") == 0) {
    moveBackward();
  } else if (strcmp(action, "left") == 0) {
    turnLeft();
  } else if (strcmp(action, "right") == 0) {
    turnRight();
  } else if (strcmp(action, "leftside") == 0) {
    moveLeftSide();
  } else if (strcmp(action, "rightside") == 0) {
    moveRightSide();
  } else if (strcmp(action, "rotateleft") == 0) {
    rotateLeft();
  } else if (strcmp(action, "rotateright") == 0) {
    rotateRight();
  } else if (strcmp(action, "stop") == 0) {
    stopMotors();
  }
  
  snprintf(response, sizeof(response), "{\"status\":\"ok\",\"action\":\"%s\"}", action);
  request->send(200, "application/json", response);
}

void handleSpeedControl(AsyncWebServerRequest *request) {
  esp_task_wdt_reset();
  
  if (request->hasParam("speed", true)) {
    int percent = request->getParam("speed", true)->value().toInt();
    setSpeedPercent(percent);
    
    static char response[200];
    snprintf(response, sizeof(response), 
      "{\"status\":\"ok\",\"action\":\"speed\",\"percent\":%d,\"speed\":%d}", 
      speedPercent, motorSpeed);
    request->send(200, "application/json", response);
  } else {
    request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing speed parameter\"}");
  }
}

void handleServoControl(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
  esp_task_wdt_reset();
  
  // 首先检查URL参数（向后兼容）
  if (request->hasParam("value", true)) {
    float angle = request->getParam("value", true)->value().toFloat();
    angle = constrain(angle, 0.0, 180.0);
    
    esp_task_wdt_reset();
    setServoAngle(angle);
    esp_task_wdt_reset();
    
    static char response[200];
    snprintf(response, sizeof(response), 
      "{\"status\":\"ok\",\"action\":\"servo\",\"servo_angle\":%.2f}", angle);
    request->send(200, "application/json", response);
    return;
  } 
  // 也支持angle参数（向后兼容）
  else if (request->hasParam("angle", true)) {
    float angle = request->getParam("angle", true)->value().toFloat();
    angle = constrain(angle, 0.0, 180.0);
    
    esp_task_wdt_reset();
    setServoAngle(angle);
    esp_task_wdt_reset();
    
    static char response[200];
    snprintf(response, sizeof(response), 
      "{\"status\":\"ok\",\"action\":\"servo\",\"servo_angle\":%.2f}", angle);
    request->send(200, "application/json", response);
    return;
  }
  
  // 处理JSON请求体
  if (data && len > 0) {
    String body = String((char*)data);
    Serial.println("Servo control body: " + body);
    
    // 简单的JSON解析（查找value字段）
    int valueIndex = body.indexOf("\"value\":");
    if (valueIndex != -1) {
      int startIndex = valueIndex + 8; // "value":后面的位置
      int endIndex = body.indexOf(',', startIndex);
      if (endIndex == -1) endIndex = body.indexOf('}', startIndex);
      
      if (endIndex != -1) {
        String valueStr = body.substring(startIndex, endIndex);
        valueStr.trim();
        float angle = valueStr.toFloat();
        angle = constrain(angle, 0.0, 180.0);
        
        Serial.println("Parsed servo angle: " + String(angle));
        
        esp_task_wdt_reset();
        setServoAngle(angle);
        esp_task_wdt_reset();
        
        static char response[200];
        snprintf(response, sizeof(response), 
          "{\"status\":\"ok\",\"action\":\"servo\",\"servo_angle\":%.2f}", angle);
        request->send(200, "application/json", response);
        return;
      }
    }
  }
  
  request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"Missing value or angle parameter\"}");
}

void handleStatusRequest(AsyncWebServerRequest *request) {
  esp_task_wdt_reset();
  
  // Serial.println("=== 处理状态请求 ===");
  float distance = getDistance();
  // Serial.print("获取到的距离: ");
  // Serial.println(distance);
  
  static char response[400];
  
  // 增强状态响应，包含舵机角度信息
  if (distance >= 999.0) {
    // 超声波传感器错误或超出范围
    Serial.println("状态: 超声波传感器错误");
    snprintf(response, sizeof(response), 
      "{\"status\":\"error\",\"distance\":999,\"speed\":%d,\"percent\":%d,\"servo_angle\":%.2f,\"safe_distance\":%.1f,\"message\":\"Ultrasonic sensor error\"}", 
      motorSpeed, speedPercent, currentServoAngle, SAFE_DISTANCE);
  } else if (distance < SAFE_DISTANCE) {
    // 距离过近，警告状态
    Serial.println("状态: 距离过近警告");
    snprintf(response, sizeof(response), 
      "{\"status\":\"warning\",\"distance\":%.2f,\"speed\":%d,\"percent\":%d,\"servo_angle\":%.2f,\"safe_distance\":%.1f,\"message\":\"Distance too close\"}", 
      distance, motorSpeed, speedPercent, currentServoAngle, SAFE_DISTANCE);
  } else {
    // 正常状态
    Serial.println("状态: 正常");
    snprintf(response, sizeof(response), 
      "{\"status\":\"ok\",\"distance\":%.2f,\"speed\":%d,\"percent\":%d,\"servo_angle\":%.2f,\"safe_distance\":%.1f}", 
      distance, motorSpeed, speedPercent, currentServoAngle, SAFE_DISTANCE);
  }
  
  Serial.print("发送响应: ");
  Serial.println(response);
  request->send(200, "application/json", response);
}
*/

/*
// Optimized Web control page (commented out due to AsyncTCP conflict with Blinker)
const char* webPage = R"HTML(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP32-S3 智能小车控制台</title>
    <style>
        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
        }
        
        body { 
            font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            min-height: 100vh;
            padding: 10px;
            color: #333;
        }
        
        .main-container {
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 20px;
            max-width: 1200px;
            margin: 0 auto;
        }
        
        .left-panel, .right-panel {
            background: rgba(255, 255, 255, 0.95);
            border-radius: 20px;
            padding: 25px;
            box-shadow: 0 15px 35px rgba(0,0,0,0.1);
            backdrop-filter: blur(10px);
        }
        
        .header {
            text-align: center;
            margin-bottom: 25px;
            grid-column: 1 / -1;
            background: rgba(255, 255, 255, 0.95);
            border-radius: 20px;
            padding: 20px;
            box-shadow: 0 15px 35px rgba(0,0,0,0.1);
        }
        
        .header h1 {
            color: #4a5568;
            font-size: 2.2em;
            margin-bottom: 10px;
            text-shadow: 2px 2px 4px rgba(0,0,0,0.1);
        }
        
        .header .subtitle {
            color: #718096;
            font-size: 1.1em;
        }
        
        .video-section {
            margin-bottom: 25px;
        }
        
        .video-container {
            position: relative;
            background: #000;
            border-radius: 15px;
            overflow: hidden;
            box-shadow: 0 10px 25px rgba(0,0,0,0.2);
            margin-bottom: 15px;
        }
        
        .video-stream {
            width: 100%;
            height: 280px;
            object-fit: cover;
            display: block;
        }
        
        .video-placeholder {
            width: 100%;
            height: 280px;
            background: linear-gradient(45deg, #2d3748, #4a5568);
            display: flex;
            align-items: center;
            justify-content: center;
            color: white;
            font-size: 1.2em;
            text-align: center;
        }
        
        .video-controls {
            display: flex;
            gap: 10px;
            align-items: center;
            flex-wrap: wrap;
        }
        
        .video-input {
            flex: 1;
            padding: 10px 15px;
            border: 2px solid #e2e8f0;
            border-radius: 10px;
            font-size: 14px;
            transition: border-color 0.3s;
        }
        
        .video-input:focus {
            outline: none;
            border-color: #667eea;
        }
        
        .video-btn {
            padding: 10px 20px;
            background: linear-gradient(135deg, #667eea, #764ba2);
            color: white;
            border: none;
            border-radius: 10px;
            cursor: pointer;
            font-weight: 600;
            transition: transform 0.2s, box-shadow 0.2s;
        }
        
        .video-btn:hover {
            transform: translateY(-2px);
            box-shadow: 0 5px 15px rgba(102, 126, 234, 0.4);
        }
        
        .status-panel {
            background: linear-gradient(135deg, #48bb78, #38a169);
            color: white;
            padding: 20px;
            border-radius: 15px;
            margin-bottom: 25px;
            box-shadow: 0 8px 20px rgba(72, 187, 120, 0.3);
        }
        
        .status-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(120px, 1fr));
            gap: 15px;
        }
        
        .status-item {
            text-align: center;
            background: rgba(255, 255, 255, 0.2);
            padding: 15px;
            border-radius: 10px;
        }
        
        .status-label {
            font-size: 0.9em;
            opacity: 0.9;
            margin-bottom: 5px;
        }
        
        .status-value {
            font-size: 1.8em;
            font-weight: bold;
        }
        
        .control-btn { 
            width: 85px; 
            height: 85px; 
            margin: 8px; 
            font-size: 14px; 
            font-weight: 600;
            border: none; 
            border-radius: 15px; 
            cursor: pointer;
            transition: all 0.3s ease;
            box-shadow: 0 4px 15px rgba(0,0,0,0.1);
            position: relative;
            overflow: hidden;
        }
        
        .control-btn:hover {
            transform: translateY(-3px);
            box-shadow: 0 8px 25px rgba(0,0,0,0.2);
        }
        
        .control-btn:active {
            transform: translateY(-1px);
        }
        
        .forward { 
            background: linear-gradient(135deg, #48bb78, #38a169);
            color: white; 
        }
        .backward { 
            background: linear-gradient(135deg, #f56565, #e53e3e);
            color: white; 
        }
        .left { 
            background: linear-gradient(135deg, #4299e1, #3182ce);
            color: white; 
        }
        .right { 
            background: linear-gradient(135deg, #ed8936, #dd6b20);
            color: white; 
        }
        .stop { 
            background: linear-gradient(135deg, #a0aec0, #718096);
            color: white; 
        }
        .strafe { 
            background: linear-gradient(135deg, #9f7aea, #805ad5);
            color: white; 
        }
        .rotate { 
            background: linear-gradient(135deg, #ed64a6, #d53f8c);
            color: white; 
        }
        
        .movement-section { 
            margin: 25px 0; 
            padding: 20px; 
            background: rgba(247, 250, 252, 0.8);
            border-radius: 15px;
            box-shadow: 0 4px 15px rgba(0,0,0,0.05);
        }
        
        .movement-title { 
            font-weight: 700; 
            margin-bottom: 15px; 
            color: #2d3748;
            font-size: 1.2em;
            text-align: center;
        }
        
        .control-grid {
            display: flex;
            flex-direction: column;
            align-items: center;
            gap: 10px;
        }
        
        .control-row {
            display: flex;
            justify-content: center;
            gap: 10px;
        }
        
        .slider-container {
            background: rgba(247, 250, 252, 0.8);
            padding: 20px;
            border-radius: 15px;
            margin: 15px 0;
            box-shadow: 0 4px 15px rgba(0,0,0,0.05);
        }
        
        .slider-label {
            font-weight: 600;
            color: #2d3748;
            margin-bottom: 10px;
            display: block;
        }
        
        .slider {
            width: 100%;
            height: 8px;
            border-radius: 5px;
            background: #e2e8f0;
            outline: none;
            -webkit-appearance: none;
            margin: 10px 0;
        }
        
        .slider::-webkit-slider-thumb {
            -webkit-appearance: none;
            appearance: none;
            width: 24px;
            height: 24px;
            border-radius: 50%;
            background: linear-gradient(135deg, #667eea, #764ba2);
            cursor: pointer;
            box-shadow: 0 2px 10px rgba(102, 126, 234, 0.3);
        }
        
        .slider::-moz-range-thumb {
            width: 24px;
            height: 24px;
            border-radius: 50%;
            background: linear-gradient(135deg, #667eea, #764ba2);
            cursor: pointer;
            border: none;
            box-shadow: 0 2px 10px rgba(102, 126, 234, 0.3);
        }
        
        .servo-controls {
            display: flex;
            align-items: center;
            justify-content: center;
            gap: 15px;
            margin-top: 15px;
            flex-wrap: wrap;
        }
        
        .servo-adjuster {
            display: flex;
            border: 2px solid #667eea;
            border-radius: 25px;
            overflow: hidden;
        }
        
        .servo-btn {
            width: 45px;
            height: 45px;
            font-size: 18px;
            font-weight: bold;
            background: linear-gradient(135deg, #667eea, #764ba2);
            color: white;
            border: none;
            cursor: pointer;
            transition: all 0.2s;
        }
        
        .servo-btn:hover {
            background: linear-gradient(135deg, #5a67d8, #6b46c1);
        }
        
        .servo-input {
            width: 80px;
            text-align: center;
            border: 2px solid #e2e8f0;
            border-radius: 8px;
            padding: 8px;
            font-size: 14px;
        }
        
        .keyboard-hints {
            background: rgba(247, 250, 252, 0.8);
            padding: 15px;
            border-radius: 10px;
            margin-top: 20px;
            font-size: 0.9em;
            color: #4a5568;
        }
        
        .keyboard-hints h4 {
            margin-bottom: 10px;
            color: #2d3748;
        }
        
        .hint-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(150px, 1fr));
            gap: 8px;
        }
        
        @media (max-width: 768px) {
            .main-container {
                grid-template-columns: 1fr;
                gap: 15px;
            }
            
            .control-btn {
                width: 70px;
                height: 70px;
                font-size: 12px;
            }
            
            .header h1 {
                font-size: 1.8em;
            }
            
            .video-stream, .video-placeholder {
                height: 200px;
            }
        }
    </style>
</head>
<body>
    <div class="main-container">
        <div class="header">
            <h1>🚗 ESP32-S3 智能小车控制台</h1>
            <div class="subtitle">高精度麦克纳姆轮控制系统</div>
        </div>
        
        <div class="left-panel">
            <div class="video-section">
                <h3 style="margin-bottom: 15px; color: #2d3748;">📹 实时视频监控</h3>
                <div class="video-container">
                    <div id="videoPlaceholder" class="video-placeholder">
                        <div>
                            <div style="font-size: 3em; margin-bottom: 10px;">📷</div>
                            <div>请输入摄像头IP地址</div>
                            <div style="font-size: 0.9em; opacity: 0.7; margin-top: 5px;">支持ESP32-CAM等设备</div>
                        </div>
                    </div>
                    <img id="videoStream" class="video-stream" style="display: none;" alt="视频流">
                </div>
                <div class="video-controls">
                    <input type="text" id="cameraIP" class="video-input" placeholder="输入摄像头IP (例: 192.168.1.100)" value="">
                    <button onclick="connectCamera()" class="video-btn">连接摄像头</button>
                    <button onclick="disconnectCamera()" class="video-btn" style="background: linear-gradient(135deg, #f56565, #e53e3e);">断开连接</button>
                </div>
            </div>
            
            <div class="status-panel">
                <h3 style="margin-bottom: 15px; text-align: center;">📊 系统状态</h3>
                <div class="status-grid">
                    <div class="status-item">
                        <div class="status-label">距离检测</div>
                        <div class="status-value"><span id="distance">--</span> cm</div>
                    </div>
                    <div class="status-item">
                        <div class="status-label">安全距离</div>
                        <div class="status-value"><span id="safeDistance">10</span> cm</div>
                    </div>
                    <div class="status-item">
                        <div class="status-label">当前速度</div>
                        <div class="status-value"><span id="speedPercent">78</span>%</div>
                    </div>
                </div>
            </div>
        </div>
        
        <div class="right-panel">
            <div class="movement-section">
                <div class="movement-title">🎮 基本运动控制</div>
                <div class="control-grid">
                    <div class="control-row">
                        <button class="control-btn forward" onmousedown="startMovement('forward')" onmouseup="stopMovement()" onmouseleave="stopMovement()">↑<br>前进</button>
                    </div>
                    <div class="control-row">
                        <button class="control-btn left" onmousedown="startMovement('left')" onmouseup="stopMovement()" onmouseleave="stopMovement()">↰<br>左转</button>
                        <button class="control-btn stop" onclick="sendCommand('stop')">■<br>停止</button>
                        <button class="control-btn right" onmousedown="startMovement('right')" onmouseup="stopMovement()" onmouseleave="stopMovement()">↱<br>右转</button>
                    </div>
                    <div class="control-row">
                        <button class="control-btn backward" onmousedown="startMovement('backward')" onmouseup="stopMovement()" onmouseleave="stopMovement()">↓<br>后退</button>
                    </div>
                </div>
            </div>
            
            <div class="movement-section">
                <div class="movement-title">⚙️ 麦轮高级控制</div>
                <div class="control-grid">
                    <div class="control-row">
                        <button class="control-btn strafe" onmousedown="startMovement('leftShift')" onmouseup="stopMovement()" onmouseleave="stopMovement()">←<br>左平移</button>
                        <button class="control-btn strafe" onmousedown="startMovement('rightShift')" onmouseup="stopMovement()" onmouseleave="stopMovement()">→<br>右平移</button>
                    </div>
                    <div class="control-row">
                        <button class="control-btn rotate" onmousedown="startMovement('leftTurn')" onmouseup="stopMovement()" onmouseleave="stopMovement()">↺<br>左掉头</button>
                        <button class="control-btn rotate" onmousedown="startMovement('rightTurn')" onmouseup="stopMovement()" onmouseleave="stopMovement()">↻<br>右掉头</button>
                    </div>
                </div>
            </div>
            
            <div class="slider-container">
                <label class="slider-label">🚀 速度控制: <span id="speedValue">78</span>%</label>
                <input type="range" id="speedSlider" class="slider" min="20" max="100" value="78" onchange="setSpeedPercent(this.value)">
            </div>
            
            <div class="slider-container">
                <label class="slider-label">🎯 舵机控制: <span id="servoAngle">90</span>°</label>
                <input type="range" id="servoSlider" class="slider" min="0" max="180" step="0.18" value="90" onchange="setServo(this.value)">
                <div class="servo-controls">
                    <div class="servo-adjuster">
                        <button class="servo-btn" onmousedown="startAdjust(-1)" onmouseup="stopAdjust()" onmouseleave="stopAdjust()">-</button>
                        <button class="servo-btn" onmousedown="startAdjust(1)" onmouseup="stopAdjust()" onmouseleave="stopAdjust()">+</button>
                    </div>
                    <input type="number" id="servoInput" class="servo-input" min="0" max="180" step="0.18" value="90" onchange="setServoFromInput(this.value)" placeholder="角度">
                </div>
            </div>
            
            <div class="keyboard-hints">
                <h4>⌨️ 键盘快捷键</h4>
                <div class="hint-grid">
                    <div>↑/W: 前进</div>
                    <div>↓/S: 后退</div>
                    <div>←/A: 左转</div>
                    <div>→/D: 右转</div>
                    <div>Q: 左掉头</div>
                    <div>E: 右掉头</div>
                    <div>Z: 左平移</div>
                    <div>C: 右平移</div>
                    <div>空格: 停止</div>
                </div>
            </div>
    </div>

    <script>
        // 点动控制变量
        let isMoving = false;
        let currentAction = null;
        
        // 视频流相关变量
        let videoConnected = false;
        let videoCheckInterval = null;
        
        // 视频流功能
        function connectCamera() {
            const cameraIP = document.getElementById('cameraIP').value.trim();
            if (!cameraIP) {
                alert('请输入摄像头IP地址');
                return;
            }
            
            // 支持多种常见的ESP32-CAM视频流格式
            const videoUrls = [
                `http://${cameraIP}/stream`,
                `http://${cameraIP}:81/stream`,
                `http://${cameraIP}/mjpeg/1`,
                `http://${cameraIP}/video`,
                `http://${cameraIP}/cam-hi.jpg`
            ];
            
            const videoStream = document.getElementById('videoStream');
            const videoPlaceholder = document.getElementById('videoPlaceholder');
            
            // 尝试连接第一个URL
            tryConnectVideo(videoUrls, 0, videoStream, videoPlaceholder);
        }
        
        function tryConnectVideo(urls, index, videoElement, placeholderElement) {
            if (index >= urls.length) {
                alert('无法连接到摄像头，请检查IP地址和网络连接');
                return;
            }
            
            const currentUrl = urls[index];
            console.log(`尝试连接视频流: ${currentUrl}`);
            
            // 创建新的图片元素进行测试
            const testImg = new Image();
            testImg.onload = function() {
                // 连接成功
                videoElement.src = currentUrl;
                videoElement.style.display = 'block';
                placeholderElement.style.display = 'none';
                videoConnected = true;
                
                // 开始监控视频流状态
                startVideoMonitoring(videoElement, placeholderElement);
                
                console.log(`视频流连接成功: ${currentUrl}`);
                
                // 保存成功的IP到本地存储
                localStorage.setItem('cameraIP', document.getElementById('cameraIP').value);
            };
            
            testImg.onerror = function() {
                // 连接失败，尝试下一个URL
                console.log(`视频流连接失败: ${currentUrl}`);
                tryConnectVideo(urls, index + 1, videoElement, placeholderElement);
            };
            
            // 设置超时
            setTimeout(() => {
                if (!videoConnected) {
                    testImg.src = '';
                    tryConnectVideo(urls, index + 1, videoElement, placeholderElement);
                }
            }, 3000);
            
            testImg.src = currentUrl;
        }
        
        function disconnectCamera() {
            const videoStream = document.getElementById('videoStream');
            const videoPlaceholder = document.getElementById('videoPlaceholder');
            
            videoStream.src = '';
            videoStream.style.display = 'none';
            videoPlaceholder.style.display = 'flex';
            videoConnected = false;
            
            // 停止视频监控
            if (videoCheckInterval) {
                clearInterval(videoCheckInterval);
                videoCheckInterval = null;
            }
            
            console.log('视频流已断开');
        }
        
        function startVideoMonitoring(videoElement, placeholderElement) {
            // 清除之前的监控
            if (videoCheckInterval) {
                clearInterval(videoCheckInterval);
            }
            
            // 每5秒检查一次视频流状态
            videoCheckInterval = setInterval(() => {
                if (videoConnected) {
                    // 通过创建新的图片元素检查视频流是否仍然可用
                    const testImg = new Image();
                    testImg.onload = function() {
                        // 视频流正常
                    };
                    testImg.onerror = function() {
                        // 视频流断开，自动重连
                        console.log('检测到视频流断开，尝试重连...');
                        connectCamera();
                    };
                    testImg.src = videoElement.src + '?t=' + Date.now();
                }
            }, 5000);
        }
        
        // 页面加载时恢复上次的摄像头IP
        window.addEventListener('load', function() {
            const savedIP = localStorage.getItem('cameraIP');
            if (savedIP) {
                document.getElementById('cameraIP').value = savedIP;
            }
        });
        
        function sendCommand(action) {
            // 使用新的RESTful API端点
            const endpoint = `/api/${action}`;
            fetch(endpoint, {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json'
                }
            })
                .then(response => response.json())
                .then(data => {
                    console.log('Command response:', data);
                    updateStatus();
                })
                .catch(error => console.error('Error:', error));
        }
        
        // 点动控制：开始移动
        function startMovement(action) {
            if (!isMoving) {
                isMoving = true;
                currentAction = action;
                sendCommand(action);
                console.log('开始移动:', action);
            }
        }
        
        // 点动控制：停止移动
        function stopMovement() {
            if (isMoving) {
                isMoving = false;
                sendCommand('stop');
                console.log('停止移动，之前动作:', currentAction);
                currentAction = null;
            }
        }
        
        function setSpeedPercent(value) {
            document.getElementById('speedValue').textContent = value;
            document.getElementById('speedPercent').textContent = value;
            
            // 使用新的RESTful API
            fetch('/api/speed', {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json'
                },
                body: JSON.stringify({ value: parseInt(value) })
            })
                .then(response => response.json())
                .then(data => console.log('Speed response:', data))
                .catch(error => console.error('Speed error:', error));
        }
        
        function updateStatus() {
            // 使用新的RESTful API获取状态
            fetch('/api/status')
                .then(response => response.json())
                .then(data => {
                    console.log('Status response:', data); // 调试信息
                    
                    // 更新距离显示
                    if (data.distance !== undefined) {
                        const distanceElement = document.getElementById('distance');
                        if (distanceElement) {
                            console.log('更新距离显示:', data.distance); // 调试日志
                            if (data.distance >= 999) {
                                distanceElement.textContent = '--';
                                distanceElement.style.color = '#ff4444';
                                console.log('距离传感器错误，显示 --');
                            } else {
                                distanceElement.textContent = data.distance.toFixed(1);
                                distanceElement.style.color = data.distance < data.safe_distance ? '#ff4444' : '#00ff00';
                                console.log('距离更新成功:', data.distance.toFixed(1) + ' cm');
                            }
                        } else {
                            console.error('找不到距离显示元素 #distance');
                        }
                    } else {
                        console.error('API响应中缺少distance字段');
                    }
                    
                    // 更新舵机角度显示
                    if (data.servo_angle !== undefined) {
                        const servoAngleElement = document.getElementById('servoAngle');
                        if (servoAngleElement) {
                            servoAngleElement.textContent = data.servo_angle.toFixed(1);
                        }
                        
                        // 同步滑块位置
                        const servoSlider = document.getElementById('servoSlider');
                        if (servoSlider && Math.abs(servoSlider.value - data.servo_angle) > 1) {
                            servoSlider.value = data.servo_angle;
                        }
                    }
                    
                    // 更新速度显示
                    if (data.percent !== undefined) {
                        const speedElement = document.getElementById('speedPercent');
                        if (speedElement) {
                            speedElement.textContent = data.percent;
                        }
                    }
                })
                .catch(error => {
                    console.error('Status update error:', error);
                    // 显示错误状态
                    const distanceElement = document.getElementById('distance');
                    if (distanceElement) {
                        distanceElement.textContent = 'Offline';
                        distanceElement.style.color = '#888888';
                    }
                });
        }
        
        // 定期更新状态
        setInterval(updateStatus, 1000);
        // Variables for continuous servo adjustment
        let adjustInterval = null;
        let adjustDirection = 0;
        let lastServoCommand = 0; // 记录上次发送命令的时间
        let pendingServoAngle = null; // 待发送的角度
        let servoCommandTimeout = null; // 防抖定时器
        
        // ESP32 IP address - 自动获取当前页面的主机地址
        const ESP32_IP = window.location.origin;
        
        function setServo(angle) {
            angle = parseFloat(angle);
            document.getElementById('servoAngle').textContent = angle.toFixed(2);
            document.getElementById('servoInput').value = angle.toFixed(2);
            
            // 使用防抖机制发送舵机控制命令
            sendServoCommandDebounced(angle);
            console.log('设置舵机角度:', angle);
        }
        
        // 防抖的舵机控制命令发送
        function sendServoCommandDebounced(angle) {
            pendingServoAngle = angle;
            
            // 清除之前的定时器
            if (servoCommandTimeout) {
                clearTimeout(servoCommandTimeout);
            }
            
            // 增加防抖延迟，减少请求频率
            servoCommandTimeout = setTimeout(() => {
                const now = Date.now();
                // 增加最小请求间隔到300ms，进一步减少舵机抖动
                if (now - lastServoCommand >= 300) {
                    sendServoCommand(pendingServoAngle);
                    lastServoCommand = now;
                } else {
                    // 如果间隔太短，再次延迟
                    setTimeout(() => {
                        sendServoCommand(pendingServoAngle);
                        lastServoCommand = Date.now();
                    }, 300 - (now - lastServoCommand));
                }
            }, 150); // 增加防抖延迟到150ms
        }
        
        // 发送舵机控制命令到ESP32
        function sendServoCommand(angle) {
            fetch('/api/servo', {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json'
                },
                body: JSON.stringify({ value: parseFloat(angle) })
            })
                .then(response => {
                    if (!response.ok) {
                        throw new Error(`HTTP error! status: ${response.status}`);
                    }
                    return response.json();
                })
                .then(data => {
                    console.log('舵机控制响应:', data);
                })
                .catch(error => {
                    console.error('舵机控制错误:', error);
                    // 错误时不重试，避免加重服务器负担
                });
        }
        
        // Start continuous servo adjustment
        function startAdjust(direction) {
            adjustDirection = direction;
            // First immediate adjustment
            adjustServoByStep(direction * 1); // 1 degree step
            
            // Start continuous adjustment after 500ms delay (增加延迟)
            setTimeout(() => {
                if (adjustDirection === direction) {
                    adjustInterval = setInterval(() => {
                        adjustServoByStep(direction * 1);
                    }, 200); // 调整间隔从100ms增加到200ms
                }
            }, 500); // 初始延迟从300ms增加到500ms
        }
        
        // Stop continuous servo adjustment
        function stopAdjust() {
            adjustDirection = 0;
            if (adjustInterval) {
                clearInterval(adjustInterval);
                adjustInterval = null;
            }
        }
        
        // Updated function for servo adjustment with fixed 1 degree step
        function adjustServoByStep(adjustment) {
            const slider = document.getElementById('servoSlider');
            const currentAngle = parseFloat(slider.value);
            let newAngle = currentAngle + adjustment;
            
            // Debug logging
            console.log('调整前角度:', currentAngle);
            console.log('调整值:', adjustment);
            console.log('计算后角度:', newAngle);
            
            // Constrain angle between 0 and 180
            newAngle = Math.max(0, Math.min(180, newAngle));
            console.log('边界检查后角度:', newAngle);
            
            // Round to nearest 0.18 degree increment with better precision handling
            newAngle = Math.round(newAngle / 0.18) * 0.18;
            // Fix floating point precision issues
            newAngle = Math.round(newAngle * 100) / 100;
            console.log('精度调整后角度:', newAngle);
            
            slider.value = newAngle;
            document.getElementById('servoInput').value = newAngle.toFixed(2);
            setServo(newAngle);
        }
        
        // New function for direct angle input
        function setServoFromInput(angle) {
            angle = parseFloat(angle);
            
            // Constrain angle between 0 and 180
            angle = Math.max(0, Math.min(180, angle));
            
            // Round to nearest 0.18 degree increment
            angle = Math.round(angle / 0.18) * 0.18;
            
            document.getElementById('servoSlider').value = angle;
            document.getElementById('servoInput').value = angle.toFixed(2);
            setServo(angle);
        }
        
        // Enhanced stop function that also stops servo
        function stopAll() {
            sendCommand('stop');
            console.log('Motors stopped');
        }
        
        // Keyboard control with jog functionality
        let keyPressed = {};
        
        document.addEventListener('keydown', function(event) {
            // Prevent repeated keydown events
            if (keyPressed[event.key]) return;
            keyPressed[event.key] = true;
            
            switch(event.key) {
                case 'ArrowUp': case 'w': case 'W': startMovement('forward'); break;
                case 'ArrowDown': case 's': case 'S': startMovement('backward'); break;
                case 'ArrowLeft': case 'a': case 'A': startMovement('left'); break;
                case 'ArrowRight': case 'd': case 'D': startMovement('right'); break;
                case 'q': case 'Q': startMovement('rotateleft'); break;
                case 'e': case 'E': startMovement('rotateright'); break;
                case 'z': case 'Z': startMovement('leftside'); break;
                case 'c': case 'C': startMovement('rightside'); break;
                case ' ': sendCommand('stop'); event.preventDefault(); break;
            }
        });
        
        document.addEventListener('keyup', function(event) {
            keyPressed[event.key] = false;
            
            switch(event.key) {
                case 'ArrowUp': case 'w': case 'W':
                case 'ArrowDown': case 's': case 'S':
                case 'ArrowLeft': case 'a': case 'A':
                case 'ArrowRight': case 'd': case 'D':
                case 'q': case 'Q':
                case 'e': case 'E':
                case 'z': case 'Z':
                case 'c': case 'C':
                    stopMovement();
                    break;
            }
        });
    </script>
</body>
</html>
)HTML";
*/

// Blinker回调函数定义
void buttonFCallback(const String & state) {
  BLINKER_LOG("收到前进按钮: ", state);
  // Serial.printf("=== buttonFCallback called with state: %s ===\n", state.c_str());
  
  if (state == BLINKER_CMD_ON) {
    // 按下时开始前进
    // Serial.println("Button pressed - starting forward movement");
    moveForward();
  } else if (state == BLINKER_CMD_OFF) {
    // 松开时停止
    // Serial.println("Button released - stopping motors");
    stopMotors();
  } else if (state == BLINKER_CMD_BUTTON_PRESSED) {
    // 长按开始 - 开始前进
    // Serial.println("Button long pressed - starting forward movement");
    moveForward();
  } else if (state == BLINKER_CMD_BUTTON_RELEASED) {
    // 长按释放 - 停止
    // Serial.println("Button long released - stopping motors");
    stopMotors();
  } else if (state == "tap" || state == BLINKER_CMD_BUTTON_TAP) {
    // 处理点击事件 - 短暂前进
    // Serial.println("Button tapped - brief forward movement");
    moveForward();
    delay(200);  // 短暂移动200ms
    stopMotors();
    // Serial.println("Brief forward movement completed");
  }
}

void buttonBCallback(const String & state) {
  BLINKER_LOG("收到后退按钮: ", state);
  // Serial.printf("=== buttonBCallback called with state: %s ===\n", state.c_str());
  
  if (state == BLINKER_CMD_ON) {
    // 按下时开始后退
    // Serial.println("Button pressed - starting backward movement");
    moveBackward();
  } else if (state == BLINKER_CMD_OFF) {
    // 松开时停止
    // Serial.println("Button released - stopping motors");
    stopMotors();
  } else if (state == BLINKER_CMD_BUTTON_PRESSED) {
    // 长按开始 - 开始后退
    // Serial.println("Button long pressed - starting backward movement");
    moveBackward();
  } else if (state == BLINKER_CMD_BUTTON_RELEASED) {
    // 长按释放 - 停止
    // Serial.println("Button long released - stopping motors");
    stopMotors();
  } else if (state == "tap" || state == BLINKER_CMD_BUTTON_TAP) {
    // 处理点击事件 - 短暂后退
    // Serial.println("Button tapped - brief backward movement");
    moveBackward();
    delay(200);  // 短暂移动200ms
    stopMotors();
    // Serial.println("Brief backward movement completed");
  }
}

void buttonLCallback(const String & state) {
  BLINKER_LOG("收到左转按钮: ", state);
  // Serial.printf("=== buttonLCallback called with state: %s ===\n", state.c_str());
  
  if (state == BLINKER_CMD_ON) {
    // 按下时开始左转
    // Serial.println("Button pressed - starting left turn");
    turnLeft();
  } else if (state == BLINKER_CMD_OFF) {
    // 松开时停止
    // Serial.println("Button released - stopping motors");
    stopMotors();
  } else if (state == BLINKER_CMD_BUTTON_PRESSED) {
    // 长按开始 - 开始左转
    // Serial.println("Button long pressed - starting left turn");
    turnLeft();
  } else if (state == BLINKER_CMD_BUTTON_RELEASED) {
    // 长按释放 - 停止
    // Serial.println("Button long released - stopping motors");
    stopMotors();
  } else if (state == "tap" || state == BLINKER_CMD_BUTTON_TAP) {
    // 处理点击事件 - 短暂左转
    // Serial.println("Button tapped - brief left turn");
    turnLeft();
    delay(200);  // 短暂转向200ms
    stopMotors();
    // Serial.println("Brief left turn completed");
  }
}

void buttonRCallback(const String & state) {
  BLINKER_LOG("收到右转按钮: ", state);
  // Serial.printf("=== buttonRCallback called with state: %s ===\n", state.c_str());
  
  if (state == BLINKER_CMD_ON) {
    // 按下时开始右转
    // Serial.println("Button pressed - starting right turn");
    turnRight();
  } else if (state == BLINKER_CMD_OFF) {
    // 松开时停止
    // Serial.println("Button released - stopping motors");
    stopMotors();
  } else if (state == BLINKER_CMD_BUTTON_PRESSED) {
    // 长按开始 - 开始右转
    // Serial.println("Button long pressed - starting right turn");
    turnRight();
  } else if (state == BLINKER_CMD_BUTTON_RELEASED) {
    // 长按释放 - 停止
    // Serial.println("Button long released - stopping motors");
    stopMotors();
  } else if (state == "tap" || state == BLINKER_CMD_BUTTON_TAP) {
    // 处理点击事件 - 短暂右转
    // Serial.println("Button tapped - brief right turn");
    turnRight();
    delay(200);  // 短暂转向200ms
    stopMotors();
    // Serial.println("Brief right turn completed");
  }
}

// 停止按钮回调函数 - 已移除
/*
void buttonSCallback(const String & state) {
  BLINKER_LOG("收到停止按钮: ", state);
  // Serial.printf("=== buttonSCallback called with state: %s ===\n", state.c_str());
  
  if (state == BLINKER_CMD_BUTTON_TAP || state == "tap") {
    Serial.println("Stop button pressed - stopping all motors");
    stopMotors();
  }
}
*/

// 自动模式按钮回调函数 - 已移除
/*
void buttonAutoCallback(const String & state) {
  BLINKER_LOG("收到自动模式按钮: ", state);
  // Serial.printf("=== buttonAutoCallback called with state: %s ===\n", state.c_str());
  
  if (state == BLINKER_CMD_BUTTON_TAP || state == "tap") {
    // 这里可以添加自动避障模式
    Serial.println("自动模式启动");
  }
}
*/

// 睡眠开关按钮回调函数
void buttonSleepCallback(const String & state) {
  BLINKER_LOG("收到睡眠开关: ", state);
  
  if (state == BLINKER_CMD_ON) {
    // 开关打开 - 进入睡眠模式
    Serial.println("睡眠模式激活，系统将进入深度睡眠...");
    
    // 停止所有电机
    stopMotors();
    
    // 关闭舵机
    servoActive = false;
    
    // 清空LCD显示
    if (lcdInitialized) {
      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_7x13B_tf);
      u8g2.drawStr(20, 30, "Sleep Mode");
      u8g2.sendBuffer();
      delay(2000);
      u8g2.clearDisplay();
    }
    
    // 发送睡眠确认消息
    Blinker.notify("ESP32进入睡眠模式");
    delay(1000);
    
    // 配置唤醒源 - 使用GPIO0作为唤醒按钮
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0); // 低电平唤醒
    
    // 进入深度睡眠
    Serial.println("进入深度睡眠模式...");
    esp_deep_sleep_start();
    
  } else if (state == BLINKER_CMD_OFF) {
    // 开关关闭 - 正常运行模式
    Serial.println("系统恢复正常运行模式");
    Blinker.notify("ESP32恢复正常运行");
  }
}

void sliderSpeedCallback(int32_t value) {
  BLINKER_LOG("收到速度滑块: ", value);
  setSpeedPercent(value);
  NumberSpeed.print(speedPercent);
}

void sliderServoCallback(int32_t value) {
  BLINKER_LOG("收到舵机滑块: ", value);
  // 将滑块值(0-100)映射到舵机角度(0-180)
  float angle = map(value, 0, 100, 0, 180);
  setServoAngle(angle);
  BLINKER_LOG("舵机角度设置为: ", angle);
}

// Blinker数据读取回调
void dataRead(const String & data) {
  // Serial.println("\n========== dataRead() START ==========");
  BLINKER_LOG("Blinker readString: ", data);
  
  // dataRead主要处理应用请求的数据，不包含距离（距离由实时刷新处理）
  
  // 更新速度显示
  NumberSpeed.print(speedPercent);
  // Serial.printf("dataRead() -> Speed sent: %d%%\n", speedPercent);
  
  // 更新IP地址显示
  String ipAddress = WiFi.localIP().toString();
  TextIP.print(ipAddress);
  // Serial.printf("dataRead() -> IP sent: %s\n", ipAddress.c_str());
  
  // Serial.println("=== dataRead() function completed ===");
}

// Blinker心跳回调 - 增强网络状态监控
void heartbeat() {
  Serial.println("\n---------- heartbeat() START ----------");
  
  // 网络状态监控和报告
  bool wifiConnected = (WiFi.status() == WL_CONNECTED);
  int rssi = WiFi.RSSI();
  
  Serial.printf("Network Status - WiFi: %s, RSSI: %d dBm, Stable: %s\n", 
                wifiConnected ? "Connected" : "Disconnected", 
                rssi, 
                networkStable ? "Yes" : "No");
  
  // 如果网络不稳定，尝试恢复
  if (!wifiConnected || rssi < -85) {
    Serial.println("Network issue detected in heartbeat, triggering recovery...");
    if (!checkWiFiConnection()) {
      reconnectWiFi();
    }
  }
  
  // 心跳主要处理系统状态，不包含距离（距离由实时刷新处理）
  Serial.printf("heartbeat() -> Speed: %d%%\n", speedPercent);
  NumberSpeed.print(speedPercent);
  
  // 更新IP地址显示
  String ipAddress = WiFi.localIP().toString();
  TextIP.print(ipAddress);
  Serial.printf("heartbeat() -> IP sent: %s\n", ipAddress.c_str());
  
  // 发送网络状态信息到Blinker（可选）
  if (networkStable) {
    // Serial.println("Network stable - heartbeat completed successfully");
  } else {
    // Serial.println("Network unstable - recovery actions taken");
  }
  
  // Serial.println("=== heartbeat() function completed ===");
}

// WiFiManager配置保存回调函数
void saveConfigCallback() {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

// 检查配网按钮是否被按下
bool checkConfigButton() {
  pinMode(CONFIG_BUTTON_PIN, INPUT_PULLUP);
  return digitalRead(CONFIG_BUTTON_PIN) == LOW;
}

// WiFi配网函数
void setupWiFiManager() {
  Serial.println("Starting WiFi configuration...");
  
  // 检查是否需要重置WiFi配置
  if (checkConfigButton()) {
    Serial.println("Config button pressed, resetting WiFi settings...");
    wifiManager.resetSettings();
    preferences.clear();
    delay(1000);
  }
  
  // 设置配置保存回调
  wifiManager.setSaveConfigCallback(saveConfigCallback);
  
  // 设置超时时间（3分钟）
  wifiManager.setConfigPortalTimeout(180);
  
  // 设置AP名称和密码
  wifiManager.setAPStaticIPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));
  
  // 设置配网门户的自定义参数
  wifiManager.setAPCallback([](WiFiManager *myWiFiManager) {
    Serial.println("进入配网模式");
    Serial.println("请连接到WiFi热点: ESP32-S3-Car-Config");
    Serial.println("密码: 12345678");
    Serial.println("然后在浏览器中打开: http://192.168.4.1");
    Serial.println("配置您的WiFi网络信息");
    
    // 配网模式下LED闪烁提示
    for (int i = 0; i < 10; i++) {
      setLED(true);
      delay(200);
      setLED(false);
      delay(200);
    }
  });
  
  // 设置配网门户的网页标题和说明
  wifiManager.setTitle("ESP32-S3智能小车WiFi配置");
  wifiManager.setConfigPortalBlocking(true);
  
  // 尝试连接WiFi，如果失败则启动配网门户
  if (!wifiManager.autoConnect("ESP32-S3-Car-Config", "12345678")) {
    Serial.println("配网超时或失败，重启设备...");
    // 重启设备
    ESP.restart();
  }
  
  // 如果到达这里，说明WiFi连接成功
  Serial.println("WiFi连接成功!");
  Serial.print("IP地址: ");
  Serial.println(WiFi.localIP());
  Serial.print("连接的WiFi: ");
  Serial.println(WiFi.SSID());
  
  // 保存配置到NVS
  if (shouldSaveConfig) {
    preferences.begin("wifi-config", false);
    preferences.putString("ssid", WiFi.SSID());
    preferences.putString("password", WiFi.psk());
    preferences.end();
    Serial.println("WiFi配置已保存到设备存储");
  }
  
  // 连接成功后LED常亮2秒
  setLED(true);
  delay(2000);
  setLED(false);
}

void setup() {
  Serial.begin(9600);  // 使用9600波特率，与HC-SR04例程保持一致
  Serial.println("ESP32-S3 Smart Car initialization started...");
  
  // 检查唤醒原因
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  switch(wakeup_reason) {
    case ESP_SLEEP_WAKEUP_EXT0:
      Serial.println("从外部信号唤醒 (GPIO0)");
      break;
    case ESP_SLEEP_WAKEUP_TIMER:
      Serial.println("从定时器唤醒");
      break;
    case ESP_SLEEP_WAKEUP_UNDEFINED:
    default:
      Serial.println("正常启动或重启");
      break;
  }
  
  // Configure watchdog timer for better stability
  esp_task_wdt_init(30, true);  // 30 second timeout, panic on timeout
  esp_task_wdt_add(NULL);       // Add current task to watchdog
  
  // GPIO initialization
  pinMode(MOTOR_IN1, OUTPUT);
  pinMode(MOTOR_IN2, OUTPUT);
  pinMode(MOTOR_IN3, OUTPUT);
  pinMode(MOTOR_IN4, OUTPUT);
  pinMode(ULTRASONIC_TRIG, OUTPUT);
  pinMode(ULTRASONIC_ECHO, INPUT);
  pinMode(LED_PIN, OUTPUT);
  // pinMode(BUZZER_PIN, OUTPUT);
  
  // 舵机引脚初始化
  pinMode(SERVO_PIN, OUTPUT); // 舵机控制引脚
  
  // Initialize PWM (ESP32-S3 optimized configuration)
  ledcSetup(0, 10000, 8);  // ENA: 10kHz, 8-bit resolution
  ledcSetup(1, 10000, 8);  // ENB: 10kHz, 8-bit resolution
  // 移除舵机LEDC设置 - 使用直接GPIO控制以获得更高精度
  
  ledcAttachPin(MOTOR_ENA, 0);
  ledcAttachPin(MOTOR_ENB, 1);
  
  // 舵机引脚设置为手动控制模式
  digitalWrite(SERVO_PIN, LOW);
  
  stopMotors();
  
  // Startup prompt
  setLED(true);
  //beep(100);
  delay(100);
  //beep(100);
  setLED(false);

  // WiFi配网设置 (使用WiFiManager)
  setupWiFiManager();
  
  /*
  // Web server configuration (commented out due to AsyncTCP conflict with Blinker)
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/html", webPage);
  });
  
  // 移除冲突的 /api 路由，因为它会拦截所有 /api/* 请求
  // server.on("/api", HTTP_GET, handleWebAPI);  // 注释掉这行
  
  // RESTful API endpoints for car control
  server.on("/api/forward", HTTP_POST, [](AsyncWebServerRequest *request){
    handleCarMovement(request, "forward");
  });
  
  server.on("/api/backward", HTTP_POST, [](AsyncWebServerRequest *request){
    handleCarMovement(request, "backward");
  });
  
  server.on("/api/left", HTTP_POST, [](AsyncWebServerRequest *request){
    handleCarMovement(request, "left");
  });
  
  server.on("/api/right", HTTP_POST, [](AsyncWebServerRequest *request){
    handleCarMovement(request, "right");
  });
  
  server.on("/api/leftShift", HTTP_POST, [](AsyncWebServerRequest *request){
    handleCarMovement(request, "leftside");
  });
  
  server.on("/api/rightShift", HTTP_POST, [](AsyncWebServerRequest *request){
    handleCarMovement(request, "rightside");
  });
  
  server.on("/api/leftTurn", HTTP_POST, [](AsyncWebServerRequest *request){
    handleCarMovement(request, "rotateleft");
  });
  
  server.on("/api/rightTurn", HTTP_POST, [](AsyncWebServerRequest *request){
    handleCarMovement(request, "rotateright");
  });
  
  server.on("/api/stop", HTTP_POST, [](AsyncWebServerRequest *request){
    handleCarMovement(request, "stop");
  });
  
  // Speed control endpoint
  server.on("/api/speed", HTTP_POST, [](AsyncWebServerRequest *request){
    handleSpeedControl(request);
  });
  
  // Servo control endpoint
  server.on("/api/servo", HTTP_POST, [](AsyncWebServerRequest *request){}, NULL, 
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
      handleServoControl(request, data, len, index, total);
    });
  
  // Status endpoint
  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request){
    handleStatusRequest(request);
  });
  
  // Enable CORS with comprehensive headers
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type, Authorization, X-Requested-With");
  DefaultHeaders::Instance().addHeader("Access-Control-Max-Age", "86400");
  
  // Handle preflight OPTIONS requests for all API endpoints
  // 移除冲突的 /api OPTIONS 路由
  // server.on("/api", HTTP_OPTIONS, [](AsyncWebServerRequest *request){
  //   request->send(200);
  // });
  server.on("/api/forward", HTTP_OPTIONS, [](AsyncWebServerRequest *request){
    request->send(200);
  });
  server.on("/api/backward", HTTP_OPTIONS, [](AsyncWebServerRequest *request){
    request->send(200);
  });
  server.on("/api/left", HTTP_OPTIONS, [](AsyncWebServerRequest *request){
    request->send(200);
  });
  server.on("/api/right", HTTP_OPTIONS, [](AsyncWebServerRequest *request){
    request->send(200);
  });
  server.on("/api/leftShift", HTTP_OPTIONS, [](AsyncWebServerRequest *request){
    request->send(200);
  });
  server.on("/api/rightShift", HTTP_OPTIONS, [](AsyncWebServerRequest *request){
    request->send(200);
  });
  server.on("/api/leftTurn", HTTP_OPTIONS, [](AsyncWebServerRequest *request){
    request->send(200);
  });
  server.on("/api/rightTurn", HTTP_OPTIONS, [](AsyncWebServerRequest *request){
    request->send(200);
  });
  server.on("/api/stop", HTTP_OPTIONS, [](AsyncWebServerRequest *request){
    request->send(200);
  });
  server.on("/api/speed", HTTP_OPTIONS, [](AsyncWebServerRequest *request){
    request->send(200);
  });
  server.on("/api/servo", HTTP_OPTIONS, [](AsyncWebServerRequest *request){
    request->send(200);
  });
  server.on("/api/status", HTTP_OPTIONS, [](AsyncWebServerRequest *request){
    request->send(200);
  });
  
  server.begin();
  Serial.println("Web server started successfully");
  */

  // Blinker initialization
  if (WiFi.status() == WL_CONNECTED) {
    // 获取保存的WiFi凭据用于Blinker初始化
    preferences.begin("wifi-config", true);
    String savedSSID = preferences.getString("ssid", "");
    String savedPassword = preferences.getString("password", "");
    preferences.end();
    
    if (savedSSID.length() > 0 && savedPassword.length() > 0) {
      Blinker.begin(auth, savedSSID.c_str(), savedPassword.c_str());
    } else {
      // 如果没有保存的凭据，使用当前连接的WiFi信息
      Blinker.begin(auth, WiFi.SSID().c_str(), WiFi.psk().c_str());
    }
    delay(500);
    
    // 绑定回调函数
    ButtonF.attach(buttonFCallback);
    ButtonB.attach(buttonBCallback);
    ButtonL.attach(buttonLCallback);
    ButtonR.attach(buttonRCallback);
    ButtonSleep.attach(buttonSleepCallback);  // 绑定睡眠开关回调
    // ButtonS.attach(buttonSCallback);  // 移除停止按钮
    // ButtonAuto.attach(buttonAutoCallback);  // 移除自动模式按钮
    SliderSpeed.attach(sliderSpeedCallback);
    SliderServo.attach(sliderServoCallback);
    
    // 绑定数据读取和心跳回调
    Blinker.attachData(dataRead);
    Blinker.attachHeartbeat(heartbeat);
    
    // 优化初始数据发送 - 减少延时
    delay(500); // 减少等待时间从1000ms到500ms
    
    // 批量发送初始数据
    float distance = getDistance();
    if (distance < 999.0) {
      NumberDistance.print(distance);
    }
    NumberSpeed.print(speedPercent);
    String ipAddress = WiFi.localIP().toString();
    TextIP.print(ipAddress);
    
    // 初始化网络状态变量
    networkStable = true;
    lastWiFiCheck = millis();
    lastBlinkerUpdate = millis();
    
    Serial.println("Blinker initialization completed");
    Serial.printf("IP Address: %s\n", ipAddress.c_str());
  }
  
  // LCD9648显示屏初始化
  Serial.println("Initializing LCD9648 display...");
  u8g2.begin();
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_7x13B_tf);  // 使用粗体字体增加浓度
  u8g2.drawStr(0, 14, "ESP32-S3 Car");
  u8g2.drawStr(0, 32, "System Ready");
  u8g2.sendBuffer();
  lcdInitialized = true;
  Serial.println("LCD9648 initialization completed");
  
  // Serial.println("=== ESP32-S3 Smart Car System Initialization Complete ===");
  Serial.printf("Memory usage: %d KB\n", (ESP.getHeapSize() - ESP.getFreeHeap()) / 1024);
  
  // Completion prompt
  for (int i = 0; i < 3; i++) {
    setLED(true);
    //beep(50);
    delay(100);
    setLED(false);
    delay(100);
  }
}

void loop() {
  // Add watchdog reset at the beginning of loop
  esp_task_wdt_reset();
  
  // 检查串口命令
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    
    if (command == "reset_wifi" || command == "RESET_WIFI") {
      Serial.println("收到WiFi重置命令，清除WiFi配置...");
      wifiManager.resetSettings();
      preferences.begin("wifi-config", false);
      preferences.clear();
      preferences.end();
      Serial.println("WiFi配置已清除，设备将重启进入配网模式...");
      delay(1000);
      ESP.restart();
    } else if (command == "wifi_status" || command == "WIFI_STATUS") {
      Serial.println("=== WiFi状态信息 ===");
      Serial.printf("连接状态: %s\n", WiFi.status() == WL_CONNECTED ? "已连接" : "未连接");
      if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("WiFi名称: %s\n", WiFi.SSID().c_str());
        Serial.printf("IP地址: %s\n", WiFi.localIP().toString().c_str());
        Serial.printf("信号强度: %d dBm\n", WiFi.RSSI());
      }
      Serial.println("==================");
    } else if (command == "help" || command == "HELP") {
      Serial.println("=== 可用命令 ===");
      Serial.println("reset_wifi  - 重置WiFi配置");
      Serial.println("wifi_status - 查看WiFi状态");
      Serial.println("help        - 显示帮助信息");
      Serial.println("===============");
    }
  }
  
  // Enhanced network health monitoring and auto-recovery
  unsigned long currentTime = millis();
  if (currentTime - lastWiFiCheck > WIFI_CHECK_INTERVAL) {
    lastWiFiCheck = currentTime;
    
    // Comprehensive network health check
    bool wifiConnected = (WiFi.status() == WL_CONNECTED);
    int currentRSSI = WiFi.RSSI();
    
    // Update network stability status
    if (wifiConnected && currentRSSI > -85) {
      if (!networkStable) {
        Serial.println("🔄 Network recovered - marking as stable");
        networkStable = true;
        wifiReconnectAttempts = 0; // Reset reconnect attempts
      }
    } else {
      if (networkStable) {
        Serial.println("⚠️ Network degraded - marking as unstable");
        networkStable = false;
      }
      
      // Trigger recovery if needed
      if (!checkWiFiConnection()) {
        Serial.println("🔧 Initiating network recovery...");
        reconnectWiFi();
      }
    }
    
    // Log network status periodically
    Serial.printf("📊 Network Health: WiFi=%s, RSSI=%d dBm, Stable=%s, Attempts=%d\n",
                  wifiConnected ? "OK" : "FAIL", 
                  currentRSSI, 
                  networkStable ? "YES" : "NO",
                  wifiReconnectAttempts);
  }
  
  // Optimized Blinker data updates (reduced frequency)
  if (currentTime - lastBlinkerUpdate > BLINKER_UPDATE_INTERVAL && networkStable) {
    lastBlinkerUpdate = currentTime;
    
    // Batch update all sensor data to reduce network calls
    float distance = getDistance();
    if (distance >= 2 && distance < 600.0) {
      NumberDistance.print(distance);
      Serial.printf("✅ 距离数据已发送: %.2f cm\n", distance);
    }
    
    // Update speed info less frequently
    static int lastSpeedSent = -1;
    if (speedPercent != lastSpeedSent) {
      NumberSpeed.print(speedPercent);
      lastSpeedSent = speedPercent;
      Serial.printf("✅ 速度数据已发送: %d%%\n", speedPercent);
    }
    
    // Update IP only when changed
    static String lastIPSent = "";
    String currentIP = WiFi.localIP().toString();
    if (currentIP != lastIPSent && WiFi.status() == WL_CONNECTED) {
      TextIP.print(currentIP);
      lastIPSent = currentIP;
      Serial.printf("✅ IP地址已发送: %s\n", currentIP.c_str());
    }
  }
  
  // LCD9648显示屏更新
  if (currentTime - lastLCDUpdate > LCD_UPDATE_INTERVAL) {
    lastLCDUpdate = currentTime;
    updateLCDDisplay();
  }
  
  // Blinker运行 - 仅在网络稳定时运行以避免错误
  if (networkStable && WiFi.status() == WL_CONNECTED) {
    Blinker.run();
  } else {
    // 网络不稳定时，减少Blinker调用频率
    static unsigned long lastBlinkerAttempt = 0;
    if (currentTime - lastBlinkerAttempt > 1000) { // 每秒尝试一次
      lastBlinkerAttempt = currentTime;
      Serial.println("⚠️ Network unstable - skipping Blinker.run()");
      
      // 尝试轻量级的网络检查
      if (WiFi.status() == WL_CONNECTED) {
        Blinker.run();
      }
    }
  }
  
  // Minimal delay for better responsiveness
  delay(5);
}
