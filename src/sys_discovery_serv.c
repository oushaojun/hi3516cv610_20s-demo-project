/**
 * @file    sys_discovery_serv.c
 * @brief   Hi3516CV610 设备发现服务 — UDP 组播响应
 *
 * 创建独立线程监听 239.0.0.1:8888。
 * 收到 {"query":"device_discovery"} 后，构造 JSON 回包:
 *   - 网络信息 (IP/MAC mask)
 *   - 系统信息 (hostname/kernel/uptime/memory)
 *   - 视频配置 (sensor/VI/各通道编码参数)
 *   - 时间戳 (毫秒)
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <sys/utsname.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <ifaddrs.h>
#include <linux/if_packet.h>

#include "sys_discovery_serv.h"
#include "sys_dbg.h"
#include "sys_thread.h"

#define MULTICAST_ADDR   "239.0.0.1"
#define MULTICAST_PORT   8888

/* 必须匹配的查询 JSON 键值 */
#define QUERY_MATCH     "\"query\":\"device_discovery\""

/* ====================================================================
 *  全局状态
 * ==================================================================== */

static const discovery_chn_cfg_t *g_chn_cfg = NULL;
static td_u32                      g_chn_cnt = 0;
static volatile td_bool            g_running = TD_FALSE;
static td_s32                      g_sock    = -1;
static thread_t                    g_thread;

/* ====================================================================
 *  JSON 序列化 helper: 追加 KV 对到缓冲区
 * ==================================================================== */

static td_s32 json_append_comma(td_char *buf, td_s32 *pos, td_s32 size)
{
    if (*pos < size - 2) { buf[(*pos)++] = ','; buf[*pos] = '\0'; }
    return *pos;
}

static td_s32 json_append_str(td_char *buf, td_s32 *pos, td_s32 size,
                               const td_char *key, const td_char *val)
{
    *pos += snprintf(buf + *pos, (size_t)(size - *pos), "\"%s\":\"%s\"", key, val);
    return *pos;
}

static td_s32 json_append_int(td_char *buf, td_s32 *pos, td_s32 size,
                               const td_char *key, td_s32 val)
{
    *pos += snprintf(buf + *pos, (size_t)(size - *pos), "\"%s\":%d", key, val);
    return *pos;
}

static td_s32 json_append_u32(td_char *buf, td_s32 *pos, td_s32 size,
                               const td_char *key, td_u32 val)
{
    *pos += snprintf(buf + *pos, (size_t)(size - *pos), "\"%s\":%u", key, val);
    return *pos;
}

/* ====================================================================
 *  网络信息 — 使用 getifaddrs 遍历
 * ==================================================================== */

static td_s32 json_append_network(td_char *buf, td_s32 *pos, td_s32 size)
{
    struct ifaddrs *ifaddr, *ifa;
    td_char         ip_str[INET_ADDRSTRLEN];
    td_char         mac_str[18];
    td_s32          first = 1;

    if (getifaddrs(&ifaddr) != 0) {
        return *pos;
    }

    *pos += snprintf(buf + *pos, (size_t)(size - *pos), "\"network\":{");

    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL) { continue; }

        /* 跳过环回接口 */
        if (strcmp(ifa->ifa_name, "lo") == 0) { continue; }

        /* IPv4 地址 */
        if (ifa->ifa_addr->sa_family == AF_INET) {
            struct sockaddr_in *sin = (struct sockaddr_in *)ifa->ifa_addr;

            if (!first) { json_append_comma(buf, pos, size); }
            first = 0;

            inet_ntop(AF_INET, &sin->sin_addr, ip_str, sizeof(ip_str));
            *pos += snprintf(buf + *pos, (size_t)(size - *pos),
                             "\"%s\":{", ifa->ifa_name);

            json_append_str(buf, pos, size, "ip", ip_str);

            /* MAC 地址 — 用 ioctl */
            {
                td_s32           sock = socket(AF_INET, SOCK_DGRAM, 0);
                struct ifreq      ifr;
                memset(&ifr, 0, sizeof(ifr));
                strncpy(ifr.ifr_name, ifa->ifa_name, IFNAMSIZ - 1);
                if (ioctl(sock, SIOCGIFHWADDR, &ifr) == 0) {
                    snprintf(mac_str, sizeof(mac_str),
                             "%02x:%02x:%02x:%02x:%02x:%02x",
                             (td_u8)ifr.ifr_hwaddr.sa_data[0],
                             (td_u8)ifr.ifr_hwaddr.sa_data[1],
                             (td_u8)ifr.ifr_hwaddr.sa_data[2],
                             (td_u8)ifr.ifr_hwaddr.sa_data[3],
                             (td_u8)ifr.ifr_hwaddr.sa_data[4],
                             (td_u8)ifr.ifr_hwaddr.sa_data[5]);
                    json_append_comma(buf, pos, size);
                    json_append_str(buf, pos, size, "mac", mac_str);
                }
                close(sock);
            }

            /* netmask */
            if (ifa->ifa_netmask != NULL) {
                struct sockaddr_in *nm = (struct sockaddr_in *)ifa->ifa_netmask;
                inet_ntop(AF_INET, &nm->sin_addr, ip_str, sizeof(ip_str));
                json_append_comma(buf, pos, size);
                json_append_str(buf, pos, size, "netmask", ip_str);
            }

            *pos += snprintf(buf + *pos, (size_t)(size - *pos), "}");
        }
    }

    freeifaddrs(ifaddr);
    *pos += snprintf(buf + *pos, (size_t)(size - *pos), "}");

    return *pos;
}

/* ====================================================================
 *  系统信息 — hostname / kernel / uptime / memory / cpu
 * ==================================================================== */

/**
 * @brief  读取 /proc/stat 第一行 cpu 总计，写入 fields[0..7]
 * @return 读取到的字段数 (>=8 为成功)
 */
static td_s32 read_cpu_stat(td_u64 fields[8])
{
    FILE *fp = fopen("/proc/stat", "r");
    td_s32 n = 0;
    td_char line[256];

    if (fp == NULL) { return 0; }
    if (fgets(line, sizeof(line), fp) != NULL) {
        n = sscanf(line, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
                   &fields[0], &fields[1], &fields[2], &fields[3],
                   &fields[4], &fields[5], &fields[6], &fields[7]);
    }
    fclose(fp);
    return n;
}

/**
 * @brief  获取 CPU 核心数 (统计 /proc/stat 中 cpuN 行)
 */
static td_s32 get_cpu_cores(td_void)
{
    FILE *fp = fopen("/proc/stat", "r");
    td_char line[64];
    td_s32 cores = 0;

    if (fp == NULL) { return 1; }
    while (fgets(line, sizeof(line), fp) != NULL) {
        if (strncmp(line, "cpu", 3) == 0 && line[3] >= '0' && line[3] <= '9') {
            cores++;
        }
    }
    fclose(fp);
    return (cores > 0) ? cores : 1;
}

static td_s32 json_append_cpu(td_char *buf, td_s32 *pos, td_s32 size)
{
    td_u64  a[8], b[8];
    td_u64  total_a, idle_a, total_b, idle_b;
    td_u64  total_delta, idle_delta;
    double  usage = 0.0;
    td_s32  cores;
    FILE   *fp;

    /* CPU 使用率 — 两次采样取差值 (1000ms, double 防整数截断) */
    if (read_cpu_stat(a) >= 4) {
        usleep(1000000);
        if (read_cpu_stat(b) >= 4) {
            total_a = a[0] + a[1] + a[2] + a[3] + a[4] + a[5] + a[6] + a[7];
            idle_a  = a[3] + a[4];
            total_b = b[0] + b[1] + b[2] + b[3] + b[4] + b[5] + b[6] + b[7];
            idle_b  = b[3] + b[4];

            total_delta = total_b - total_a;
            idle_delta  = idle_b  - idle_a;

            if (total_delta > 0) {
                usage = 100.0 - ((double)idle_delta * 100.0 / (double)total_delta);
            }
        }
    }

    cores = get_cpu_cores();

    *pos += snprintf(buf + *pos, (size_t)(size - *pos), "\"cpu\":{");

    *pos += snprintf(buf + *pos, (size_t)(size - *pos),
                     "\"usage_percent\":%.1f", usage);
    json_append_comma(buf, pos, size);
    json_append_int(buf, pos, size, "cores", cores);

    /* load average — /proc/loadavg */
    fp = fopen("/proc/loadavg", "r");
    if (fp != NULL) {
        double l1, l5, l15;
        if (fscanf(fp, "%lf %lf %lf", &l1, &l5, &l15) == 3) {
            td_char tmp[16];
            json_append_comma(buf, pos, size);
            snprintf(tmp, sizeof(tmp), "%.2f", l1);
            json_append_str(buf, pos, size, "load_1m", tmp);
            json_append_comma(buf, pos, size);
            snprintf(tmp, sizeof(tmp), "%.2f", l5);
            json_append_str(buf, pos, size, "load_5m", tmp);
            json_append_comma(buf, pos, size);
            snprintf(tmp, sizeof(tmp), "%.2f", l15);
            json_append_str(buf, pos, size, "load_15m", tmp);
        }
        fclose(fp);
    }

    *pos += snprintf(buf + *pos, (size_t)(size - *pos), "}");

    return *pos;
}

static td_s32 json_append_sysinfo(td_char *buf, td_s32 *pos, td_s32 size)
{
    td_char            tmp[256];
    struct utsname     uts;
    td_u32             uptime_sec = 0;
    td_u32             mem_total   = 0;
    td_u32             mem_free    = 0;
    td_u32             mem_avail   = 0;
    FILE              *fp;

    /* hostname */
    *pos += snprintf(buf + *pos, (size_t)(size - *pos), "\"system\":{");

    gethostname(tmp, sizeof(tmp));
    json_append_str(buf, pos, size, "hostname", tmp);

    /* kernel */
    json_append_comma(buf, pos, size);
    if (uname(&uts) == 0) {
        snprintf(tmp, sizeof(tmp), "%s %s", uts.sysname, uts.release);
    } else {
        snprintf(tmp, sizeof(tmp), "unknown");
    }
    json_append_str(buf, pos, size, "kernel", tmp);

    /* uptime — /proc/uptime */
    fp = fopen("/proc/uptime", "r");
    if (fp != NULL) {
        double up;
        if (fscanf(fp, "%lf", &up) == 1) {
            uptime_sec = (td_u32)up;
        }
        fclose(fp);
    }
    json_append_comma(buf, pos, size);
    json_append_u32(buf, pos, size, "uptime_sec", uptime_sec);

    /* memory — /proc/meminfo */
    fp = fopen("/proc/meminfo", "r");
    if (fp != NULL) {
        while (fgets(tmp, sizeof(tmp), fp) != NULL) {
            if (strncmp(tmp, "MemTotal:", 9) == 0) {
                sscanf(tmp + 9, "%u", &mem_total);
            } else if (strncmp(tmp, "MemFree:", 8) == 0) {
                sscanf(tmp + 8, "%u", &mem_free);
            } else if (strncmp(tmp, "MemAvailable:", 13) == 0) {
                sscanf(tmp + 13, "%u", &mem_avail);
            }
        }
        fclose(fp);
    }
    json_append_comma(buf, pos, size);
    json_append_u32(buf, pos, size, "mem_total_kb", mem_total);
    json_append_comma(buf, pos, size);
    json_append_u32(buf, pos, size, "mem_free_kb", mem_free);
    json_append_comma(buf, pos, size);
    json_append_u32(buf, pos, size, "mem_available_kb", mem_avail);

    /* cpu */
    json_append_comma(buf, pos, size);
    json_append_cpu(buf, pos, size);

    *pos += snprintf(buf + *pos, (size_t)(size - *pos), "}");

    return *pos;
}

/* ====================================================================
 *  视频配置
 * ==================================================================== */

static const td_char *codec_name(td_u32 type)
{
    switch (type) {
    case 96:   /* OT_PT_H264 */   return "H.264";
    case 265:  /* OT_PT_H265 */   return "H.265";
    case 1002: /* OT_PT_MJPEG */  return "MJPEG";
    default:                       return "??";
    }
}

static const td_char *rc_name(td_u32 mode)
{
    switch (mode) {
    case 0: return "ABR";
    case 1: return "CBR";
    case 2: return "VBR";
    case 3: return "AVBR";
    case 4: return "CVBR";
    case 5: return "QVBR";
    case 6: return "QPMAP";
    case 7: return "FIXQP";
    default: return "??";
    }
}

static td_s32 json_append_video(td_char *buf, td_s32 *pos, td_s32 size)
{
    td_u32 i;

    *pos += snprintf(buf + *pos, (size_t)(size - *pos),
                     "\"video\":{"
                     "\"sensor\":\"SC4336P\",");

    /* VI */
    json_append_u32(buf, pos, size, "vi_width",  2560);
    json_append_comma(buf, pos, size);
    json_append_u32(buf, pos, size, "vi_height", 1440);
    json_append_comma(buf, pos, size);
    json_append_u32(buf, pos, size, "vi_fps",    30);
    json_append_comma(buf, pos, size);

    /* 编码通道 */
    *pos += snprintf(buf + *pos, (size_t)(size - *pos),
                     "\"channels\":[");

    for (i = 0; i < g_chn_cnt; i++) {
        if (i > 0) { json_append_comma(buf, pos, size); }
        *pos += snprintf(buf + *pos, (size_t)(size - *pos), "{");

        json_append_int(buf, pos, size, "index", (td_s32)i);
        json_append_comma(buf, pos, size);
        json_append_str(buf, pos, size, "codec", codec_name(g_chn_cfg[i].type));
        json_append_comma(buf, pos, size);
        json_append_u32(buf, pos, size, "width",  g_chn_cfg[i].width);
        json_append_comma(buf, pos, size);
        json_append_u32(buf, pos, size, "height", g_chn_cfg[i].height);
        json_append_comma(buf, pos, size);
        json_append_u32(buf, pos, size, "fps",    g_chn_cfg[i].fps);
        json_append_comma(buf, pos, size);
        json_append_u32(buf, pos, size, "bitrate_kbps", g_chn_cfg[i].bitrate);
        json_append_comma(buf, pos, size);
        json_append_str(buf, pos, size, "rc_mode", rc_name(g_chn_cfg[i].rc_mode));

        *pos += snprintf(buf + *pos, (size_t)(size - *pos), "}");
    }

    *pos += snprintf(buf + *pos, (size_t)(size - *pos), "]");

    *pos += snprintf(buf + *pos, (size_t)(size - *pos), "}");

    return *pos;
}

/* ====================================================================
 *  时间戳 (毫秒精度)
 * ==================================================================== */

static td_void time_now_ms_str(td_char *out, td_s32 size)
{
    struct timeval  tv;
    struct tm       tm_info;
    time_t          now;

    gettimeofday(&tv, NULL);
    now = tv.tv_sec;
    localtime_r(&now, &tm_info);
    snprintf(out, (size_t)size, "%04d-%02d-%02dT%02d:%02d:%02d.%03lld",
             tm_info.tm_year + 1900, tm_info.tm_mon + 1,
             tm_info.tm_mday, tm_info.tm_hour, tm_info.tm_min,
             tm_info.tm_sec, (td_s64)(tv.tv_usec / 1000));
}

/* ====================================================================
 *  构造完整 JSON 响应
 * ==================================================================== */

#define JSON_BUF_SIZE   4096

static td_s32 build_discovery_json(td_char *buf, td_s32 size)
{
    td_char ts_str[32];
    td_s32  pos = 0;

    time_now_ms_str(ts_str, sizeof(ts_str));

    pos += snprintf(buf + pos, (size_t)(size - pos),
                    "{"
                    "\"type\":\"hi3516_discovery\",");
    json_append_str(buf, &pos, size, "device", "Hi3516CV610");
    json_append_comma(buf, &pos, size);
    json_append_str(buf, &pos, size, "time", ts_str);
    json_append_comma(buf, &pos, size);

    /* network */
    json_append_network(buf, &pos, size);
    json_append_comma(buf, &pos, size);

    /* system */
    json_append_sysinfo(buf, &pos, size);
    json_append_comma(buf, &pos, size);

    /* video */
    json_append_video(buf, &pos, size);

    pos += snprintf(buf + pos, (size_t)(size - pos), "}");

    return pos;
}

/* ====================================================================
 *  线程函数 — 接收查询 → 校验 JSON → 回复
 * ==================================================================== */

static td_void *discovery_thread(td_void *arg)
{
    struct sockaddr_in  sender_addr;
    socklen_t           sender_len = sizeof(sender_addr);
    td_char             recv_buf[1024];
    td_char             json_buf[JSON_BUF_SIZE];
    td_s32              n;
    td_s32              json_len;

    (td_void)arg;
    thread_set_name("discovery");

    DBG_LOG("DISC", "listening on %s:%d", MULTICAST_ADDR, MULTICAST_PORT);

    while (g_running) {
        memset(&sender_addr, 0, sizeof(sender_addr));
        sender_len = sizeof(sender_addr);

        n = recvfrom(g_sock, recv_buf, sizeof(recv_buf) - 1, 0,
                     (struct sockaddr *)&sender_addr, &sender_len);

        if (n < 0) {
            if (errno == EINTR) { continue; }
            //DBG_WARN("DISC", "recvfrom err: %s", strerror(errno));
            usleep(500000);
            continue;
        }

        if (!g_running) { break; }

        recv_buf[n] = '\0';
        DBG_LOG("DISC", "query from %s: %s",
                inet_ntoa(sender_addr.sin_addr), recv_buf);

        /* 校验查询格式: 必须包含 {"query":"device_discovery"} */
        if (strstr(recv_buf, QUERY_MATCH) == NULL) {
            DBG_WARN("DISC", "ignored unknown query");
            continue;
        }

        /* 构造 JSON 回包 */
        json_len = build_discovery_json(json_buf, sizeof(json_buf));

        /* 单播回复到发送者 */
        n = sendto(g_sock, json_buf, (size_t)json_len, 0,
                   (struct sockaddr *)&sender_addr, sender_len);
        if (n < 0) {
            DBG_ERROR("DISC", "sendto reply err: %s", strerror(errno));
        } else {
            DBG_LOG("DISC", "replied %d bytes to %s",
                    json_len, inet_ntoa(sender_addr.sin_addr));
        }
    }

    DBG_LOG("DISC", "thread stopped");
    return NULL;
}

/* ====================================================================
 *  公开接口
 * ==================================================================== */

td_s32 discovery_serv_init(const discovery_chn_cfg_t *chn_cfg, td_u32 chn_cnt)
{
    struct sockaddr_in  addr;
    struct ip_mreq      mreq;
    td_s32              reuse = 1;

    if (chn_cfg == NULL) { return TD_FAILURE; }

    g_chn_cfg = chn_cfg;
    g_chn_cnt = chn_cnt;

    /* 1. 创建 UDP socket */
    g_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (g_sock < 0) {
        DBG_ERROR("DISC", "socket err: %s", strerror(errno));
        return TD_FAILURE;
    }

    /* 2. 端口复用 */
    if (setsockopt(g_sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        DBG_ERROR("DISC", "SO_REUSEADDR err: %s", strerror(errno));
        close(g_sock);
        g_sock = -1;
        return TD_FAILURE;
    }

    /* 3. bind INADDR_ANY:8888 */
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(MULTICAST_PORT);

    if (bind(g_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        DBG_ERROR("DISC", "bind err: %s", strerror(errno));
        close(g_sock);
        g_sock = -1;
        return TD_FAILURE;
    }

    /* 4. 加入组播组 */
    memset(&mreq, 0, sizeof(mreq));
    mreq.imr_multiaddr.s_addr = inet_addr(MULTICAST_ADDR);
    mreq.imr_interface.s_addr = htonl(INADDR_ANY);

    if (setsockopt(g_sock, IPPROTO_IP, IP_ADD_MEMBERSHIP,
                   &mreq, sizeof(mreq)) < 0) {
        DBG_ERROR("DISC", "IP_ADD_MEMBERSHIP err: %s", strerror(errno));
        close(g_sock);
        g_sock = -1;
        return TD_FAILURE;
    }

    /* 5. 设置 recvfrom 超时 (1s)，便于检查 g_running 退出 */
    {
        struct timeval tv = { 1, 0 };
        setsockopt(g_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }

    /* 6. 启动监听线程 */
    g_running = TD_TRUE;
    if (thread_create(&g_thread, "discovery", 16*1024,
                      discovery_thread, NULL) != TD_SUCCESS) {
        DBG_ERROR("DISC", "thread_create failed");
        g_running = TD_FALSE;
        close(g_sock);
        g_sock = -1;
        return TD_FAILURE;
    }

    DBG_LOG("DISC", "init OK");
    return TD_SUCCESS;
}

td_void discovery_serv_deinit(td_void)
{
    if (!g_running) { return; }

    DBG_LOG("DISC", "deinit...");
    g_running = TD_FALSE;

    thread_join(g_thread);

    if (g_sock >= 0) {
        /* 离开组播组 */
        struct ip_mreq mreq;
        mreq.imr_multiaddr.s_addr = inet_addr(MULTICAST_ADDR);
        mreq.imr_interface.s_addr = htonl(INADDR_ANY);
        setsockopt(g_sock, IPPROTO_IP, IP_DROP_MEMBERSHIP,
                   &mreq, sizeof(mreq));
        close(g_sock);
        g_sock = -1;
    }

    DBG_LOG("DISC", "deinit done");
}
