#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <stdbool.h>
#include "cjson/cJSON.h"

#define PORT 11434
#define SERVER_IP "10.205.22.40"
#define BUFFER_SIZE 4096
#define MAX_POLL_ATTEMPTS 500
#define POLL_INTERVAL_US 200000
#define RESPONSE_TIMEOUT 100

static char last_response[BUFFER_SIZE] = {0};
static int sock = 0;
static bool request_sent = false;
static struct timeval request_start_time;
static bool stream_mode = true; // 默认非流式

// 切换 socket 阻塞/非阻塞模式
static void set_blocking(int fd, bool blocking) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return;
    if (blocking)
        flags &= ~O_NONBLOCK;
    else
        flags |= O_NONBLOCK;
    fcntl(fd, F_SETFL, flags);
}

// 删除子串
static void remove_substr(char *str, const char *sub) {
    size_t len = strlen(sub);
    if (!len) return;
    char *pos;
    while ((pos = strstr(str, sub))) {
        memmove(pos, pos + len, strlen(pos + len) + 1);
    }
}

// 解析HTTP响应体中的JSON，提取response字段（非流式用）
static int parse_response(const char *raw, char *out) {
    char *body = strstr(raw, "\r\n\r\n");
    if (!body) return 1;
    while (*body != '{') if (*body++ == '\0') return 1;

    cJSON *root = cJSON_Parse(body);
    if (!root) return 2;
    cJSON *resp = cJSON_GetObjectItem(root, "response");
    if (!resp) { cJSON_Delete(root); return 3; }
    strcpy(out, resp->valuestring);
    cJSON_Delete(root);
    return 0;
}

// 检查缓冲区中的JSON是否完整且可解析
static bool is_json_complete(const char *buf) {
    char *body = strstr(buf, "\r\n\r\n");
    if (!body) return false;
    while (*body != '{') {
        if (*body == '\0') return false;
        body++;
    }
    cJSON *root = cJSON_Parse(body);
    if (root) {
        cJSON_Delete(root);
        return true;
    }
    return false;
}

// 非阻塞连接服务器
static int connect_server() {
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -1;

    int flags = fcntl(sock, F_GETFL, 0);
    if (flags == -1 || fcntl(sock, F_SETFL, flags | O_NONBLOCK) == -1) return -2;

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    inet_pton(AF_INET, SERVER_IP, &addr.sin_addr);

    int ret = connect(sock, (struct sockaddr*)&addr, sizeof(addr));
    if (ret < 0 && errno != EINPROGRESS) return -3;

    fd_set fdset;
    FD_ZERO(&fdset);
    FD_SET(sock, &fdset);
    struct timeval tv = {3, 0};
    if (select(sock + 1, NULL, &fdset, NULL, &tv) <= 0) return -4;

    int err;
    socklen_t len = sizeof(err);
    getsockopt(sock, SOL_SOCKET, SO_ERROR, &err, &len);
    return err ? -5 : 0;
}

// 发送请求（支持流式/非流式）
static int send_request(const char *prompt, bool stream) {
    char body[1024];
    snprintf(body, sizeof(body),
        "{\"model\":\"deepseek-r1:1.5b\",\"prompt\":\"%s\",\"stream\":%s,\"include_context\":false}",
        prompt, stream ? "true" : "false");

    char headers[256];
    snprintf(headers, sizeof(headers),
        "POST /api/generate HTTP/1.1\r\nHost: %s:%d\r\nContent-Type: application/json\r\nContent-Length: %zu\r\n\r\n",
        SERVER_IP, PORT, strlen(body));

    char req[4096];
    snprintf(req, sizeof(req), "%s%s", headers, body);
    if (send(sock, req, strlen(req), 0) < 0) return -1;

    request_sent = true;
    gettimeofday(&request_start_time, NULL);
    return 0;
}

// 检查超时
static bool is_timeout() {
    struct timeval now;
    gettimeofday(&now, NULL);
    return (now.tv_sec - request_start_time.tv_sec) >= RESPONSE_TIMEOUT;
}

// 轮询读取数据（非流式用），返回：1=完整响应，0=数据不完整，负数=错误
static int poll_response(char *buf, int *len) {
    if (!request_sent) return -1;
    if (is_timeout()) return -2;

    char chunk[BUFFER_SIZE];
    int n = read(sock, chunk, sizeof(chunk) - 1);
    if (n > 0) {
        chunk[n] = '\0';
        strcat(buf + *len, chunk);
        *len += n;
        return is_json_complete(buf) ? 1 : 0;
    }

    if (n == 0) return -3;
    return (errno == EAGAIN || errno == EWOULDBLOCK) ? 0 : -4;
}

// 解析Unicode转义符
void parse_unicode_escape(char *str) {
    char tmp[BUFFER_SIZE];
    strcpy(tmp, str);
    int idx = 0;
    for (int i = 0; tmp[i]; i++) {
        if (tmp[i] == '\\' && tmp[i+1] == 'u') {
            if (strncmp(tmp+i+2, "003c", 4) == 0) {
                str[idx++] = '<'; i += 5;
            } else if (strncmp(tmp+i+2, "003e", 4) == 0) {
                str[idx++] = '>'; i += 5;
            }
        } else {
            str[idx++] = tmp[i];
        }
    }
    str[idx] = 0;
}

// 解析流式响应行，提取response字段并去重
void parse_json_response(const char *line, char *out) {
    const char *key = "\"response\":\"";
    char *pos = strstr(line, key);
    if (!pos) { out[0] = 0; return; }

    pos += strlen(key);
    int i = 0;
    while (*pos != '"' && *pos && i < BUFFER_SIZE-1) {
        if (*pos == '\\' && *(pos+1) == 'n') {
            out[i++] = '\n'; pos += 2;
        } else {
            out[i++] = *pos++;
        }
    }
    out[i] = 0;
    parse_unicode_escape(out);

    // 去重：和上一条一样则清空
    if (strcmp(out, last_response) == 0) out[0] = 0;
    else strcpy(last_response, out);
}

// 判断流式是否结束
int is_stream_done(const char *line) {
    return strstr(line, "\"done\":true") != NULL;
}

char* replace(const char* res, const char* src, const char* dst) {
    if (!res || !src || !dst) return NULL;

    size_t res_len = strlen(res), src_len = strlen(src), dst_len = strlen(dst);
    if (!src_len) return strdup(res);

    // 计算出现次数
    size_t count = 0;
    for (const char* p = res; (p = strstr(p, src)); p += src_len) count++;

    // 分配结果缓冲区
    char* result = malloc(res_len + count * (dst_len - src_len) + 1);
    if (!result) return NULL;

    // 执行替换
    char* out = result;
    const char* in = res;
    while (*in) {
        if (strncmp(in, src, src_len) == 0) {
            memcpy(out, dst, dst_len);
            out += dst_len;
            in  += src_len;
        } else {
            *out++ = *in++;
        }
    }
    *out = '\0';
    return result;
}

// 接收流式响应并打印（阻塞式）
void receive_and_print_stream(int sockfd) {
    char buf[BUFFER_SIZE], res[BUFFER_SIZE];
    ssize_t n;
    int header_end = 0;

    while ((n = recv(sockfd, buf, BUFFER_SIZE-1, 0)) > 0) {
        buf[n] = 0;
        char *line = strtok(buf, "\n");
        while (line) {
            if (!header_end) {
                if (strlen(line) <= 2 && strstr(line, "\r")) header_end = 1;
                line = strtok(NULL, "\n");
                continue;
            }

            parse_json_response(line, res);
            if (strlen(res) > 0) {
                strcpy(res, replace(res, "<think>","思考中..."));
                strcpy(res, replace(res, "</think>","回答中..."));
                
                
                printf("%s", res);
                fflush(stdout);
            }

            if (is_stream_done(line)) {
                printf("\n\n");
                memset(last_response, 0, sizeof(last_response));
                return;
            }
            line = strtok(NULL, "\n");
        }
    }
}

int main() {
    if (connect_server() != 0) {
        fprintf(stderr, "连接失败\n");
        return 1;
    }

    char prompt[1024], full_buf[BUFFER_SIZE * 4] = {0};
    int total = 0, attempts = 0;

    while (1) {
        if (!request_sent) {
            printf("\n输入stream或nostream切换模式，或直接输入问题：\n");
            printf("(%s)>>> ", stream_mode ? "stream" : "nostream");
            if (!fgets(prompt, sizeof(prompt), stdin)) continue;
            prompt[strcspn(prompt, "\n")] = 0;

            // 检查模式切换命令
            if (strcmp(prompt, "stream") == 0) {
                stream_mode = true;
                printf("已切换到流式模式。\n");
                continue;
            }
            if (strcmp(prompt, "nostream") == 0) {
                stream_mode = false;
                printf("已切换到非流式模式。\n");
                continue;
            }

            // 非流式初始化缓冲区
            if (!stream_mode) {
                memset(full_buf, 0, sizeof(full_buf));
                total = 0;
                attempts = 0;
            }

            if (send_request(prompt, stream_mode) != 0) {
                printf("发送失败\n");
                continue;
            }

            printf("请求已发送，等待响应...\n");

            if (stream_mode) {
                // 流式接收：临时设为阻塞模式
                set_blocking(sock, true);
                receive_and_print_stream(sock);
                set_blocking(sock, false);
                request_sent = false; // 流式接收完毕
            }
            // 非流式将在下一轮循环中轮询
        } else {
            // 非流式轮询处理
            int res = poll_response(full_buf, &total);
            if (res == 1) {
                char result[BUFFER_SIZE * 2] = {0};
                if (parse_response(full_buf, result) == 0) {
                    remove_substr(result, "</think>");
                    remove_substr(result, "<think>");
                    remove_substr(result, "\n\n");
                    printf(" %s\n", result);
                } else {
                    printf("解析失败\n");
                }
                request_sent = false;
            } else if (res < 0) {
                printf("读取错误 (%d)\n", res);
                request_sent = false;
            } else {
                printf("等待响应中...%ds\r", attempts / 5);
                fflush(stdout);
                usleep(POLL_INTERVAL_US);
                if (++attempts > MAX_POLL_ATTEMPTS) {
                    printf("\n轮询超时\n");
                    request_sent = false;
                }
            }
        }
    }

    close(sock);
    return 0;
}