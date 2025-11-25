// rip.cpp
// 简易 RIP-like 协议实现（教学/模拟用途）

#include "rip.h"
#include <esp_system.h>

static std::vector<RouteEntry> routeTable;
static const unsigned long ROUTE_TIMEOUT_MS = 30000;   // 30s 未见则失效
static const unsigned long UPDATE_INTERVAL_MS = 10000; // 10s 周期发送
static unsigned long lastUpdateTime = 0;
// 设备唯一标识（由 MAC 派生），用于在 RIP 广播中标识本节点
static String selfId = "";

// 从 ESP32 efuse MAC 生成 12 字符十六进制 ID（大写，无分隔符）
static String generateSelfId()
{
    uint64_t mac = ESP.getEfuseMac();
    char buf[13];
    for (int i = 0; i < 6; ++i)
    {
        uint8_t byte = (mac >> (8 * (5 - i))) & 0xFF;
        sprintf(buf + i * 2, "%02X", byte);
    }
    buf[12] = '\0';
    return String(buf);
}

void ripInit()
{
    routeTable.clear();
    lastUpdateTime = millis();
    if (selfId.length() == 0)
        selfId = generateSelfId();
    Serial.print("RIP initialized, id=");
    Serial.println(selfId);
}

static void addOrUpdateRoute(const String &dest, uint16_t metric)
{
    for (auto &e : routeTable)
    {
        if (e.dest == dest)
        {
            e.metric = metric;
            e.lastSeen = millis();
            return;
        }
    }
    if (routeTable.size() >= RIP_MAX_ROUTES)
        routeTable.erase(routeTable.begin());
    RouteEntry ne;
    ne.dest = dest;
    ne.metric = metric;
    ne.lastSeen = millis();
    routeTable.push_back(ne);
}

void ripLoop()
{
    unsigned long now = millis();
    // 老化
    for (int i = (int)routeTable.size() - 1; i >= 0; --i)
    {
        if (now - routeTable[i].lastSeen > ROUTE_TIMEOUT_MS)
        {
            Serial.print("RIP: Removing stale route: ");
            Serial.println(routeTable[i].dest);
            routeTable.erase(routeTable.begin() + i);
        }
    }

    // 定期发送 UPDATE
    if (now - lastUpdateTime >= UPDATE_INTERVAL_MS)
    {
        ripSendUpdate();
        lastUpdateTime = now;
    }
}

// 格式： RIP|UPDATE|node1:metric,node2:metric
void ripSendUpdate()
{
    String payload = "RIP|UPDATE|";
    // 将本节点自身用唯一 ID 广播，metric=1
    payload += selfId;
    payload += ":1";
    // 附带已知路由
    for (auto &e : routeTable)
    {
        payload += ",";
        payload += e.dest;
        payload += ":";
        payload += String(e.metric);
    }
    // 通过 HC-12 广播
    hc12.setMode(HC12Module::COMM_MODE);
    hc12.sendData(payload);
    Serial.print("RIP: Sent UPDATE: ");
    Serial.println(payload);
}

bool ripHandlePacket(const String &packet, const String &from)
{
    if (!packet.startsWith("RIP|"))
        return false;
    Serial.print("RIP: Handling packet: ");
    Serial.println(packet);

    // 简单解析
    // RIP|UPDATE|node:metric,node2:metric
    int p1 = packet.indexOf('|');
    int p2 = packet.indexOf('|', p1 + 1);
    if (p1 < 0 || p2 < 0)
        return true; // 吃掉格式不对的 RIP 报文

    String cmd = packet.substring(p1 + 1, p2);
    String body = packet.substring(p2 + 1);

    if (cmd == "UPDATE")
    {
        // body like node:metric,node:metric
        int idx = 0;
        while (idx < body.length())
        {
            int comma = body.indexOf(',', idx);
            String part;
            if (comma < 0)
            {
                part = body.substring(idx);
                idx = body.length();
            }
            else
            {
                part = body.substring(idx, comma);
                idx = comma + 1;
            }
            int colon = part.indexOf(':');
            if (colon > 0)
            {
                String node = part.substring(0, colon);
                String mstr = part.substring(colon + 1);
                uint16_t metric = (uint16_t)mstr.toInt();
                // 增加跳数惩罚（通过此节点传递 +1），但这里我们只把收到条目直接记录
                addOrUpdateRoute(node, metric + 1);
                Serial.print("RIP: Add/Update route: ");
                Serial.print(node);
                Serial.print(" metric=");
                Serial.println(metric + 1);
            }
        }
    }

    return true; // 已处理
}

String ripGetRoutesSummary()
{
    String s = "RIP routes:";
    for (auto &e : routeTable)
    {
        s += " ";
        s += e.dest;
        s += ":";
        s += String(e.metric);
    }
    return s;
}

// 添加对 RIP 功能的完整实现

// 定义一个函数，用于从路由表中获取所有路由的详细信息
std::vector<RouteEntry> ripFetchAllRoutes()
{
    return routeTable;
}

// 定义一个函数，用于清空路由表（调试或重置时使用）
void ripClearRoutes()
{
    routeTable.clear();
    Serial.println("RIP: Route table cleared.");
}

// 定义一个函数，用于手动删除特定路由
bool ripRemoveRoute(const String &dest)
{
    for (int i = 0; i < routeTable.size(); ++i)
    {
        if (routeTable[i].dest == dest)
        {
            routeTable.erase(routeTable.begin() + i);
            Serial.print("RIP: Removed route to ");
            Serial.println(dest);
            return true;
        }
    }
    return false;
}
