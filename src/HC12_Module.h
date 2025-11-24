/**
 * @file HC12_Module.h
 * @brief HC-12无线串口通信模块的头文件
 */

#ifndef HC12_MODULE_H
#define HC12_MODULE_H

#include <Arduino.h>
#include <HardwareSerial.h>

// 配置文件
#include "config.h"

class HC12Module
{
public:
    enum Mode
    {
        AT_MODE,
        COMM_MODE
    };

    // 初始化函数
    bool init(int setPin, int uartNum = HC12_UART_NUM, int rxPin = HC12_RX_PIN, int txPin = HC12_TX_PIN, int baudRate = HC12_BAUD_RATE);

    // 模式控制
    void setMode(Mode mode);

    // AT指令功能
    String sendATCommand(const String &command, int timeout = 1000);
    bool testConnection();
    String getVersion();
    String getBaudRate();
    bool setBaudRate(int baudRate);
    // 在主机端重新配置本地 UART（不发送 AT 指令，仅本地重设）
    bool reconfigureLocalSerial(int baudRate);
    String getChannel();
    bool setChannel(String channel);
    String getMode();
    bool setMode(String mode);
    String getPower();
    bool setPowerLevel(int powerLevel);
    String getAllParams();
    bool factoryReset();
    bool setParity(char parity);
    bool enterSleepMode();

    // 数据收发
    bool sendData(const String &data);
    bool available();
    String readData();

    // 诊断功能
    void diagnoseHardware();
    bool configureOptimal();

private:
    HardwareSerial *hc12Serial;
    int setPin;
    int uartNum;
    Mode currentMode;
    // 如果为 true，则 SET 引脚为 HIGH 表示进入 AT 模式；否则 LOW 表示 AT 模式
    bool atModeLevelHigh = false;
    // 存储串口引脚与当前波特率，便于在设置变更时重新初始化本地 UART
    int rxPin = -1;
    int txPin = -1;
    int currentBaud = 9600;
};

#endif // HC12_MODULE_H
