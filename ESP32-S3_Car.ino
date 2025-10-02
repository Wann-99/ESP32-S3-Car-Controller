// Blinker mode definition (must be before including Blinker.h)
// #define BLINKER_WIFI

#include <WiFi.h>
#include <ESPAsyncWebServer.h>
// #include <Blinker.h>
#include "driver/ledc.h"
#include "esp_task_wdt.h"  // Add watchdog timer header

// WiFi Configuration
const char* ssid = "CU_204";
const char* password = "wj990518.";

// Blinker Configuration
// const char* auth = "88897b5b6069";

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
#define BUZZER_PIN        7   // GPIO7 - Buzzer

// Global variables
int motorSpeed = 200;        // Motor speed (50-255)
int speedPercent = 78;       // Speed percentage (20-100%)
unsigned long lastWebUpdate = 0;
unsigned long lastDistanceCheck = 0;
const float SAFE_DISTANCE = 10.0;  // Reduced safe distance from 15cm to 10cm for better mobility

// Blinker button definitions
/*
BlinkerButton ButtonF("btn-f");
BlinkerButton ButtonB("btn-b");
BlinkerButton ButtonL("btn-l");
BlinkerButton ButtonR("btn-r");
BlinkerButton ButtonS("btn-s");
BlinkerButton ButtonAuto("btn-auto");
BlinkerSlider SliderSpeed("slider-speed");
BlinkerNumber NumberDistance("distance");
*/

// Web server
AsyncWebServer server(80);

// System restart function
void systemRestart(const char* reason) {
  Serial.printf("%s, system will restart in 3 seconds...\n", reason);
  delay(1000);
  // Blinker.notify(String(reason) + ", system will restart");
  delay(2000);
  ESP.restart();
}

// Status indication
void setLED(bool state) {
  digitalWrite(LED_PIN, state);
}

// Buzzer control
void beep(int duration = 100) {
  digitalWrite(BUZZER_PIN, HIGH);
  delay(duration);
  digitalWrite(BUZZER_PIN, LOW);
}

// Enhanced motor control functions with safety check
bool checkSafeToMoveForward() {
  float distance = getDistance();
  
  // Add debug output to help diagnose the issue
  Serial.print("Distance reading: ");
  Serial.print(distance);
  Serial.print(" cm, Safe distance: ");
  Serial.print(SAFE_DISTANCE);
  Serial.println(" cm");
  
  // Check for invalid readings (999.0 indicates sensor error)
  if (distance >= 999.0) {
    Serial.println("Warning: Invalid distance reading, allowing movement");
    return true;  // Allow movement if sensor reading is invalid
  }
  
  bool isSafe = distance > SAFE_DISTANCE;
  if (!isSafe) {
    Serial.println("Movement blocked by obstacle");
  }
  
  return isSafe;
}

// Motor control functions (performance optimized)
void setMotorSpeed(int leftSpeed, int rightSpeed) {
  // Batch set GPIO status to reduce function call overhead
  digitalWrite(MOTOR_IN1, leftSpeed > 0 ? HIGH : LOW);
  digitalWrite(MOTOR_IN2, leftSpeed > 0 ? LOW : HIGH);
  digitalWrite(MOTOR_IN3, rightSpeed > 0 ? HIGH : LOW);
  digitalWrite(MOTOR_IN4, rightSpeed > 0 ? LOW : HIGH);
  
  // Use ESP32-S3 high-speed PWM
  ledcWrite(0, abs(leftSpeed));   // ENA: PWM channel 0
  ledcWrite(1, abs(rightSpeed));  // ENB: PWM channel 1
}

// Motor direction control with safety check
void moveForward() {
  if (!checkSafeToMoveForward()) {
    Serial.println("Forward blocked - obstacle detected");
    beep(100);
    return;
  }
  setMotorSpeed(motorSpeed, motorSpeed);
  Serial.println("Forward");
}

void moveBackward() {
  setMotorSpeed(-motorSpeed, -motorSpeed);
  Serial.println("Backward");
}

void turnLeft() {
  setMotorSpeed(motorSpeed, -motorSpeed);
  Serial.println("Turn Left");
}

void turnRight() {
  setMotorSpeed(-motorSpeed, motorSpeed);
  Serial.println("Turn Right");
}

// Mecanum wheel strafe movement functions (left/right side movement)
void moveLeftSide() {
  // Mecanum wheel left strafe: 
  // Front-left and rear-right wheels forward, front-right and rear-left wheels backward
  setMotorSpeed(-motorSpeed, motorSpeed);
  Serial.println("Mecanum Strafe Left");
}

void moveRightSide() {
  // Mecanum wheel right strafe:
  // Front-right and rear-left wheels forward, front-left and rear-right wheels backward  
  setMotorSpeed(motorSpeed, -motorSpeed);
  Serial.println("Mecanum Strafe Right");
}

// In-place rotation functions - 180 degree turn
void rotateLeft() {
  setMotorSpeed(-motorSpeed, motorSpeed);
  Serial.println("Rotate Left 180° (In-place)");
  
  // Calculate rotation time based on speed
  // Approximate time for 180° rotation (adjust based on testing)
  int rotationTime = map(motorSpeed, 51, 255, 2000, 1000); // 1-2 seconds based on speed
  
  delay(rotationTime);
  stopMotors();
  Serial.println("Left 180° rotation completed");
}

void rotateRight() {
  setMotorSpeed(motorSpeed, -motorSpeed);
  Serial.println("Rotate Right 180° (In-place)");
  
  // Calculate rotation time based on speed
  // Approximate time for 180° rotation (adjust based on testing)
  int rotationTime = map(motorSpeed, 51, 255, 2000, 1000); // 1-2 seconds based on speed
  
  delay(rotationTime);
  stopMotors();
  Serial.println("Right 180° rotation completed");
}

void stopMotors() {
  setMotorSpeed(0, 0);
  Serial.println("Stop");
}

// Speed control functions
void setSpeedPercent(int percent) {
  speedPercent = constrain(percent, 20, 100);
  motorSpeed = map(speedPercent, 20, 100, 51, 255);
  Serial.printf("Speed set to %d%% (%d/255)\n", speedPercent, motorSpeed);
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
  // 触发超声波脉冲 (严格按照HC-SR04时序)
  digitalWrite(ULTRASONIC_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(ULTRASONIC_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(ULTRASONIC_TRIG, LOW);
  
  // 测量回波时间，设置60ms超时（对应约10米最大距离）
  Time_Echo_us = pulseIn(ULTRASONIC_ECHO, HIGH, 60000);
  
  // 严格的超时和有效性检测 (参考HC-SR04例程)
  if (Time_Echo_us == 0 || Time_Echo_us < 1 || Time_Echo_us > 60000) {
    return 999.0; // 返回最大值表示超出范围或错误
  }
  
  // HC-SR04超声波距离计算 (完全按照例程公式)
  // 距离(mm) = Time_Echo_us * 34 / 2 / 100 (转换为cm)
  // 为保持精度，使用X100计算
  if ((Time_Echo_us < 60000) && (Time_Echo_us > 1)) {
    Len_mm_X100 = (Time_Echo_us * 34) / 2;
    Len_Integer = Len_mm_X100 / 100;
    Len_Fraction = Len_mm_X100 % 100;
    
    Serial.print("Present Length is: ");
    Serial.print(Len_Integer, DEC);
    Serial.print(".");
    if (Len_Fraction < 10) {
      Serial.print("0");
    }
    Serial.print(Len_Fraction, DEC);
    Serial.println("mm");
    
    // 转换为cm并返回
    float distance_cm = Len_Integer / 10.0 + Len_Fraction / 1000.0;
    
    // 范围检查 (2cm - 400cm)
    if (distance_cm < 2.0 || distance_cm > 400.0) {
      return 999.0;
    }
    
    return distance_cm;
  }
  
  return 999.0; // 测量失败
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
  
  // 发送精确控制脉冲序列
  for (int i = 0; i < 10; i++) {  // 增加脉冲数量确保精确定位
    servopulse(angle);
    delay(20);  // 20ms间隔
    esp_task_wdt_reset();  // 重置看门狗
  }
  
  // 添加稳定延时
  delay(100);
  
  Serial.print("Present Length is: ");
  Serial.print(angle, 2);
  Serial.println(" degrees");
}

// 停止舵机PWM信号
void stopServo() {
  digitalWrite(SERVO_PIN, LOW);
  servoActive = false;
  Serial.println("Servo stopped");
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

// Optimized Web API handling
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

// Optimized Web control page
const char* webPage = R"HTML(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ESP32-S3 Smart Car Control</title>
    <style>
        body { font-family: Arial; text-align: center; margin: 20px; background: #f0f0f0; }
        .container { max-width: 400px; margin: 0 auto; background: white; padding: 20px; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }
        .control-btn { width: 80px; height: 80px; margin: 5px; font-size: 16px; border: none; border-radius: 10px; cursor: pointer; }
        .forward { background: #4CAF50; color: white; }
        .backward { background: #f44336; color: white; }
        .left { background: #2196F3; color: white; }
        .right { background: #FF9800; color: white; }
        .stop { background: #9E9E9E; color: white; }
        .strafe { background: #4CAF50; color: white; }
        .rotate { background: #E91E63; color: white; }
        .speed-control { margin: 20px 0; }
        .status { margin: 10px 0; padding: 10px; background: #e8f5e8; border-radius: 5px; }
        #distance { font-size: 24px; color: #2196F3; }
        .movement-section { margin: 20px 0; padding: 15px; border: 2px solid #ddd; border-radius: 10px; }
        .movement-title { font-weight: bold; margin-bottom: 10px; color: #333; }
        .servo-buttons { 
            display: flex; 
            justify-content: center; 
            align-items: center; 
            margin-top: 10px; 
            gap: 15px; 
        }
        .servo-adjuster {
            display: flex;
            border: 2px solid #607D8B;
            border-radius: 25px;
            overflow: hidden;
        }
        .servo-buttons .control-btn { 
            width: 45px; 
            height: 45px; 
            font-size: 20px; 
            font-weight: bold;
            background: #607D8B; 
            color: white; 
            border: none;
            cursor: pointer;
            transition: background-color 0.2s;
        }
        .servo-buttons .control-btn:hover {
            background: #546E7A;
        }
        .servo-buttons .control-btn:active {
            background: #455A64;
        }
        .servo-buttons .control-btn.decrease {
            border-radius: 0;
        }
        .servo-buttons .control-btn.increase {
            border-radius: 0;
        }
        .servo-buttons input[type="number"] { 
            text-align: center; 
            border: 1px solid #ccc; 
            border-radius: 5px; 
            padding: 8px; 
            font-size: 14px;
        }
    </style>
</head>
<body>
    <div class="container">
        <h2>ESP32-S3 Smart Car</h2>
        
        <div class="status">
            <div>Distance: <span id="distance">--</span> cm</div>
            <div>Safe Distance: <span id="safeDistance">20</span> cm</div>
            <div>Speed: <span id="speedPercent">50</span>%</div>
        </div>
        
        <div class="movement-section">
            <div class="movement-title">基本运动控制</div>
            <div style="margin: 10px 0;">
                <div>
                    <button class="control-btn forward" onmousedown="startMovement('forward')" onmouseup="stopMovement()" onmouseleave="stopMovement()">↑<br>前进</button>
                </div>
                <div>
                    <button class="control-btn left" onmousedown="startMovement('left')" onmouseup="stopMovement()" onmouseleave="stopMovement()">↰<br>左转</button>
                    <button class="control-btn stop" onclick="sendCommand('stop')">■<br>停止</button>
                    <button class="control-btn right" onmousedown="startMovement('right')" onmouseup="stopMovement()" onmouseleave="stopMovement()">↱<br>右转</button>
                </div>
                <div>
                    <button class="control-btn backward" onmousedown="startMovement('backward')" onmouseup="stopMovement()" onmouseleave="stopMovement()">↓<br>后退</button>
                </div>
            </div>
        </div>
        
        <div class="movement-section">
            <div class="movement-title">麦轮高级运动控制</div>
            <div style="margin: 10px 0;">
                <div>
                    <button class="control-btn strafe" onmousedown="startMovement('leftside')" onmouseup="stopMovement()" onmouseleave="stopMovement()">←<br>麦轮左平移</button>
                    <button class="control-btn strafe" onmousedown="startMovement('rightside')" onmouseup="stopMovement()" onmouseleave="stopMovement()">→<br>麦轮右平移</button>
                </div>
                <div style="margin-top: 10px;">
                    <button class="control-btn rotate" onmousedown="startMovement('rotateleft')" onmouseup="stopMovement()" onmouseleave="stopMovement()">↺<br>左掉头</button>
                    <button class="control-btn rotate" onmousedown="startMovement('rotateright')" onmouseup="stopMovement()" onmouseleave="stopMovement()">↻<br>右掉头</button>
                </div>
            </div>
        </div>
        
        <div class="speed-control">
            <label>速度控制 (%): </label>
            <input type="range" id="speedSlider" min="20" max="100" value="50" onchange="setSpeedPercent(this.value)">
            <span id="speedValue">50</span>%
        </div>
        
        <div class="speed-control">
            <label>舵机控制: </label>
            <input type="range" id="servoSlider" min="0" max="180" step="0.18" value="90" onchange="setServo(this.value)">
            <div>角度: <span id="servoAngle">90</span>°</div>
            <div class="servo-buttons">
                <div class="servo-adjuster">
                    <button class="control-btn decrease" onmousedown="startAdjust(-1)" onmouseup="stopAdjust()" onmouseleave="stopAdjust()">-</button>
                    <button class="control-btn increase" onmousedown="startAdjust(1)" onmouseup="stopAdjust()" onmouseleave="stopAdjust()">+</button>
                </div>
                <input type="number" id="servoInput" min="0" max="180" step="0.18" value="90" onchange="setServoFromInput(this.value)" style="width: 60px; margin: 0 5px;" placeholder="角度">
            </div>
        </div>
    </div>

    <script>
        // 点动控制变量
        let isMoving = false;
        let currentAction = null;
        
        function sendCommand(action) {
            fetch('/api?action=' + action)
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
            fetch('/api?action=speedpercent&value=' + value);
        }
        
        function updateStatus() {
            fetch('/api?action=distance')
                .then(response => response.json())
                .then(data => {
                    if (data.distance !== undefined) {
                        document.getElementById('distance').textContent = data.distance;
                    }
                })
                .catch(error => console.error('Error:', error));
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
            const url = `${ESP32_IP}/api?action=servo&value=${angle}`;
            fetch(url, {
                method: 'GET',
                timeout: 3000 // 3秒超时
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

void setup() {
  Serial.begin(9600);  // 使用9600波特率，与HC-SR04例程保持一致
  Serial.println("ESP32-S3 Smart Car initialization started...");
  
  // GPIO initialization
  pinMode(MOTOR_IN1, OUTPUT);
  pinMode(MOTOR_IN2, OUTPUT);
  pinMode(MOTOR_IN3, OUTPUT);
  pinMode(MOTOR_IN4, OUTPUT);
  pinMode(ULTRASONIC_TRIG, OUTPUT);
  pinMode(ULTRASONIC_ECHO, INPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  
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
  beep(100);
  delay(100);
  beep(100);
  setLED(false);

  // WiFi connection (optimized connection logic)
  Serial.printf("Connecting to WiFi: %s\n", ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 30) {
    delay(1000);
    Serial.print(".");
    setLED(retries % 2);
    retries++;
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\nWiFi connection failed, starting AP mode");
    WiFi.mode(WIFI_AP);
    WiFi.softAP("ESP32-S3-Car", "12345678");
    Serial.printf("AP mode started, IP: %s\n", WiFi.softAPIP().toString().c_str());
  } else {
    Serial.printf("\nWiFi connected successfully, IP address: %s\n", WiFi.localIP().toString().c_str());
  }
  
  // Web server configuration
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/html", webPage);
  });
  
  server.on("/api", HTTP_GET, handleWebAPI);
  
  // Enable CORS with comprehensive headers
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type, Authorization, X-Requested-With");
  DefaultHeaders::Instance().addHeader("Access-Control-Max-Age", "86400");
  
  // Handle preflight OPTIONS requests
  server.on("/api", HTTP_OPTIONS, [](AsyncWebServerRequest *request){
    request->send(200);
  });
  
  server.begin();
  Serial.println("Web server started successfully");

  // Blinker initialization
  /*
  if (WiFi.status() == WL_CONNECTED) {
    Blinker.begin(auth, ssid, password);
    delay(500);
    
    // Bind callback functions
    ButtonF.attach(buttonFCallback);
    ButtonB.attach(buttonBCallback);
    ButtonL.attach(buttonLCallback);
    ButtonR.attach(buttonRCallback);
    ButtonS.attach(buttonSCallback);
    ButtonAuto.attach(buttonAutoCallback);
    SliderSpeed.attach(sliderSpeedCallback);
    
    Serial.println("Blinker initialization completed");
  }
  */
  
  Serial.println("=== ESP32-S3 Smart Car System Initialization Complete ===");
  Serial.printf("Memory usage: %d KB\n", (ESP.getHeapSize() - ESP.getFreeHeap()) / 1024);
  
  // Completion prompt
  for (int i = 0; i < 3; i++) {
    setLED(true);
    beep(50);
    delay(100);
    setLED(false);
    delay(100);
  }
}

void loop() {
  // Add watchdog reset at the beginning of loop
  esp_task_wdt_reset();
  
  // WiFi connection check (less frequent)
  static unsigned long lastWiFiCheck = 0;
  if (millis() - lastWiFiCheck > 60000) { // Check every 60 seconds
    lastWiFiCheck = millis();
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi connection lost, attempting reconnection...");
      WiFi.reconnect();
    }
    // Reset watchdog after WiFi operations
    esp_task_wdt_reset();
  }
  
  // Minimal delay for better web responsiveness
  delay(5);
}