# ESP32-S3 智能小车控制系统

## 📖 项目简介

基于ESP32-S3开发的智能小车控制系统，支持麦克纳姆轮全向移动、超声波避障、舵机云台控制和实时视频监控。提供Web界面远程控制，具备完整的RESTful API接口。

## ✨ 主要特性

### 🚗 运动控制
- **麦克纳姆轮全向移动**：前进、后退、左右平移、原地旋转
- **精确速度控制**：20%-100%可调速度范围
- **平滑运动**：优化的PWM控制算法

### 🎯 传感器系统
- **超声波测距**：HC-SR04传感器，实时距离检测
- **智能避障**：可配置安全距离（默认10cm）
- **状态指示**：LED和蜂鸣器反馈

### 📹 视频监控
- **实时视频流**：支持ESP32-CAM等设备
- **多格式兼容**：自动适配常见视频流格式
- **灵活配置**：可自定义摄像头IP地址

### 🌐 Web控制界面
- **响应式设计**：支持手机、平板、电脑
- **实时状态显示**：距离、速度、舵机角度
- **直观操作**：游戏手柄式控制界面

### 🔧 舵机云台
- **精确角度控制**：0-180度范围
- **平滑调节**：防抖动优化
- **实时反馈**：角度状态显示

## 🔌 硬件连接

### ESP32-S3 引脚配置

| 功能 | 引脚 | 说明 |
|------|------|------|
| **电机驱动** | | |
| 左电机方向1 | GPIO1 | MOTOR_IN1 |
| 左电机方向2 | GPIO2 | MOTOR_IN2 |
| 右电机方向1 | GPIO42 | MOTOR_IN3 |
| 右电机方向2 | GPIO41 | MOTOR_IN4 |
| 左电机使能 | GPIO3 | MOTOR_ENA (PWM) |
| 右电机使能 | GPIO40 | MOTOR_ENB (PWM) |
| **传感器** | | |
| 超声波触发 | GPIO4 | ULTRASONIC_TRIG |
| 超声波回响 | GPIO5 | ULTRASONIC_ECHO |
| **舵机控制** | | |
| 舵机信号 | GPIO6 | SERVO_PIN (PWM) |
| **指示器** | | |
| 状态LED | GPIO48 | LED_PIN |
| 蜂鸣器 | GPIO7 | BUZZER_PIN |

### 硬件要求

- **主控板**：ESP32-S3 DevKit
- **电机驱动**：L298N或类似双H桥驱动器
- **电机**：4个直流减速电机（麦克纳姆轮）
- **传感器**：HC-SR04超声波测距模块
- **舵机**：SG90或类似9g舵机
- **摄像头**：ESP32-CAM（可选）
- **电源**：7.4V锂电池组

## 🚀 快速开始

### 1. 环境准备

```bash
# Arduino IDE 库依赖
- WiFi (ESP32内置)
- ESPAsyncWebServer
- AsyncTCP
```

### 2. 代码配置

修改WiFi配置（第12-13行）：
```cpp
const char* ssid = "你的WiFi名称";
const char* password = "你的WiFi密码";
```

### 3. 上传代码

1. 在Arduino IDE中打开 `ESP32-S3_Car.ino`
2. 选择开发板：ESP32S3 Dev Module
3. 配置参数：
   - CPU Frequency: 240MHz
   - Flash Size: 16MB
   - Partition Scheme: Default 4MB
4. 点击上传

### 4. 访问控制界面

上传成功后，串口监视器会显示IP地址：
```
WiFi connected successfully, IP address: 192.168.1.xxx
```

在浏览器中访问该IP地址即可使用控制界面。

## 🎮 控制界面说明

### 主要功能区域

1. **视频监控区**：实时显示摄像头画面
2. **运动控制区**：方向控制按钮
3. **状态显示区**：距离、速度、舵机角度
4. **参数调节区**：速度和舵机角度滑块

### 控制按钮

| 按钮 | 功能 | 说明 |
|------|------|------|
| ↑ | 前进 | 向前移动 |
| ↓ | 后退 | 向后移动 |
| ← | 左转 | 原地左转 |
| → | 右转 | 原地右转 |
| ↖ | 左前 | 左前方移动 |
| ↗ | 右前 | 右前方移动 |
| ↙ | 左后 | 左后方移动 |
| ↘ | 右后 | 右后方移动 |
| ⏸ | 停止 | 停止所有运动 |

## 🔗 API接口文档

### 运动控制接口

所有运动控制接口均使用 `POST` 方法：

```http
POST /api/forward     # 前进
POST /api/backward    # 后退
POST /api/left        # 左转
POST /api/right       # 右转
POST /api/leftShift   # 左平移
POST /api/rightShift  # 右平移
POST /api/leftTurn    # 左旋转
POST /api/rightTurn   # 右旋转
POST /api/stop        # 停止
```

**响应格式**：
```json
{
  "status": "ok",
  "action": "forward"
}
```

### 速度控制接口

```http
POST /api/speed
Content-Type: application/x-www-form-urlencoded

value=80  # 速度百分比 (20-100)
```

**响应格式**：
```json
{
  "status": "ok",
  "action": "speed",
  "speed": 204,
  "percent": 80
}
```

### 舵机控制接口

```http
POST /api/servo
Content-Type: application/json

{
  "value": 90.0  # 角度 (0-180)
}
```

**响应格式**：
```json
{
  "status": "ok",
  "action": "servo",
  "servo_angle": 90.00
}
```

### 状态查询接口

```http
GET /api/status
```

**响应格式**：
```json
{
  "status": "ok",
  "distance": 25.30,
  "speed": 204,
  "percent": 80,
  "servo_angle": 90.00,
  "safe_distance": 10.0
}
```

**状态说明**：
- `ok`: 正常状态
- `warning`: 距离过近警告
- `error`: 传感器错误

## ⚙️ 高级配置

### 安全距离调整

修改第33行的安全距离设置：
```cpp
const float SAFE_DISTANCE = 10.0;  // 单位：厘米
```

### PWM频率优化

修改第1578-1579行的PWM配置：
```cpp
ledcSetup(0, 10000, 8);  // 频率10kHz，8位精度
ledcSetup(1, 10000, 8);
```

### 串口波特率

默认使用9600波特率，与HC-SR04兼容：
```cpp
Serial.begin(9600);
```

## 🛠️ 故障排除

### 常见问题

1. **WiFi连接失败**
   - 检查SSID和密码是否正确
   - 确认WiFi信号强度
   - 系统会自动启动AP模式作为备用

2. **电机不转动**
   - 检查电源电压（推荐7.4V）
   - 确认L298N驱动器连接
   - 验证引脚配置

3. **超声波无读数**
   - 检查TRIG和ECHO引脚连接
   - 确认传感器供电（5V）
   - 查看串口调试信息

4. **舵机抖动**
   - 检查电源稳定性
   - 调整PWM参数
   - 确认舵机规格匹配

5. **网页无法访问**
   - 确认设备在同一网络
   - 检查防火墙设置
   - 查看串口输出的IP地址

### 调试信息

启用串口监视器（9600波特率）查看详细调试信息：
- WiFi连接状态
- 超声波测距数据
- API请求响应
- 系统运行状态

## 📁 项目结构

```
ESP32-S3_Car/
├── ESP32-S3_Car.ino          # 主程序文件
├── README.md                  # 项目文档
├── api_test.html             # API测试工具
├── test_status_api.html      # 状态API测试工具
└── data/                     # 数据文件目录
```

## 🔄 版本更新

### 最新优化

- ✅ 修复API路由冲突问题
- ✅ 优化超声波测距精度
- ✅ 改进舵机控制稳定性
- ✅ 增强Web界面响应性
- ✅ 添加详细调试信息

### 开发计划

- 🔄 添加IMU姿态传感器
- 🔄 实现自动巡航模式
- 🔄 增加语音控制功能
- 🔄 开发手机APP

## 📄 许可证

本项目采用MIT许可证，详见LICENSE文件。

## 🤝 贡献

欢迎提交Issue和Pull Request来改进项目！

## 📞 技术支持

如有问题，请通过以下方式联系：
- 提交GitHub Issue
- 查看串口调试信息
- 参考故障排除章节

---

**享受你的ESP32-S3智能小车控制体验！** 🚗✨