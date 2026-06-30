#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// 你原来的执行命令（可根据路径修改）
//#define COMMAND "./rknn_zipformer_demo ./model/encoder-epoch-99-avg-1.rknn ./model/decoder-epoch-99-avg-1.rknn ./model/joiner-epoch-99-avg-1.rknn ./model/test.wav"

#define VOICE_RECOGNIZE_CMD "./rknn_zipformer_demo"
#define MODEL_DIR "./model/"


char *get_zipformer_result(void)
{
    FILE *fp;
    char tempbuf[1024];
    char *result = NULL;
    char command[512];
    
    // 构建命令
    snprintf(command, sizeof(command), 
             "%s %sencoder-epoch-99-avg-1.rknn %sdecoder-epoch-99-avg-1.rknn %sjoiner-epoch-99-avg-1.rknn %s1.wav",
             VOICE_RECOGNIZE_CMD, MODEL_DIR, MODEL_DIR, MODEL_DIR, MODEL_DIR);

    // 执行命令并打开输出流
    fp = popen(command, "r");
    if (fp == NULL) {
        printf("popen 执行失败\n");
        return NULL;
    }

    // 逐行读取输出
    while (fgets(tempbuf, sizeof(tempbuf), fp) != NULL) {
        // 找到识别结果行
        char *key = strstr(tempbuf, "Zipformer output: ");
        if (key != NULL) {
            // 跳过前缀，拿到真正的识别文字
            key += strlen("Zipformer output: ");

            // 分配内存并复制结果
            result = (char *)malloc(strlen(key) + 1);
            if (result != NULL) {
                strcpy(result, key);

                // 去掉换行符（干净输出）
                char *newline = strchr(result, '\n');
                if (newline) *newline = 0;
            }
            break;
        }
    }

    // 关闭流
    pclose(fp);

    return result;
}
