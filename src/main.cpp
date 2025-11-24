/**
 * @file main.cpp
 * @brief 基于ESP32的4x4矩阵键盘和SSD1315 OLED显示屏的九宫格输入法（IME）+ HC-12无线模块实现的无线通讯终端。
 *
 * 硬件：
 * - ESP32le
 * - 4x4矩阵键盘
 * - SSD1315 OLED显示屏（I2C）
 * - HC-12 无线模块 (连接到 D17, D16)
 */

#include <Arduino.h>
#include "debug.h" // 调试接口，注释掉可关闭所有调试输出

// 硬件交互相关库
#include <Keypad.h>
#include <Wire.h>
#include <U8g2lib.h>
#include "HC12_Module.h" // HC-12模块类
// SPIFFS 用于持久化消息历史与 RCV 设置
#include <SPIFFS.h>

// 标准C++库
// 注意：必须在Arduino.h之后包含，以便识别String类型。
#include <vector>
#include <map>
#include <set>
#include <string>
#include <algorithm>
#include <cstring>

// 配置文件
#include "config.h"

// --- HC-12 模块初始化 ---
HC12Module hc12;

// --- OLED显示屏设置 ---
// 使用硬件I2C。自定义I2C引脚：SDA=D22，SCL=D23。
// 构造函数指定SCL和SDA引脚，适用于自定义接线。
U8G2_SSD1315_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/U8X8_PIN_NONE, /* clock=*/SSD1315_SCL_PIN, /* data=*/SSD1315_SDA_PIN);

// --- 键盘设置 ---
char keys[ROWS][COLS] = {
    {'1', '2', '3', 'A'}, // 'A' for CHS/ENG switch
    {'4', '5', '6', 'B'}, // 'B' for NUM/Letter switch
    {'7', '8', '9', 'C'}, // 'C' for Delete
    {'*', '0', '#', 'D'}  // '*' for Prev Cand, '#' for Next Cand, 'D' for Send/Confirm
};
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// 输入法子模块
#include "input_method/input_method.h"
// RIP 协议子模块
#include "rip.h"

// 前向声明（UI/逻辑辅助）
void drawUI();
void handleKeypress(char key);
void utf8Backspace(String &s);
// 串口控制台输入处理（按行缓冲）
void handleSerialConsoleInput();

// 显示刷新节拍
unsigned long lastDisplayUpdate = 0;
const unsigned long DISPLAY_INTERVAL = 80; // ms

// 空闲/省电设置
const unsigned long IDLE_TIMEOUT_MS = 120000; // 2 分钟无操作进入省电（可按需调整）
unsigned long lastActivityTime = 0;           // 上次交互时间
bool lowPowerMode = false;
const unsigned long SLEEP_DISPLAY_INTERVAL = 1000; // 省电时屏幕更新间隔（降低频率）

// 接收消息临时缓冲与显示计时
String incomingMessage = "";
unsigned long incomingMessageTime = 0;
const unsigned long INCOMING_MSG_DISPLAY_MS = 3000;

// 发送/接收 模式切换：recvMode = true 表示聊天/接收模式，记录历史；false 表示发送模式，收到消息为短暂提示
bool recvMode = false;
std::vector<String> messageHistory; // 存储接收/发送历史（简化为 String 列表）
// 可配置的历史上限（RCV 设置中可调整并可持久化）
size_t maxMessageHistory = 50;
// 聊天分页：chatPage=0 表示最新（最靠近尾部）的页面
int chatPage = 0;
// 每页聊天消息数（RCV 模式可配置）
int chatPageSize = 3;
// 聊天翻页长按支持
int chatNavDir = 0;            // +1 向更早页（older），-1 向更新页（newer）
unsigned long chatNavLast = 0; // 上次执行翻页或按下时间
char lastChatNavKey = 0;
const unsigned long CHAT_NAV_INITIAL_DELAY = 500;   // ms 首次长按延迟
const unsigned long CHAT_NAV_REPEAT = 120;          // ms 重复间隔
const unsigned long CHAT_NAV_JUMP_THRESHOLD = 1500; // ms 长按阈值，跳到最早/最新

// 跳转确认提示
String chatJumpMsg = "";
unsigned long chatJumpMsgTime = 0;
const unsigned long CHAT_JUMP_MSG_MS = 900; // 提示显示时长

// RCV 设置相关（通过在接收模式下连续按 '*' '#' 进入）
bool inRcvSettings = false;
int rcvSettingsIndex = 0;
bool rcvPersist = false; // 是否写入文件持久化消息历史

// 快捷按键检测（在 recvMode 下连续按 '*' '#' 触发进入 RCV 设置）
char lastRecvShortcut = 0; // 0/ '*' / '#'
unsigned long lastRecvShortcutTime = 0;
const unsigned long RECV_SHORTCUT_WINDOW = 1200; // ms 内连续按键窗口

// 持久化文件路径
const char *HISTORY_FILE = "/history.txt";
const char *SETTINGS_FILE = "/rcv_settings.txt";

// 简单 UTF-8 验证：尝试判断字符串是否为合理的 UTF-8（非严格，但能过滤大量乱码）
bool looksLikeUtf8(const String &s)
{
    const char *p = s.c_str();
    size_t len = s.length();
    size_t i = 0;
    while (i < len)
    {
        uint8_t c = (uint8_t)p[i];
        if (c < 0x80)
        {
            // ASCII
            i++;
        }
        else if ((c & 0xE0) == 0xC0)
        {
            // 2-byte
            if (i + 1 >= len)
                return false;
            uint8_t c1 = (uint8_t)p[i + 1];
            if ((c1 & 0xC0) != 0x80)
                return false;
            i += 2;
        }
        else if ((c & 0xF0) == 0xE0)
        {
            // 3-byte
            if (i + 2 >= len)
                return false;
            uint8_t c1 = (uint8_t)p[i + 1];
            uint8_t c2 = (uint8_t)p[i + 2];
            if (((c1 & 0xC0) != 0x80) || ((c2 & 0xC0) != 0x80))
                return false;
            i += 3;
        }
        else if ((c & 0xF8) == 0xF0)
        {
            // 4-byte
            if (i + 3 >= len)
                return false;
            uint8_t c1 = (uint8_t)p[i + 1];
            uint8_t c2 = (uint8_t)p[i + 2];
            uint8_t c3 = (uint8_t)p[i + 3];
            if (((c1 & 0xC0) != 0x80) || ((c2 & 0xC0) != 0x80) || ((c3 & 0xC0) != 0x80))
                return false;
            i += 4;
        }
        else
        {
            return false;
        }
    }
    return true;
}

// 保存/加载持久化设置和历史（非常简化：history 为每行消息）
void saveHistoryToFS()
{
    if (!SPIFFS.begin(true))
        return;
    File f = SPIFFS.open(HISTORY_FILE, FILE_WRITE);
    if (!f)
        return;
    for (auto &m : messageHistory)
    {
        f.println(m);
    }
    f.close();
}

void loadHistoryFromFS()
{
    if (!SPIFFS.begin(true))
        return;
    if (!SPIFFS.exists(HISTORY_FILE))
        return;
    File f = SPIFFS.open(HISTORY_FILE, FILE_READ);
    if (!f)
        return;
    messageHistory.clear();
    while (f.available())
    {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() > 0)
            messageHistory.push_back(line);
        if (messageHistory.size() > maxMessageHistory)
            messageHistory.erase(messageHistory.begin());
    }
    f.close();
}

void saveRcvSettings()
{
    if (!SPIFFS.begin(true))
        return;
    File f = SPIFFS.open(SETTINGS_FILE, FILE_WRITE);
    if (!f)
        return;
    f.println(String(chatPageSize));
    f.println(String(maxMessageHistory));
    f.println(String(rcvPersist ? 1 : 0));
    f.close();
}

void loadRcvSettings()
{
    if (!SPIFFS.begin(true))
        return;
    if (!SPIFFS.exists(SETTINGS_FILE))
        return;
    File f = SPIFFS.open(SETTINGS_FILE, FILE_READ);
    if (!f)
        return;
    String a = f.readStringUntil('\n');
    String b = f.readStringUntil('\n');
    String c = f.readStringUntil('\n');
    a.trim();
    b.trim();
    c.trim();
    if (a.length() > 0)
        chatPageSize = a.toInt();
    if (b.length() > 0)
        maxMessageHistory = (size_t)b.toInt();
    if (c.length() > 0)
        rcvPersist = (c.toInt() != 0);
    f.close();
}

// 串口控制台输入缓冲（用于接收来自 PC 的命令行）
String serialCmdBuffer = "";

// UI 辅助
int candidateWindowStart = 0; // candidates 显示窗口起始索引

// 大小写与特殊符号支持
bool engUppercase = false; // ENG 模式是否为大写
bool symbolMode = false;   // NUM 模式下是否显示特殊符号

// 符号多次按键支持（SYM 模式）
char lastSymbolKey = 0;                   // 上一次用于符号选择的按键
unsigned long lastSymbolTime = 0;         // 上一次按键时间
int lastSymbolIndex = 0;                  // 在 specialMap 中的索引（循环选择）
const unsigned long SYMBOL_TIMEOUT = 800; // ms，多次按键超时

// 1键单击/双击(显示对应表)支持
char lastOneKey = 0;
unsigned long lastOneTime = 0;
bool showKeymap = false;
unsigned long keymapShowTime = 0;
const unsigned long KEYMAP_DISPLAY_MS = 3000;

// --- HC-12 设置界面状态 ---
bool inSettings = false; // 是否进入 HC-12 设置界面
int settingsIndex = 0;   // 当前在设置菜单的索引
String settingsMsg = ""; // 设置操作返回信息，短暂显示
unsigned long settingsMsgTime = 0;
const unsigned long SETTINGS_MSG_MS = 1500;

const char *settingsMenu[] = {
    "Get Version",
    "Get All Params",
    "Get Baud",
    "Set Baud",
    "Get Channel",
    "Set Channel",
    "Get Mode",
    "Set Mode",
    "Get Power",
    "Set Power",
    "Sleep",
    "Factory Reset",
    "Exit"};
const int SETTINGS_MENU_COUNT = sizeof(settingsMenu) / sizeof(settingsMenu[0]);

// 特殊符号映射（NUM 模式下 B 切换时使用）
const char *specialMap[10] = {
    " ",
    "~",     // 1 一般不使用但保留
    "!@#",   // 2 -> 举例
    "#$%",   // 3
    "^&*",   // 4
    "()_+",  // 5
    "-=[]",  // 6
    "{};:'", // 7
    "<>,.?", // 8
    "+/\\|"  // 9
};

// 自动检测 HC-12 当前波特率并配置本地串口（对常见波特率循环发送 AT 检测响应）
void configureHC12()
{
    const int baudRates[] = {1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200};
    const int numRates = sizeof(baudRates) / sizeof(baudRates[0]);
    int foundBaud = -1;

    for (int i = 0; i < numRates && foundBaud < 0; i++)
    {
        int rate = baudRates[i];
        Serial.print("[HC12 DETECT] Trying baud: ");
        Serial.println(rate);

        // 重新配置本地串口到 candidate 波特率
        hc12.reconfigureLocalSerial(rate);
        delay(30);

        // 发送 AT 检测（sendATCommand 会在必要时进入 AT 模式）
        String resp = hc12.sendATCommand("AT", 200);
        resp.trim();
        Serial.print("[HC12 DETECT] Resp: ");
        Serial.println(resp);

        String upper = resp;
        upper.toUpperCase();
        if (upper.indexOf("OK") >= 0)
        {
            foundBaud = rate;
            break;
        }
    }

    if (foundBaud > 0)
    {
        HC12_BAUD_RATE = foundBaud;
        // 确保本地串口与模块同步
        hc12.reconfigureLocalSerial(HC12_BAUD_RATE);
        Serial.print("[HC12 DETECT] Found working baud: ");
        Serial.println(HC12_BAUD_RATE);
        incomingMessage = String("HC12 baud:") + String(HC12_BAUD_RATE);
        incomingMessageTime = millis();
    }
    else
    {
        Serial.println("[HC12 DETECT] No working baud found, keep default.");
    }
}

// 更新最后活动时间；如果处于低功耗则唤醒
void updateLastActivity()
{
    lastActivityTime = millis();
    if (lowPowerMode)
    {
        // 唤醒流程：恢复 OLED，退出 HC-12 睡眠
        lowPowerMode = false;
        // 唤醒 HC-12（切换到通信模式并重设本地串口）
        hc12.setMode(HC12Module::COMM_MODE);
        hc12.reconfigureLocalSerial(HC12_BAUD_RATE);
        // 打开显示
        u8g2.setPowerSave(false);
        // 给模块一点时间
        delay(60);
        // 恢复显示并提示
        incomingMessage = "Woke from sleep";
        incomingMessageTime = millis();
        drawUI();
    }
}

// 进入低功耗模式：关闭 OLED 显示并让 HC-12 进入睡眠（AT+SLEEP）
void enterLowPowerMode()
{
    if (lowPowerMode)
        return;
    lowPowerMode = true;
    // 关闭显示（进入节能）
    u8g2.setPowerSave(true);
    // 让 HC-12 进入睡眠（通过 AT 指令）
    // 切换到 AT 模式并发送 AT+SLEEP
    bool ok = hc12.enterSleepMode();
    // 在屏幕上显示提示（如果屏幕仍可用）
    incomingMessage = ok ? "Entering sleep" : "Sleep failed";
    incomingMessageTime = millis();
    // 降低显示更新频率
    lastDisplayUpdate = millis();
}

// 开机动画：简单进度条+名称
void showBootAnimation()
{
    const int frames = 6;
    u8g2.setFont(u8g2_font_6x13B_tr);
    for (int f = 0; f < frames; f++)
    {
        u8g2.clearBuffer();
        char buf[64];
        snprintf(buf, sizeof(buf), "WirelessMessage");
        u8g2.drawStr(0, 18, buf);
        // 进度条
        int w = (f + 1) * 20;
        u8g2.drawFrame(0, 28, 100, 8);
        u8g2.drawBox(1, 29, w > 98 ? 98 : w, 6);
        u8g2.drawStr(0, 52, "Booting...");
        u8g2.sendBuffer();
        delay(200);
    }
}

// 在开机页面显示具体步骤与状态
void showBootStep(const char *status, int percent)
{
    if (percent < 0)
        percent = 0;
    if (percent > 100)
        percent = 100;
    int framesW = map(percent, 0, 100, 0, 98);

    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x13B_tr);
    u8g2.drawStr(0, 12, "WirelessMessage");

    // 进度条框与填充
    u8g2.drawFrame(0, 22, 100, 8);
    if (framesW > 0)
        u8g2.drawBox(1, 23, framesW, 6);

    // 状态文本，简短显示在进度条下方
    u8g2.setFont(u8g2_font_6x13B_tr);
    if (status && strlen(status) > 0)
    {
        // 截断以防文本过长
        char s[40];
        strncpy(s, status, sizeof(s) - 1);
        s[sizeof(s) - 1] = '\0';
        u8g2.drawStr(0, 48, s);
    }

    // 百分比显示在右上角
    char pct[8];
    snprintf(pct, sizeof(pct), "%d%%", percent);
    u8g2.drawStr(110 - 6, 12, pct);

    u8g2.sendBuffer();
    // 给用户时间看清状态
    delay(160);
}

void setup()
{
    // 串口用于调试
    Serial.begin(115200);
    delay(2000);
    DEBUG_PRINTLN("Starting WirelessMessage...");

    // OLED 初始化
    u8g2.begin();
    u8g2.enableUTF8Print();

    // 设置默认字体（ASCII）
    u8g2.setFont(u8g2_font_6x13B_tr);

    // 显示开机动画并绘制初始 UI
    showBootAnimation();
    // 显示当前步骤：已初始化显示
    showBootStep("Init OLED", 10);

    // HC-12 初始化
    showBootStep("Init HC-12", 25);
    if (!hc12.init(HC12_SET_PIN, 2, 16, 17, HC12_BAUD_RATE))
    {
        DEBUG_PRINTLN("HC-12 init failed");
        showBootStep("HC-12 init failed", 25);
    }
    else
    {
        DEBUG_PRINTLN("HC-12 initialized");
    }

    // 自动检测 HC-12 当前波特率并配置本地串口
    showBootStep("Detect HC-12 baud", 35);
    configureHC12();

    showBootStep("Load pinyin dict", 60);
    loadPinyinDict();
    showBootStep("Load frequency data", 75);
    loadFrequencyData();

    // 初始化 RIP 子模块
    showBootStep("Init RIP module", 85);
    ripInit();

    // 加载 RCV 设置与历史（如果持久化开启）
    loadRcvSettings();
    if (rcvPersist)
    {
        loadHistoryFromFS();
    }

    // 初始模式显示
    inputMode = MODE_CHS;

    // 初次绘制
    showBootStep("Ready", 100);
    drawUI();

    // 记录初始活动时间
    lastActivityTime = millis();
}

// 删除 UTF-8 最后一个字符
void utf8Backspace(String &s)
{
    if (s.length() == 0)
        return;
    int i = s.length() - 1;
    // 向前找到一个非续字节(0b10xxxxxx)
    while (i > 0 && (((uint8_t)s[i] & 0xC0) == 0x80))
    {
        i--;
    }
    s = s.substring(0, i);
}

// 将当前候选窗口调整到包含 candidateIndex
void normalizeCandidateWindow()
{
    const int windowSize = 5;
    if (candidateIndex < candidateWindowStart)
        candidateWindowStart = candidateIndex;
    if (candidateIndex >= candidateWindowStart + windowSize)
        candidateWindowStart = candidateIndex - windowSize + 1;
    if (candidateWindowStart < 0)
        candidateWindowStart = 0;
}

void handleKeypress(char key)
{
    if (key == NO_KEY)
        return;

    // 有按键交互，更新活动时间并在必要时唤醒
    updateLastActivity();

    DEBUG_PRINT("Keypress: ");
    DEBUG_PRINTLN(key);

    // 通用按键处理
    // 如果处于 RCV 设置界面，部分按键用于调整设置
    if (inRcvSettings)
    {
        if (key == 'A')
        {
            if (rcvSettingsIndex > 0)
                rcvSettingsIndex--;
            drawUI();
            return;
        }
        else if (key == 'B')
        {
            // B 做为下移
            rcvSettingsIndex++;
            // 限制范围
            if (rcvSettingsIndex > 5)
                rcvSettingsIndex = 5;
            drawUI();
            return;
        }
        else if (key == 'C')
        {
            // 小幅调整 - 对于 PerPage 与 MaxHist 进行减小
            if (rcvSettingsIndex == 0)
            {
                if (chatPageSize > 1)
                    chatPageSize--;
            }
            else if (rcvSettingsIndex == 1)
            {
                if (maxMessageHistory > 10)
                    maxMessageHistory -= 10;
            }
            drawUI();
            return;
        }
        // 其它按键继续走下面的通用处理（包括 D 已在上面处理）
    }
    switch (key)
    {
    case 'A':
        // 循环切换：CHS -> ENG(lower) -> ENG(upper/ENG(C)) -> NUM(normal) -> SYM -> CHS
        if (inputMode == MODE_CHS)
        {
            inputMode = MODE_ENG;
            engUppercase = false; // ENG 小写
        }
        else if (inputMode == MODE_ENG && !engUppercase)
        {
            // ENG -> ENG 大写 (标记为 ENG(C) )
            engUppercase = true;
        }
        else if (inputMode == MODE_ENG && engUppercase)
        {
            // ENG(upper) -> NUM(normal)
            inputMode = MODE_NUM;
            symbolMode = false;
        }
        else if (inputMode == MODE_NUM && !symbolMode)
        {
            // NUM -> SYM
            symbolMode = true;
        }
        else
        {
            // SYM 或 其他情况 -> 回到中文模式
            inputMode = MODE_CHS;
            engUppercase = false;
            symbolMode = false;
        }
        // 切换模式时退出拼音组合
        pinyinBuffer = "";
        composing = false;
        candidates.clear();
        candidateIndex = 0;
        // 取消符号多键状态
        lastSymbolKey = 0;
        lastSymbolIndex = 0;
        break;

    case 'B':
        // 双按 B 进入/退出设置界面；单按切换 发送/接收 模式（recvMode）
        {
            static unsigned long lastBTime = 0;
            unsigned long nowB = millis();
            if ((nowB - lastBTime) < 600)
            {
                // 双按：切换设置界面并切换 HC-12 模式
                inSettings = !inSettings;
                settingsIndex = 0;
                settingsMsg = "";
                settingsMsgTime = 0;
                if (inSettings)
                {
                    // 进入设置：拉低 SET 引脚进入 AT 模式（设置生效需退出设置）
                    hc12.setMode(HC12Module::AT_MODE);
                }
                else
                {
                    // 退出设置：回到通信模式，使设置生效并恢复正常通信
                    hc12.setMode(HC12Module::COMM_MODE);
                }
                lastBTime = 0;
                break;
            }
            lastBTime = nowB;

            // 单按：切换 recvMode
            recvMode = !recvMode;
            // 切换到接收模式时，清除临时 incomingMessage，使聊天窗口显示历史
            if (recvMode)
            {
                incomingMessage = "";
                // 进入聊天模式，显示最新页
                chatPage = 0;
            }
            // 保持输入/拼音状态不变，但清除当前拼音组合以避免模式干扰
            pinyinBuffer = "";
            composing = false;
            candidates.clear();
            candidateIndex = 0;
            // 切换接收/发送时重置符号多键状态
            lastSymbolKey = 0;
            lastSymbolIndex = 0;
        }
        break;

    case 'C':
        // 删除
        if (composing && pinyinBuffer.length() > 0)
        {
            pinyinBuffer.remove(pinyinBuffer.length() - 1);
            updateCandidates();
            if (pinyinBuffer.length() == 0)
                composing = false;
        }
        else if (inputBuffer.length() > 0)
        {
            utf8Backspace(inputBuffer);
        }
        break;

    case 'D':
        // 发送/确认
        if (inSettings)
        {
            // 执行选中的设置项
            String res = "";
            if (settingsIndex == 1)
            {
                res = hc12.getAllParams();
                // 将返回结果按 "OK" 位置拆分以便显示（插入分隔符）
                String formatted = "";
                int i = 0;
                int n = res.length();
                while (i < n)
                {
                    if (i + 2 <= n && res.substring(i, i + 2) == "OK")
                    {
                        if (formatted.length() > 0)
                            formatted += " | ";
                        formatted += "OK";
                        i += 2;
                    }
                    else
                    {
                        formatted += res[i];
                        i++;
                    }
                }
                // 推送到接收历史，前缀为 ATRCV:
                String note = String("ATRCV: ") + formatted;
                messageHistory.push_back(note);
                if (messageHistory.size() > maxMessageHistory)
                    messageHistory.erase(messageHistory.begin());
                // 切换到接收模式并显示最新页
                recvMode = true;
                chatPage = 0;
                // 将短暂提示也设置为 formatted 以便在非聊天模式下也能看到
                incomingMessage = formatted;
                incomingMessageTime = millis();
                // 把 res 替换为 formatted 以供后续显示
                res = formatted;
            }
            // (settingsIndex == 1 handled above: Get All Params pushes to history)
            else if (settingsIndex == 2)
            {
                res = hc12.getBaudRate();
            }
            else if (settingsIndex == 3)
            {
                // 循环常见波特率并设置示例（用户可通过多次按 D 改变）
                static int baudOptions[] = {1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200};
                static int sel = 5; // 默认指向 38400 索引
                sel = (sel + 1) % (sizeof(baudOptions) / sizeof(int));
                int b = baudOptions[sel];
                bool ok = hc12.setBaudRate(b);
                if (ok)
                {
                    hc12.reconfigureLocalSerial(b);
                    res = "OK+B" + String(b);
                    // 更改波特率后重新检测并同步（以防模块写入后需要确认）
                    configureHC12();
                }
                else
                {
                    res = "FAIL";
                }
            }
            else if (settingsIndex == 4)
            {
                res = hc12.getChannel();
            }
            else if (settingsIndex == 5)
            {
                String cur = hc12.getChannel();
                int ch = 1;
                if (cur.length() >= 3)
                    ch = cur.substring(cur.length() - 3).toInt();
                ch++;
                if (ch > 127)
                    ch = 127;
                char buf[4];
                snprintf(buf, sizeof(buf), "%03d", ch);
                bool ok = hc12.setChannel(String(buf));
                res = ok ? String("OK+C") + String(buf) : String("FAIL");
            }
            else if (settingsIndex == 6)
            {
                res = hc12.getMode();
            }
            else if (settingsIndex == 7)
            {
                String cur = hc12.getMode();
                String next = "FU1";
                if (cur.indexOf("FU1") >= 0)
                    next = "FU2";
                else if (cur.indexOf("FU2") >= 0)
                    next = "FU3";
                else if (cur.indexOf("FU3") >= 0)
                    next = "FU4";
                else if (cur.indexOf("FU4") >= 0)
                    next = "FU1";
                bool ok = hc12.setMode(next);
                res = ok ? String("OK+FU") + next.substring(2) : String("FAIL");
            }
            else if (settingsIndex == 8)
            {
                res = hc12.getPower();
            }
            else if (settingsIndex == 9)
            {
                static int powerSel = 8;
                powerSel = (powerSel % 8) + 1;
                bool ok = hc12.setPowerLevel(powerSel);
                res = ok ? String("OK+P") + String(powerSel) : String("FAIL");
            }
            else if (settingsIndex == 10)
            {
                bool ok = hc12.enterSleepMode();
                res = ok ? "OK+SLEEP" : "FAIL";
            }
            else if (settingsIndex == 11)
            {
                bool ok = hc12.factoryReset();
                res = ok ? "OK+DEFAULT" : "FAIL";
                if (ok)
                {
                    // 恢复出厂后，模块波特率可能回到默认，重新检测并同步
                    configureHC12();
                }
            }
            else if (settingsIndex == 12)
            {
                inSettings = false; // 退出设置
                res = "Exit";
            }

            settingsMsg = res;
            settingsMsgTime = millis();
            // 在串口输出选择项与返回值，便于调试（按 D 无响应时查看）
            Serial.print("Settings select idx=");
            Serial.print(settingsIndex);
            Serial.print(" label=");
            Serial.print(settingsMenu[settingsIndex]);
            Serial.print(" -> response=");
            Serial.println(res);
            drawUI();
        }
        else if (inRcvSettings)
        {
            // 根据当前选项执行操作或切换
            // 0: PerPage (增1)
            // 1: MaxHist (+10)
            // 2: Persist (toggle)
            // 3: Save -> write settings and history
            // 4: Load -> load history
            // 5: Exit
            if (rcvSettingsIndex == 0)
            {
                chatPageSize = (chatPageSize % 8) + 1; // 1..8
            }
            else if (rcvSettingsIndex == 1)
            {
                maxMessageHistory = maxMessageHistory + 10;
                if (maxMessageHistory > 500)
                    maxMessageHistory = 500;
            }
            else if (rcvSettingsIndex == 2)
            {
                rcvPersist = !rcvPersist;
            }
            else if (rcvSettingsIndex == 3)
            {
                // Save
                saveRcvSettings();
                if (rcvPersist)
                    saveHistoryToFS();
                settingsMsg = "Saved";
                settingsMsgTime = millis();
            }
            else if (rcvSettingsIndex == 4)
            {
                // Load
                loadHistoryFromFS();
                settingsMsg = "Loaded";
                settingsMsgTime = millis();
            }
            else if (rcvSettingsIndex == 5)
            {
                inRcvSettings = false;
            }
            drawUI();
        }
        else if (composing && !candidates.empty())
        {
            commitCandidate();
        }
        else if (inputBuffer.length() > 0)
        {
            // 通过 HC-12 发送
            // 确保发送前为通信模式
            hc12.setMode(HC12Module::COMM_MODE);
            bool ok = hc12.sendData(inputBuffer);
            DEBUG_PRINT("Send: ");
            DEBUG_PRINT(inputBuffer);
            DEBUG_PRINT(" -> ");
            DEBUG_PRINTLN(ok ? "OK" : "FAIL");
            // 在发送模式下，保留短暂提示；在接收模式下追加到历史并显示
            String note = String(ok ? "Sent: " : "SendFail: ") + inputBuffer;
            incomingMessage = note;
            incomingMessageTime = millis();
            // 记录历史消息（无论当前模式，保存在 messageHistory）
            messageHistory.push_back(note);
            if (messageHistory.size() > maxMessageHistory)
                messageHistory.erase(messageHistory.begin());
            inputBuffer = "";
        }
        break;

    case '*':
        // 切换/显示 T9 表或上一个候选，或聊天翻页（上一页/更早）
        if (inSettings)
        {
            if (settingsIndex > 0)
                settingsIndex--;
        }
        else if (recvMode)
        {
            // 检测是否为进入 RCV 设置的快捷键序列：先按 '*' 再按 '#'（在窗口内）
            unsigned long now = millis();
            lastRecvShortcut = '*';
            lastRecvShortcutTime = now;

            // 如果已经在 rcv 设置中，交由设置处理（以下为聊天翻页逻辑）
            if (inRcvSettings)
            {
                if (rcvSettingsIndex > 0)
                    rcvSettingsIndex--;
            }
            else
            {
                // 正常聊天翻页（长按支持）
                chatNavDir = -1; // 向更新页（newer）
                chatNavLast = now;
                lastChatNavKey = '*';
                if (chatPage > 0)
                    chatPage--;
            }
        }
        else if (inputMode == MODE_CHS)
        {
            if (showT9Table && lastStarKey == '*')
            {
                showT9Table = false;
            }
            else
            {
                showT9Table = true;
            }
            lastStarKey = '*';
            lastStarTime = millis();
        }
        else
        {
            // 英文/数字 模式作为上一个候选（或无操作）
            if (!candidates.empty())
            {
                if (candidateIndex > 0)
                    candidateIndex--;
                normalizeCandidateWindow();
            }
        }
        break;

    case '#':
        // 下一个候选或聊天翻页（下一页/更新）
        if (inSettings)
        {
            if (settingsIndex + 1 < SETTINGS_MENU_COUNT)
                settingsIndex++;
        }
        else if (recvMode)
        {
            unsigned long now = millis();
            // 检查此前是否有 '*' 快捷按下且在时间窗口内，则进入 RCV 设置
            if (lastRecvShortcut == '*' && (now - lastRecvShortcutTime) < RECV_SHORTCUT_WINDOW && !inRcvSettings)
            {
                inRcvSettings = true;
                rcvSettingsIndex = 0;
                lastRecvShortcut = 0;
                // 进入设置时停用聊天翻页
            }
            else
            {
                // 正常聊天翻页（向更早）
                chatNavDir = +1; // 向更早页
                chatNavLast = now;
                lastChatNavKey = '#';
                chatPage++;
            }
        }
        else
        {
            if (!candidates.empty())
            {
                if (candidateIndex + 1 < candidates.size())
                    candidateIndex++;
                normalizeCandidateWindow();
            }
        }
        break;

    default:
        // 数字键/输入处理
        if (key >= '0' && key <= '9')
        {
            // 处理 1 键的单击/双击：单击插入空格，双击显示对应表
            if (key == '1')
            {
                unsigned long now = millis();
                if (lastOneKey == '1' && (now - lastOneTime) < 800)
                {
                    // 双击：切换显示键位对应表
                    showKeymap = !showKeymap;
                    keymapShowTime = millis();
                }
                else if (inputMode == MODE_NUM && !symbolMode)
                {
                    // 数字模式下，1 键作为数字输入
                    inputBuffer += key;
                }
                else
                {
                    // 单击：插入空格
                    inputBuffer += ' ';
                }
                lastOneKey = '1';
                lastOneTime = now;
                break; // 已处理
            }

            if (inputMode == MODE_CHS)
            {
                if (key == '0')
                {
                    // 0 作为候选确认
                    if (composing && !candidates.empty())
                    {
                        commitCandidate();
                    }
                    else
                    {
                        inputBuffer += '0';
                    }
                }
                else
                {
                    handlePinyinInput(key);
                }
            }
            else if (inputMode == MODE_ENG)
            {
                if (key == '0')
                {
                    inputBuffer += ' ';
                }
                else
                {
                    handleEnglishInput(key);
                    // 如果为大写模式，将最后一个字母转为大写
                    if (engUppercase && inputBuffer.length() > 0)
                    {
                        int lastPos = inputBuffer.length() - 1;
                        char c = inputBuffer[lastPos];
                        if (c >= 'a' && c <= 'z')
                        {
                            inputBuffer.setCharAt(lastPos, (char)toupper(c));
                        }
                    }
                }
            }
            else if (inputMode == MODE_NUM)
            {
                if (!symbolMode)
                {
                    inputBuffer += key;
                }
                else
                {
                    // 特殊符号模式：使用 specialMap 映射，取第一个符号
                    int idx = key - '0';
                    if (idx >= 0 && idx <= 9)
                    {
                        const char *s = specialMap[idx];
                        if (s != nullptr && s[0] != '\0')
                        {
                            // 支持多次按键选择：在 SYMBOL_TIMEOUT 内连续按同一键则循环替换符号
                            unsigned long now = millis();
                            int len = (int)strlen(s);
                            if (lastSymbolKey == key && (now - lastSymbolTime) < SYMBOL_TIMEOUT)
                            {
                                // 已经有一个符号，替换为下一个
                                lastSymbolIndex = (lastSymbolIndex + 1) % len;
                                // 删除上一次选择的字符（适用于 ASCII 符号）
                                if (inputBuffer.length() > 0)
                                {
                                    utf8Backspace(inputBuffer);
                                }
                                inputBuffer += s[lastSymbolIndex];
                            }
                            else
                            {
                                // 新的符号选择序列
                                lastSymbolKey = key;
                                lastSymbolIndex = 0;
                                inputBuffer += s[lastSymbolIndex];
                            }
                            lastSymbolTime = now;
                        }
                    }
                }
            }
        }
        break;
    }

    // 保证 candidateIndex 有效
    if (!candidates.empty())
    {
        if (candidateIndex >= candidates.size())
            candidateIndex = candidates.size() - 1;
        if (candidateIndex < 0)
            candidateIndex = 0;
    }
    else
    {
        candidateIndex = 0;
    }

    drawUI();
}

void drawUI()
{
    // 基本布局：
    // 顶部：模式与状态
    // 中间：已输入文本
    // 中下：拼音与候选

    u8g2.clearBuffer();

    // 顶部模式栏或聊天页码
    u8g2.setFont(u8g2_font_6x13B_tr);
    if (recvMode)
    {
        // 聊天模式：隐藏输入模式提示，显示页码（chatPage 0 为最新），并显示总消息数与箭头提示
        int pageSize = chatPageSize; // 每页展示的消息行数
        int totalMsgs = (int)messageHistory.size();
        int totalPages = (int)((totalMsgs + pageSize - 1) / pageSize);
        if (totalPages <= 0)
            totalPages = 1;
        int displayPage = chatPage; // 0 为最新页
        // 限制范围
        if (displayPage < 0)
            displayPage = 0;
        if (displayPage >= totalPages)
            displayPage = totalPages - 1;
        // 页面文本与总数（中文）："聊天：第X/Y页  共N条"
        char topBuf[40];
        snprintf(topBuf, sizeof(topBuf), "聊天：第%d/%d页  共%d条", displayPage + 1, totalPages, totalMsgs);
        u8g2.drawStr(0, 10, topBuf);
        // 箭头提示：如果有更早页则显示上箭头▲，若有更近/更新页则显示下箭头▼
        if (displayPage + 1 < totalPages)
        {
            u8g2.drawStr(128 - 6, 10, "▲");
        }
        if (displayPage > 0)
        {
            u8g2.drawStr(128 - 12, 10, "▼");
        }
    }
    else
    {
        const char *modeStr = "CHS";
        if (inputMode == MODE_ENG)
        {
            if (engUppercase)
                modeStr = "ENG(C)"; // ENG capitalized (C 表示大写)
            else
                modeStr = "ENG";
        }
        else if (inputMode == MODE_NUM)
        {
            if (symbolMode)
                modeStr = "SYM";
            else
                modeStr = "NUM";
        }
        u8g2.drawStr(0, 10, "Mode:");
        u8g2.drawStr(48, 10, modeStr);
        // 在右上角显示简短的 RIP 路由摘要，便于调试（截断以免溢出）
        String ripSum = ripGetRoutesSummary();
        if (ripSum.length() > 20)
        {
            ripSum = ripSum.substring(0, 17) + "...";
        }
        u8g2.drawStr(80, 10, ripSum.c_str());
    }

    // 中间显示已输入文本（中文需UTF8字体）
    u8g2.setFont(u8g2_font_wqy12_t_gb2312);
    // 限制显示长度，避免超出屏幕
    String displayInput = inputBuffer;
    if (displayInput.length() == 0)
        displayInput = " ";
    // 在聊天模式下将输入区上移以腾出更多空间用于显示消息
    if (recvMode)
    {
        u8g2.drawUTF8(0, 26, displayInput.c_str());
    }
    else
    {
        u8g2.drawUTF8(0, 30, displayInput.c_str());
    }

    // 如果处于设置界面，绘制设置菜单覆盖底部区域
    if (inSettings)
    {
        u8g2.setFont(u8g2_font_6x13B_tr);
        u8g2.drawStr(0, 44, "HC-12 Settings:");
        // 显示多行菜单，让当前项尽量居中；靠近顶/尾时自动夹紧边界
        int linesToShow = 2;
        if (linesToShow > SETTINGS_MENU_COUNT)
            linesToShow = SETTINGS_MENU_COUNT;
        int half = linesToShow / 2;
        int maxStart = SETTINGS_MENU_COUNT - linesToShow;
        if (maxStart < 0)
            maxStart = 0;
        int start = settingsIndex - half;
        if (start < 0)
            start = 0;
        if (start > maxStart)
            start = maxStart;
        for (int i = 0; i < linesToShow; i++)
        {
            int idx = start + i;
            int y = 44 + 10 + i * 10;
            if (idx >= 0 && idx < SETTINGS_MENU_COUNT)
            {
                const char *txt = settingsMenu[idx];
                if (idx == settingsIndex)
                {
                    u8g2.drawStr(0, y, ">");
                    u8g2.drawStr(8, y, txt);
                }
                else
                {
                    u8g2.drawStr(8, y, txt);
                }
            }
        }
        // 显示短暂操作结果
        if (settingsMsg.length() > 0 && (millis() - settingsMsgTime) < SETTINGS_MSG_MS)
        {
            u8g2.drawStr(0, 64, settingsMsg.c_str());
        }
        u8g2.sendBuffer();
        return; // 不绘制其他下方内容
    }

    // RCV 模式下的专用设置界面（由 '*' '#' 快捷打开）
    if (inRcvSettings)
    {
        u8g2.setFont(u8g2_font_6x13B_tr);
        u8g2.drawStr(0, 44, "RCV Settings:");
        // 菜单项：每页条数、最大历史、持久化、保存、加载、退出
        const char *rcvMenu[] = {"PerPage", "MaxHist", "Persist", "Save", "Load", "Exit"};
        const int rcvCount = sizeof(rcvMenu) / sizeof(rcvMenu[0]);
        int lines = 3; // 每次显示3行
        int half = lines / 2;
        int maxStart = rcvCount - lines;
        if (maxStart < 0)
            maxStart = 0;
        int start = rcvSettingsIndex - half;
        if (start < 0)
            start = 0;
        if (start > maxStart)
            start = maxStart;
        for (int i = 0; i < lines; i++)
        {
            int idx = start + i;
            int y = 44 + 10 + i * 10;
            if (idx >= 0 && idx < rcvCount)
            {
                if (idx == rcvSettingsIndex)
                {
                    u8g2.drawStr(0, y, ">");
                    u8g2.drawStr(8, y, rcvMenu[idx]);
                }
                else
                {
                    u8g2.drawStr(8, y, rcvMenu[idx]);
                }
                // 右侧显示当前值
                char valbuf[20] = {0};
                if (idx == 0)
                    snprintf(valbuf, sizeof(valbuf), "%d", chatPageSize);
                else if (idx == 1)
                    snprintf(valbuf, sizeof(valbuf), "%u", (unsigned)maxMessageHistory);
                else if (idx == 2)
                    snprintf(valbuf, sizeof(valbuf), "%s", rcvPersist ? "Yes" : "No");
                u8g2.drawStr(80, y, valbuf);
            }
        }
        u8g2.sendBuffer();
        return;
    }

    // 显示拼音缓冲（如果有）
    u8g2.setFont(u8g2_font_6x13B_tr);
    String py = pinyinBuffer;
    // 在聊天模式下为保证空间，不绘制拼音缓冲；否则正常显示
    if (!recvMode && composing && py.length() > 0)
    {
        u8g2.drawStr(0, 44, "Pinyin:");
        u8g2.drawStr(48, 44, py.c_str());
    }

    // 如果有临时传入消息，显示在底部
    if (recvMode)
    {
        // 接收/聊天模式：分页显示历史，从最新开始
        u8g2.setFont(u8g2_font_wqy12_t_gb2312);
        int pageSize = chatPageSize; // 每页显示行数
        int totalMsgs = (int)messageHistory.size();
        int totalPages = (totalMsgs + pageSize - 1) / pageSize;
        if (totalPages <= 0)
            totalPages = 1;
        int displayPage = chatPage;
        if (displayPage < 0)
            displayPage = 0;
        if (displayPage >= totalPages)
            displayPage = totalPages - 1;
        // 计算此页要显示的消息索引范围：页面0为最新页，显示 messageHistory 的尾部
        int endIndex = totalMsgs - displayPage * pageSize - 1; // inclusive
        int startIndex = endIndex - (pageSize - 1);
        if (endIndex < 0)
            endIndex = -1;
        if (startIndex < 0)
            startIndex = 0;
        // 聊天模式消息从更靠上处开始，使用更紧凑的行高以便显示三条
        int y = 36; // 顶部基线（较之前上移）
        const int CHAT_LINE_HEIGHT = 12;
        for (int i = startIndex; i <= endIndex && i >= 0 && i < totalMsgs; i++)
        {
            String m = messageHistory[i];
            u8g2.drawUTF8(0, y, m.c_str());
            y += CHAT_LINE_HEIGHT; // 行高
        }
        // 显示跳转确认提示（若有）
        if (chatJumpMsg.length() > 0 && (millis() - chatJumpMsgTime) < CHAT_JUMP_MSG_MS)
        {
            u8g2.setFont(u8g2_font_6x13B_tr);
            u8g2.drawStr(0, 58, chatJumpMsg.c_str());
        }
    }
    else if (incomingMessage.length() > 0 && (millis() - incomingMessageTime) < INCOMING_MSG_DISPLAY_MS)
    {
        u8g2.setFont(u8g2_font_wqy12_t_gb2312);
        u8g2.drawStr(0, 58, incomingMessage.c_str());
    }
    else
    {
        // 如果显示整体键位映射或T9表
        if (showKeymap || (showT9Table && (millis() - lastStarTime) < 3000))
        {
            u8g2.setFont(u8g2_font_6x13B_tr);
            // 绘制简易的九宫格对应表
            const char *labels[4][4] = {
                {"1", "2 abc", "3 def", "A"},
                {"4 ghi", "5 jkl", "6 mno", "B"},
                {"7 pqrs", "8 tuv", "9 wxyz", "C"},
                {"* Prev", "0 OK", "# Next", "D Send"}};

            int sx = 0;
            int sy = 44;
            int cellW = 32;
            int cellH = 10;
            for (int r = 0; r < 4; r++)
            {
                for (int c = 0; c < 4; c++)
                {
                    int x = sx + c * cellW;
                    int y = sy + r * cellH;
                    u8g2.drawStr(x, y, labels[r][c]);
                }
            }

            // 如果是符号模式，显示简要符号表
            if (symbolMode)
            {
                u8g2.drawStr(0, 58, "Symbol mode: press number for symbols");
            }
            else
            {
                // 显示大小写状态
                if (inputMode == MODE_ENG)
                {
                    u8g2.drawStr(80, 58, engUppercase ? "UP" : "lo");
                }
            }
        }
        else
        {
            // 候选显示窗口（最多 5 个）
            const int windowSize = 5;
            if (!candidates.empty())
            {
                normalizeCandidateWindow();
                int y = 58; // baseline
                int x = 0;
                int drawn = 0;
                for (int i = candidateWindowStart; i < candidates.size() && drawn < windowSize; i++)
                {
                    String cand = candidates[i];
                    // 用中文字体绘制
                    u8g2.setFont(u8g2_font_wqy12_t_gb2312);
                    int xpos = x + drawn * 24;
                    // 高亮当前选中
                    if (i == candidateIndex)
                    {
                        // 在选中项下划线标记
                        u8g2.drawUTF8(xpos, y, cand.c_str());
                        u8g2.drawLine(xpos, y + 2, xpos + 12, y + 2);
                    }
                    else
                    {
                        u8g2.drawUTF8(xpos, y, cand.c_str());
                    }
                    drawn++;
                }
            }
        }
    }

    u8g2.sendBuffer();
}

void loop()
{
    // 读取键盘按键
    char key = keypad.getKey();
    if (key != NO_KEY)
    {
        handleKeypress(key);
    }

    // 处理来自 PC 串口的命令输入
    handleSerialConsoleInput();
    // RIP 协议周期处理（路由老化与定期发送 UPDATE）
    ripLoop();

    // 读取 HC-12 接收
    if (hc12.available())
    {
        String msg = hc12.readData();
        if (msg.length() > 0)
        {
            // 更新活动时间（外部数据到达也视作活动）
            updateLastActivity();
            // 先交给 RIP 子模块处理；若返回 false 则按普通数据处理
            if (ripHandlePacket(msg))
            {
                // 将路由表摘要作为短暂提示显示（便于调试）
                incomingMessage = ripGetRoutesSummary();
                incomingMessageTime = millis();
                drawUI();
            }
            else
            {
                // 过滤明显乱码（非 UTF-8）以避免屏幕刷屏
                if (!looksLikeUtf8(msg))
                {
                    DEBUG_PRINT("Received garbled via HC-12, ignoring: ");
                    DEBUG_PRINTLN(msg);
                    incomingMessage = "<garbled ignored>";
                    incomingMessageTime = millis();
                    drawUI();
                }
                else
                {
                    String note = "RCV: " + msg;
                    DEBUG_PRINT("Received via HC-12: ");
                    DEBUG_PRINTLN(msg);
                    // 将收到的消息加入历史
                    messageHistory.push_back(note);
                    if (messageHistory.size() > maxMessageHistory)
                        messageHistory.erase(messageHistory.begin());

                    if (recvMode)
                    {
                        // 在接收/聊天模式中，保持历史在界面上（不使用短暂 incomingMessage）
                        // 新消息到来时自动切换到最新页
                        chatPage = 0;
                    }
                    else
                    {
                        // 在发送模式下，显示短暂提示
                        incomingMessage = note;
                        incomingMessageTime = millis();
                    }
                    drawUI();
                }
            }
        }
    }

    // 定期刷新显示（防止没有按键时屏幕静止）
    if (millis() - lastDisplayUpdate > DISPLAY_INTERVAL)
    {
        lastDisplayUpdate = millis();
        drawUI();
    }

    // 聊天翻页长按与重复处理
    if (lastChatNavKey != 0 && recvMode)
    {
        // 如果按键仍在按下，则根据时间执行重复或跳转
        bool stillPressed = false;
        // 尝试使用 Keypad 库的 isPressed（若不可用，编译会报错并修正）
        stillPressed = keypad.isPressed(lastChatNavKey);

        unsigned long now = millis();
        if (stillPressed)
        {
            // 如果超过跳转阈值，直接跳到最早或最新
            if ((now - chatNavLast) >= CHAT_NAV_JUMP_THRESHOLD)
            {
                if (chatNavDir > 0)
                {
                    // 跳到最早页
                    int pageSize = chatPageSize;
                    int totalMsgs = (int)messageHistory.size();
                    int totalPages = (totalMsgs + pageSize - 1) / pageSize;
                    if (totalPages <= 0)
                        totalPages = 1;
                    chatPage = totalPages - 1;
                    chatJumpMsg = "已跳转到最旧";
                }
                else if (chatNavDir < 0)
                {
                    // 跳到最新页
                    chatPage = 0;
                    chatJumpMsg = "已跳转到最新";
                }
                // 更新时间以避免重复触发，并记录提示显示时间
                chatNavLast = now;
                chatJumpMsgTime = now;
                drawUI();
            }
            else if ((now - chatNavLast) >= ((chatNavLast == 0) ? CHAT_NAV_INITIAL_DELAY : CHAT_NAV_REPEAT))
            {
                // 重复翻页
                if (chatNavDir > 0)
                {
                    chatPage++;
                }
                else if (chatNavDir < 0)
                {
                    if (chatPage > 0)
                        chatPage--;
                }
                chatNavLast = now;
                drawUI();
            }
        }
        else
        {
            // 按键已释放，停止导航
            lastChatNavKey = 0;
            chatNavDir = 0;
            chatNavLast = 0;
        }
    }

    // 空闲超时检测（进入低功耗）
    if (!lowPowerMode && (millis() - lastActivityTime) > IDLE_TIMEOUT_MS)
    {
        enterLowPowerMode();
    }

    delay(20); // 小延迟用于去抖与减轻 CPU 占用
}

// 处理串口控制台输入（按行），默认以 AT 模式发送指令；若响应包含 "ERROR" 则改为通信模式发送原始数据
void handleSerialConsoleInput()
{
    while (Serial.available())
    {
        char c = (char)Serial.read();
        // 支持回车或换行作为命令结束
        if (c == '\r' || c == '\n')
        {
            if (serialCmdBuffer.length() == 0)
                continue; // 忽略空行

            String cmd = serialCmdBuffer;
            serialCmdBuffer = "";

            // 有串口交互，更新活动时间并唤醒
            updateLastActivity();
            // 在串口打印接收到的命令
            DEBUG_PRINT("Console cmd: ");
            DEBUG_PRINTLN(cmd);

            // 支持本地命令：?RIP 或 RIP? 显示当前 RIP 路由摘要（不发送到 HC-12）
            if (cmd == "?RIP" || cmd == "RIP?")
            {
                String rs = ripGetRoutesSummary();
                Serial.println(rs);
                incomingMessage = rs;
                incomingMessageTime = millis();
                drawUI();
            }
            else
            {
                // 先以 AT 模式发送，并读回响应
                String response = hc12.sendATCommand(cmd, 800);
                DEBUG_PRINT("AT response: ");
                DEBUG_PRINTLN(response);

                // 如果响应包含 ERROR（不区分大小写），则切换到通信模式发送原始命令内容
                String upperResp = response;
                upperResp.toUpperCase();
                if (upperResp.indexOf("ERROR") >= 0)
                {
                    DEBUG_PRINTLN("AT returned ERROR, sending in communication mode...");
                    // 以通信模式发送原始命令字符串
                    bool ok = hc12.sendData(cmd);
                    DEBUG_PRINT("Comm send: ");
                    DEBUG_PRINT(ok ? "OK" : "FAIL");
                    DEBUG_PRINT(" -> ");
                    DEBUG_PRINTLN(cmd);

                    incomingMessage = String(ok ? "Sent(CMD): " : "SendFail: ") + cmd;
                    incomingMessageTime = millis();
                    updateLastActivity();
                }
                else
                {
                    // 显示 AT 响应
                    incomingMessage = "AT-> " + response;
                    incomingMessageTime = millis();
                    updateLastActivity();
                }
            }

            // 同步更新 UI
            drawUI();
        }
        else
        {
            // 组成命令行（回显可选）
            serialCmdBuffer += c;
        }
    }
}
