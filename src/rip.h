// rip.h
// 简易 RIP-like 子模块接口（适用于 ESP32 + HC-12 的学习/模拟实现）
// 提供：初始化、主循环、处理收到的 RIP 报文、查看路由表等

#ifndef WM_RIP_H
#define WM_RIP_H

#include <Arduino.h>
#include <vector>
#include "HC12_Module.h"

// 定义路由条目结构体，使其在全局范围内可见
struct RouteEntry
{
    String dest;
    uint16_t metric;
    unsigned long lastSeen; // millis()
};

// 初始化 RIP 模块（定时器、路由表）
void ripInit();

// 在主循环中周期调用（发送更新、老化路由）
void ripLoop();

// 处理收到的报文；如果是 RIP 报文则处理并返回 true（表示已消费），否则返回 false
bool ripHandlePacket(const String &packet, const String &from = "");

// 手动触发发送一次路由更新（用于调试/命令行）
void ripSendUpdate();

// 查询当前路由表的摘要（用于 UI/调试）
String ripGetRoutesSummary();

// 新增函数声明
// 获取所有路由的详细信息
std::vector<RouteEntry> ripFetchAllRoutes();

// 清空路由表
void ripClearRoutes();

// 手动删除特定路由
bool ripRemoveRoute(const String &dest);

// 限制条目数
const size_t RIP_MAX_ROUTES = 64;

// 导出 HC-12 实例的引用（若主文件定义了 hc12，可通过 extern 使用）
extern HC12Module hc12;

#endif // WM_RIP_H
