#ifndef VOICE_RECOGNIZE_H
#define VOICE_RECOGNIZE_H

#ifdef __cplusplus
extern "C" {
#endif

// 语音识别函数
// 返回值：成功返回识别结果字符串，失败返回 NULL
// 注意：返回的字符串需要手动 free()
char *get_zipformer_result(void);

#ifdef __cplusplus
}
#endif

#endif