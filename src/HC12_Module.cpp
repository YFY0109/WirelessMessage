/**
 * @file HC12_Module.cpp
 * @brief HC-12无线串口通信模块的C++实现
 *
 * 基于HC-12用户手册V2.6/V3.0实现AT指令控制、数据传输等功能
 */

#include "HC12_Module.h"
#include <Arduino.h>
#include <HardwareSerial.h>

/**
 * @brief 初始化HC-12模块
 * @param setPin SET引脚号
 * @param uartNum 使用哪个UART (1或2)
 * @param rxPin RX引脚号
 * @param txPin TX引脚号
 * @param baudRate 串口波特率
 * @return 初始化是否成功
 */
bool HC12Module::init(int setPin, int uartNum, int rxPin, int txPin, int baudRate)
{
    this->setPin = setPin;
    this->uartNum = uartNum;
    // 保存引脚与波特率以便以后本地重配置
    this->rxPin = rxPin;
    this->txPin = txPin;
    this->currentBaud = baudRate;

    // 初始化SET引脚
    pinMode(setPin, OUTPUT);
    setMode(COMM_MODE); // 默认设置为通信模式

    // 初始化串口
    if (uartNum == 1)
    {
        Serial1.begin(baudRate, SERIAL_8N1, rxPin, txPin);
        hc12Serial = &Serial1;
    }
    else if (uartNum == 2)
    {
        Serial2.begin(baudRate, SERIAL_8N1, rxPin, txPin);
        hc12Serial = &Serial2;
    }
    else
    {
        return false;
    }

    delay(100); // 等待模块稳定

    // 测试连接
    return testConnection();
}

/**
 * @brief 本地重新配置与 HC-12 相连的 UART（在模块波特率改变后使用）
 */
bool HC12Module::reconfigureLocalSerial(int baudRate)
{
    // 保存当前波特率
    this->currentBaud = baudRate;
    if (this->uartNum == 1)
    {
        Serial1.begin(baudRate, SERIAL_8N1, this->rxPin, this->txPin);
        hc12Serial = &Serial1;
    }
    else if (this->uartNum == 2)
    {
        Serial2.begin(baudRate, SERIAL_8N1, this->rxPin, this->txPin);
        hc12Serial = &Serial2;
    }
    else
    {
        return false;
    }
    delay(80); // 等待 UART 与模块稳定
    return true;
}

/**
 * @brief 进入AT指令模式
 */
void HC12Module::setMode(Mode mode)
{
    // NOTE: Many HC-12 modules expect SET = LOW to enter AT mode and HIGH for communication mode.
    // Swap the levels so LOW -> AT_MODE, HIGH -> COMM_MODE.
    if (mode == AT_MODE)
    {
        digitalWrite(setPin, LOW);
        delay(40); // 根据手册要求等待40ms
    }
    else
    {
        digitalWrite(setPin, HIGH);
        delay(80); // 根据手册要求等待80ms
    }
    currentMode = mode;
}

/**
 * @brief 发送AT指令
 * @param command AT指令字符串
 * @param timeout 响应超时时间(ms)
 * @return 模块响应字符串
 */
String HC12Module::sendATCommand(const String &command, int timeout)
{
    // 如果当前不在 AT 模式，则进入 AT 模式并记录我们切换过来；
    bool switchedToAT = false;
    if (currentMode != AT_MODE)
    {
        setMode(AT_MODE);
        switchedToAT = true;
    }
    // 清空接收缓冲区
    while (hc12Serial->available())
    {
        hc12Serial->read();
    }

    // 发送指令（以\r\n结尾）
    hc12Serial->print(command);
    hc12Serial->print("\r\n");

    // 等待响应
    String response = "";
    unsigned long startTime = millis();

    while (millis() - startTime < timeout)
    {
        if (hc12Serial->available())
        {
            char c = hc12Serial->read();
            // 过滤掉回车换行符
            if (c != '\r' && c != '\n')
            {
                response += c;
            }
        }
        delay(1);
    }

    // 只有当本调用切换到了 AT 模式时，才在返回前切回通信模式；如果外部已经在 AT 模式（如设置界面），
    // 则不自动切换，等待外部显式退出 AT 模式以生效设置。
    if (switchedToAT)
    {
        setMode(COMM_MODE);
    }
    return response;
}

/**
 * @brief 测试模块连接
 * @return 是否连接成功
 */
bool HC12Module::testConnection()
{
    String response = sendATCommand("AT");
    return response.indexOf("OK") >= 0;
}

/**
 * @brief 查询固件版本
 * @return 固件版本信息
 */
String HC12Module::getVersion()
{
    return sendATCommand("AT+V");
}

/**
 * @brief 查询当前波特率
 * @return 当前波特率字符串
 */
String HC12Module::getBaudRate()
{
    return sendATCommand("AT+RB");
}

/**
 * @brief 设置波特率
 * @param baudRate 要设置的波特率 (1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200)
 * @return 设置是否成功
 */
bool HC12Module::setBaudRate(int baudRate)
{
    String cmd;
    switch (baudRate)
    {
    case 1200:
        cmd = "AT+B1200";
        break;
    case 2400:
        cmd = "AT+B2400";
        break;
    case 4800:
        cmd = "AT+B4800";
        break;
    case 9600:
        cmd = "AT+B9600";
        break;
    case 19200:
        cmd = "AT+B19200";
        break;
    case 38400:
        cmd = "AT+B38400";
        break;
    case 57600:
        cmd = "AT+B57600";
        break;
    case 115200:
        cmd = "AT+B115200";
        break;
    default:
        return false;
    }

    String response = sendATCommand(cmd);
    return response.indexOf("OK") >= 0;
}

/**
 * @brief 查询当前频道
 * @return 当前频道字符串
 */
String HC12Module::getChannel()
{
    return sendATCommand("AT+RC");
}

/**
 * @brief 设置频道
 * @param channel 频道号(001-127)
 * @return 设置是否成功
 */
bool HC12Module::setChannel(String channel)
{
    if (channel.length() != 3 || channel.toInt() < 1 || channel.toInt() > 127)
    {
        return false;
    }

    String cmd = "AT+C" + channel;
    String response = sendATCommand(cmd);
    return response.indexOf("OK") >= 0;
}

/**
 * @brief 查询当前工作模式
 * @return 模式字符串
 */
String HC12Module::getMode()
{
    return sendATCommand("AT+RF");
}

/**
 * @brief 设置工作模式
 * @param mode 模式(FU1, FU2, FU3, FU4)
 * @return 设置是否成功
 */
bool HC12Module::setMode(String mode)
{
    if (mode != "FU1" && mode != "FU2" && mode != "FU3" && mode != "FU4")
    {
        return false;
    }

    String cmd = "AT+FU" + mode.substring(2);
    String response = sendATCommand(cmd);
    return response.indexOf("OK") >= 0;
}

/**
 * @brief 查询发射功率
 * @return 功率信息字符串
 */
String HC12Module::getPower()
{
    return sendATCommand("AT+RP");
}

/**
 * @brief 设置发射功率等级
 * @param powerLevel 功率等级(1-8)
 * @return 设置是否成功
 */
bool HC12Module::setPowerLevel(int powerLevel)
{
    if (powerLevel < 1 || powerLevel > 8)
    {
        return false;
    }

    String cmd = "AT+P" + String(powerLevel);
    String response = sendATCommand(cmd);
    return response.indexOf("OK") >= 0;
}

/**
 * @brief 获取所有参数
 * @return 所有参数字符串
 */
String HC12Module::getAllParams()
{
    return sendATCommand("AT+RX");
}

/**
 * @brief 恢复出厂默认设置
 * @return 恢复是否成功
 */
bool HC12Module::factoryReset()
{
    String response = sendATCommand("AT+DEFAULT");
    return response.indexOf("OK") >= 0;
}

/**
 * @brief 设置串口校验位
 * @param parity 校验位类型(N-无校验, O-奇校验, E-偶校验)
 * @return 设置是否成功
 */
bool HC12Module::setParity(char parity)
{
    if (parity != 'N' && parity != 'O' && parity != 'E')
    {
        return false;
    }

    String cmd = "AT+P" + String(parity);
    String response = sendATCommand(cmd);
    return response.indexOf("OK") >= 0;
}

/**
 * @brief 进入睡眠模式
 * @return 设置是否成功
 */
bool HC12Module::enterSleepMode()
{
    String response = sendATCommand("AT+SLEEP");
    return response.indexOf("OK") >= 0;
}

/**
 * @brief 通过HC-12发送数据
 * @param data 要发送的数据
 * @return 发送是否成功
 */
bool HC12Module::sendData(const String &data)
{
    if (currentMode != COMM_MODE)
    {
        setMode(COMM_MODE);
    }

    // 清空接收缓冲区
    while (hc12Serial->available())
    {
        hc12Serial->read();
    }

    // 发送数据
    size_t bytesWritten = hc12Serial->print(data);
    hc12Serial->flush(); // 确保数据发送完成

    return bytesWritten > 0;
}

/**
 * @brief 检查是否有可读数据
 * @return 是否有数据可读
 */
bool HC12Module::available()
{
    return hc12Serial->available() > 0;
}

/**
 * @brief 读取数据
 * @return 读取到的数据字符串
 */
String HC12Module::readData()
{
    String data = "";
    while (hc12Serial->available())
    {
        data += (char)hc12Serial->read();
    }
    return data;
}

/**
 * @brief 硬件诊断函数
 */
void HC12Module::diagnoseHardware()
{
    Serial.println("\n=== HC-12 Hardware Diagnosis ===");

    // 1. 检查SET引脚状态
    Serial.println("1. Checking SET pin status...");
    pinMode(setPin, INPUT_PULLUP);
    delay(10);
    int setPinState = digitalRead(setPin);
    Serial.print("   SET Pin current state: ");
    Serial.println(setPinState ? "HIGH" : "LOW");

    // 2. 测试SET引脚控制
    Serial.println("2. Testing SET pin control...");
    // 切换为输出模式以便通过 setMode 控制引脚
    pinMode(setPin, OUTPUT);

    Serial.println("   Setting SET pin LOW (AT mode)...");
    // 使用 setMode 确保 currentMode 与引脚状态一致
    setMode(AT_MODE);
    delay(100);
    Serial.print("   SET Pin state: ");
    Serial.println(digitalRead(setPin) ? "HIGH" : "LOW");

    Serial.println("   Setting SET pin HIGH (Communication mode)...");
    setMode(COMM_MODE);
    delay(100);
    Serial.print("   SET Pin state: ");
    Serial.println(digitalRead(setPin) ? "HIGH" : "LOW");

    // 3. 测试连接
    Serial.println("3. Testing connection...");
    bool connected = testConnection();
    Serial.println(connected ? "   ✓ Connection successful" : "   ✗ Connection failed");

    // 4. 获取模块信息
    Serial.println("4. Module information:");
    String version = getVersion();
    Serial.print("   Firmware: ");
    Serial.println(version);

    String params = getAllParams();
    Serial.println("   Parameters:");
    Serial.println(params);

    Serial.println("\n=== Diagnosis Complete ===");
    // 确保在诊断结束后退出 AT 模式，回到通信模式
    setMode(COMM_MODE);
}

/**
 * @brief 配置为最佳设置
 * @return 配置是否成功
 */
bool HC12Module::configureOptimal()
{
    bool success = true;

    Serial.println("Configuring HC-12 with optimal settings...");

    // 设置为FU3模式
    success &= setMode("FU3");
    delay(100);

    // 设置波特率为38400bps
    success &= setBaudRate(38400);
    delay(100);

    // 设置频道为039
    success &= setChannel("039");
    delay(100);

    // 设置发射功率为最大
    success &= setPowerLevel(8);
    delay(100);

    // 设置无校验
    success &= setParity('N');
    delay(100);

    // 验证配置
    delay(200);
    String config = getAllParams();
    Serial.println("Final configuration:");
    Serial.println(config);

    if (success)
    {
        Serial.println("HC-12 configured successfully!");
        Serial.println("Settings: FU3 mode, 38400bps, CH039(433.4MHz), +20dBm, No parity");
    }
    else
    {
        Serial.println("Warning: Some settings may not have been applied");
    }

    return success;
}