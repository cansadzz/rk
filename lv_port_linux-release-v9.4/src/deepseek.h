/* deepseek.h - DeepSeek 登录页面声明 */
#ifndef DEEPSEEK_H
#define DEEPSEEK_H

void deepseek_exit_save(void);

#include "lvgl/lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

LV_FONT_DECLARE(song);

/* 创建并显示 DeepSeek 登录页面 */
void deepseek_login_screen_create(void);

/* 获取当前用户名/密码（返回静态缓存，调用者无需 free） */
const char *deepseek_get_username(void);
const char *deepseek_get_password(void);

#ifdef __cplusplus
}
#endif

#endif /* DEEPSEEK_H */
