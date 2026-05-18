/**
 * @file    sys_discovery_cli.c
 * @brief   Hi3516 设备发现客户端 — UDP 组播查询
 *
 * 向局域网发送组播探测包 (239.0.0.1:8888)，
 * 请求格式: {"query":"device_discovery"}
 * 等待并打印设备回复的 JSON 信息。
 *
 * 编译 (本地 gcc):
 *   gcc -Wall -o sys_discovery_cli sys_discovery_cli.c
 *
 * 用法:
 *   ./sys_discovery_cli              # 发送查询, 等 3 秒
 *   ./sys_discovery_cli -t 5         # 等 5 秒
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define MULTICAST_ADDR  "239.0.0.1"
#define MULTICAST_PORT  8888
#define DISCOVER_MSG    "{\"query\":\"device_discovery\"}"
#define DEFAULT_TIMEOUT 3

static volatile int g_running = 1;

static void sig_handler(int signo)
{
    (void)signo;
    g_running = 0;
}

int main(int argc, char **argv)
{
    int                 sock;
    struct sockaddr_in  mcast_addr;
    int                 ttl     = 2;
    int                 timeout = DEFAULT_TIMEOUT;
    char                buf[8192];
    ssize_t             n;

    /* ---- 解析参数 ---- */
    if (argc > 1) {
        if (strcmp(argv[1], "-t") == 0 && argc > 2) {
            timeout = atoi(argv[2]);
        } else if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
            printf("Usage: %s [-t timeout_sec]\n", argv[0]);
            printf("  Send multicast discovery query to %s:%d\n",
                   MULTICAST_ADDR, MULTICAST_PORT);
            printf("  Query: %s\n", DISCOVER_MSG);
            return 0;
        }
    }

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    /* ---- 1. 创建 UDP socket ---- */
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket");
        return 1;
    }

    /* ---- 2. 设置组播 TTL ---- */
    if (setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL,
                   &ttl, sizeof(ttl)) < 0) {
        perror("IP_MULTICAST_TTL");
        close(sock);
        return 1;
    }

    /* ---- 3. 组播地址结构 ---- */
    memset(&mcast_addr, 0, sizeof(mcast_addr));
    mcast_addr.sin_family      = AF_INET;
    mcast_addr.sin_addr.s_addr = inet_addr(MULTICAST_ADDR);
    mcast_addr.sin_port        = htons(MULTICAST_PORT);

    /* ---- 4. 发送探测包 ---- */
    printf(">>> Sending discovery query to %s:%d ...\n",
           MULTICAST_ADDR, MULTICAST_PORT);
    printf(">>> Query: %s\n", DISCOVER_MSG);

    n = sendto(sock, DISCOVER_MSG, strlen(DISCOVER_MSG), 0,
               (struct sockaddr *)&mcast_addr, sizeof(mcast_addr));
    if (n < 0) {
        perror("sendto");
        close(sock);
        return 1;
    }

    /* ---- 5. 等待响应 ---- */
    printf(">>> Waiting %d seconds for responses... (Ctrl+C to stop)\n\n", timeout);

    {
        fd_set         rfds;
        struct timeval tv;

        while (g_running && timeout > 0) {
            FD_ZERO(&rfds);
            FD_SET(sock, &rfds);
            tv.tv_sec  = 1;
            tv.tv_usec = 0;

            if (select(sock + 1, &rfds, NULL, NULL, &tv) < 0) {
                if (errno == EINTR) { continue; }
                break;
            }

            if (FD_ISSET(sock, &rfds)) {
                struct sockaddr_in  sender;
                socklen_t           slen = sizeof(sender);

                n = recvfrom(sock, buf, sizeof(buf) - 1, 0,
                             (struct sockaddr *)&sender, &slen);
                if (n > 0) {
                    buf[n] = '\0';
                    printf("[%s] %s\n\n", inet_ntoa(sender.sin_addr), buf);
                    /* 收到后立即退出 */
                    g_running = 0;
                }
            } else {
                timeout--;
            }
        }
    }

    if (g_running) {
        printf(">>> No device found within timeout.\n");
    }

    close(sock);
    return 0;
}
