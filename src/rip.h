// rip.h
// 简易 RIP-like 子模块接口（适用于 ESP32 + HC-12 的学习/模拟实现）
// 提供：初始化、主循环、处理收到的 RIP 报文、查看路由表等

#ifndef WM_RIP_H
#define WM_RIP_H

#include <Arduino.h>
#include <vector>
#include <String.h>
#include "HC12_Module.h"

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

// 限制条目数
const size_t RIP_MAX_ROUTES = 64;

// 导出 HC-12 实例的引用（若主文件定义了 hc12，可通过 extern 使用）
extern HC12Module hc12;

#endif // WM_RIP_H
// rip.h
// 简单的 header-only RIP (RIPv2 风格) 协议子模块
// 提供：路由项结构、RIP 包的序列化/反序列化、路由表更新与过期处理
// 设计为独立单文件，易于在嵌入式项目中引用：#include "rip.h"
// 注：此实现为轻量教育/演示用途，省略了套接字和系统相关 IO，用户应在平台代码中负责发送/接收 UDP 数据并调用 handlePacket()

#ifndef WM_RIP_H
#define WM_RIP_H

#include <cstdint>
#include <vector>
#include <algorithm>
#include <cstring>

namespace wm
{

    // 最大跳数（RIP 常量）
    static constexpr uint32_t RIP_INFINITY = 16;

    // 路由项（使用 IPv4 32-bit 表示）
    struct RipRoute
    {
        uint32_t prefix;    // 网络地址 (host byte order)
        uint32_t mask;      // 网络掩码 (host byte order)
        uint32_t nextHop;   // 下一跳 IP (host byte order)
        uint32_t metric;    // 跳数 (1..16)
        uint32_t timeoutMs; // 过期计时器，单位毫秒
    };

    // RIP 协议处理类（header-only）
    class Rip
    {
    public:
        Rip(uint32_t defaultTimeoutMs = 180000) : defaultTimeoutMs_(defaultTimeoutMs) {}

        // 将收到的 RIP 数据包（raw UDP payload）交给模块解析并更新路由表
        // fromIp 为发送者的 IP（host byte order），可为 0 表示未知
        void handlePacket(const uint8_t *data, size_t len, uint32_t fromIp = 0)
        {
            if (!data || len < 4)
                return;
            uint8_t command = data[0];
            uint8_t version = data[1];
            // 仅支持 RIPv2 (version==2) 或 RIPv1（有限支持）
            if (version < 1 || version > 2)
                return;

            // 条目区从第4字节开始，每条 20 字节（RIPv2）
            size_t offset = 4;
            std::vector<RipRoute> entries;
            while (offset + 20 <= len)
            {
                // AFI (2 bytes) + route tag (2 bytes) + addr(4) + mask(4) + nextHop(4) + metric(4)
                const uint8_t *p = data + offset;
                uint16_t afi = (p[0] << 8) | p[1];
                // route tag ignored
                uint32_t addr = be32toh(*(const uint32_t *)(p + 4));
                uint32_t mask = be32toh(*(const uint32_t *)(p + 8));
                uint32_t nextHop = be32toh(*(const uint32_t *)(p + 12));
                uint32_t metric = be32toh(*(const uint32_t *)(p + 16));

                // basic validation
                if (metric == 0)
                    metric = RIP_INFINITY;
                if (metric > RIP_INFINITY)
                    metric = RIP_INFINITY;

                RipRoute r;
                r.prefix = addr;
                r.mask = mask;
                r.nextHop = (nextHop == 0 ? fromIp : nextHop);
                r.metric = metric;
                r.timeoutMs = defaultTimeoutMs_;
                entries.push_back(r);

                offset += 20;
            }

            // 根据命令决定如何处理；command 1=request，2=response
            if (command == 1)
            {
                // request — 可实现特殊处理（例如发送完整表）
                // 当前实现不主动回复 request，由上层决定
            }
            else if (command == 2)
            {
                // response — 使用距离向量规则更新本地路由表
                updateFromEntries(entries, fromIp);
            }
        }

        // 将本地路由表序列化为 RIPv2 response 格式
        void buildResponse(std::vector<uint8_t> &out, uint32_t srcIp = 0) const
        {
            out.clear();
            out.reserve(4 + routes_.size() * 20);
            out.push_back(2); // response
            out.push_back(2); // version
            out.push_back(0); // zero
            out.push_back(0);

            for (const auto &r : routes_)
            {
                // AFI = 2 (IP)
                out.push_back(0);
                out.push_back(2);
                // route tag
                out.push_back(0);
                out.push_back(0);
                // addr
                uint32_t a = htobe32(r.prefix);
                append32(out, a);
                // mask
                uint32_t m = htobe32(r.mask);
                append32(out, m);
                // nextHop
                uint32_t nh = htobe32(r.nextHop);
                append32(out, nh);
                // metric
                uint32_t met = htobe32(r.metric);
                append32(out, met);
            }
        }

        // 周期性函数（传入自上次调用以来的毫秒数），用于路由过期
        void tick(uint32_t elapsedMs)
        {
            if (routes_.empty())
                return;
            for (auto &r : routes_)
            {
                if (r.timeoutMs > elapsedMs)
                    r.timeoutMs -= elapsedMs;
                else
                    r.timeoutMs = 0;
            }
            // 移除过期的路由（timeout==0 或 metric==INF）
            routes_.erase(std::remove_if(routes_.begin(), routes_.end(), [](const RipRoute &r)
                                         { return r.timeoutMs == 0 || r.metric >= RIP_INFINITY; }),
                          routes_.end());
        }

        // 手工插入/更新路由（例如本地直连或静态路由）
        void addOrUpdateRoute(uint32_t prefix, uint32_t mask, uint32_t nextHop, uint32_t metric)
        {
            if (metric == 0)
                metric = 1;
            if (metric > RIP_INFINITY)
                metric = RIP_INFINITY;
            for (auto &r : routes_)
            {
                if (r.prefix == prefix && r.mask == mask)
                {
                    // 更新
                    r.nextHop = nextHop;
                    r.metric = metric;
                    r.timeoutMs = defaultTimeoutMs_;
                    return;
                }
            }
            RipRoute nr;
            nr.prefix = prefix;
            nr.mask = mask;
            nr.nextHop = nextHop;
            nr.metric = metric;
            nr.timeoutMs = defaultTimeoutMs_;
            routes_.push_back(nr);
        }

        // 获取当前路由表快照
        std::vector<RipRoute> getRoutes() const { return routes_; }

    private:
        std::vector<RipRoute> routes_;
        uint32_t defaultTimeoutMs_;

        static inline void append32(std::vector<uint8_t> &out, uint32_t v)
        {
            out.push_back((v >> 24) & 0xFF);
            out.push_back((v >> 16) & 0xFF);
            out.push_back((v >> 8) & 0xFF);
            out.push_back(v & 0xFF);
        }

        // 小端/大端辅助（在没有系统 htonl 的环境下安全工作）
        static inline uint32_t be32toh(uint32_t v)
        {
            // v is big-endian in memory (network order). Convert to host (assume little-endian common)
            return ((v & 0xFF) << 24) | ((v & 0xFF00) << 8) | ((v & 0xFF0000) >> 8) | ((v & 0xFF000000) >> 24);
        }
        static inline uint32_t htobe32(uint32_t v) { return be32toh(v); }

        // 将收到的条目应用到本地路由表（简单距离向量，邻居增加 1 跳）
        void updateFromEntries(const std::vector<RipRoute> &entries, uint32_t fromIp)
        {
            for (const auto &e : entries)
            {
                // 忽略不合理的条目
                if (e.metric >= RIP_INFINITY)
                    continue;
                uint32_t newMetric = e.metric + 1;
                if (newMetric > RIP_INFINITY)
                    newMetric = RIP_INFINITY;

                bool found = false;
                for (auto &r : routes_)
                {
                    if (r.prefix == e.prefix && r.mask == e.mask)
                    {
                        found = true;
                        // 若来自相同下一跳，刷新并更新 metric
                        if (r.nextHop == e.nextHop || r.nextHop == fromIp)
                        {
                            r.metric = newMetric;
                            r.nextHop = (e.nextHop ? e.nextHop : fromIp);
                            r.timeoutMs = defaultTimeoutMs_;
                        }
                        else
                        {
                            // 来自不同下一跳，采用更小的 metric
                            if (newMetric < r.metric)
                            {
                                r.metric = newMetric;
                                r.nextHop = (e.nextHop ? e.nextHop : fromIp);
                                r.timeoutMs = defaultTimeoutMs_;
                            }
                        }
                        break;
                    }
                }
                if (!found)
                {
                    RipRoute nr = e;
                    nr.metric = newMetric;
                    nr.nextHop = (e.nextHop ? e.nextHop : fromIp);
                    nr.timeoutMs = defaultTimeoutMs_;
                    routes_.push_back(nr);
                }
            }
        }
    };

} // namespace wm

#endif // WM_RIP_H
