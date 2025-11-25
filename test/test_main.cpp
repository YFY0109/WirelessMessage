#include <Arduino.h>
#include <unity.h>

// 添加 OLED 与 HC-12 支持
#include <U8g2lib.h>
#include "../src/HC12_Module.h"

// 一个简单的测试函数，检查 1+1 是否等于 2
void test_addition(void)
{
    TEST_ASSERT_EQUAL(2, 1 + 1);
}

// OLED 实例（与 main.cpp 保持一致的 I2C 引脚）
U8G2_SSD1315_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/U8X8_PIN_NONE, /* clock=*/23, /* data=*/22);

// HC-12 模块实例与常量
HC12Module hc12;
const int HC12_SET_PIN = 4;
const int HC12_BAUD = 38400;

// 计时器发送状态
unsigned long lastSendMillis = 0;
int sendCounter = 0;

void setup()
{
    // 等待 2 秒，以便串口监视器连接
    delay(2000);

    Serial.begin(115200);

    // 初始化 OLED 并设置为全亮
    u8g2.begin();
    // 关闭省电模式并将对比度调到最大
    u8g2.setPowerSave(false);
    u8g2.setContrast(255);
    u8g2.enableUTF8Print();
    u8g2.setFont(u8g2_font_6x13B_tr);
    u8g2.clearBuffer();
    u8g2.drawStr(0, 20, "Display: FULL BRIGHT");
    u8g2.sendBuffer();

    // 初始化 HC-12（使用默认 UART2 TX/RX 引脚）
    if (!hc12.init(HC12_SET_PIN, 2, 16, 17, HC12_BAUD))
    {
        Serial.println("HC-12 init failed in test");
    }

    // 运行 Unity 测试（在 setup 中完整运行一次），然后继续进入定时器循环
    UNITY_BEGIN();
    RUN_TEST(test_addition);
    UNITY_END();

    // 初始化计时器
    lastSendMillis = millis();
    sendCounter = 0;
}

void loop()
{
    unsigned long now = millis();
    if (now - lastSendMillis >= 5000)
    {
        lastSendMillis = now;
        sendCounter++;

        // 将计数通过 HC-12 发送
        String payload = String("TIMER:") + String(sendCounter);
        // 确保处于通信模式
        hc12.setMode(HC12Module::COMM_MODE);
        bool ok = hc12.sendData(payload);
        Serial.print("Sent timer #");
        Serial.print(sendCounter);
        Serial.print(" -> ");
        Serial.println(ok ? "OK" : "FAIL");

        // 在 OLED 上显示当前计数
        u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_6x13B_tr);
        char buf[32];
        snprintf(buf, sizeof(buf), "Timer count: %d", sendCounter);
        u8g2.drawStr(0, 30, buf);
        u8g2.sendBuffer();
    }

    // 轻微延时，避免占用全部 CPU
    delay(50);
}
