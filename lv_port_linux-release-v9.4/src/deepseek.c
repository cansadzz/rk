/* deepseek.c - DeepSeek 登录页面实现 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "deepseek.h"
#include <errno.h>
#include "../lvgl/src/font/lv_symbol_def.h"
#include "../lvgl/src/font/lv_font.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>

#define SERVER_IP "10.180.75.40" // 根据实际AI服务地址修改
#define PORT 11434
#define BUFFER_SIZE 4096
#define POLL_INTERVAL_US 200000
#define RESPONSE_TIMEOUT 100

static int ai_sock          = 0;
static bool ai_request_sent = false;
static struct timeval ai_request_start_time;

static void ds_autologin_check_cb(lv_event_t * e);

/* 局部静态对象 */
static lv_obj_t * ds_login_screen    = NULL;
static lv_obj_t * ds_username_ta     = NULL;
static lv_obj_t * ds_password_ta     = NULL;
static lv_obj_t * ds_result_label    = NULL;
static lv_obj_t * ds_remember_check  = NULL;
static lv_obj_t * ds_autologin_check = NULL;
static lv_obj_t * ds_kb              = NULL;
static lv_obj_t * ds_ime             = NULL;
static lv_obj_t * ds_cont            = NULL;

static lv_obj_t * ds_main_scr          = NULL;
static lv_obj_t * ds_main_input_ta     = NULL;
static lv_obj_t * ds_chat_cont         = NULL;
static lv_obj_t * ds_main_kb           = NULL;
static lv_obj_t * ds_main_ime          = NULL;
static lv_obj_t * ds_main_result_label = NULL;
static lv_coord_t ds_chat_next_y       = 8;

/* 语音识别相关 */
static int ds_voice_recognizing    = 0;
static lv_timer_t * ds_voice_timer = NULL;
static int ds_voice_recording      = 0;
static void ds_voice_btn_pressed_cb(lv_event_t * e);
static void ds_voice_btn_released_cb(lv_event_t * e);
static void ds_voice_recognize_timer_cb(lv_timer_t * timer);

/* forward declarations */
void deepseek_main_screen_create(void);
static void ds_voice_btn_pressed_cb(lv_event_t * e);
static void ds_voice_btn_released_cb(lv_event_t * e);
static void ds_perform_recognition(void);
static void check_ai_response(lv_timer_t * timer);
static void ds_update_history_list(void);
static void ds_history_item_cb(lv_event_t * e);
static void ds_save_chat_histories(void);
static void ds_load_chat_histories(void);
static void ds_create_new_chat(void);
static void ds_save_message_to_history(const char * content, int is_user);

static char ds_username_buf[128] = {0};
static char ds_password_buf[128] = {0};
static int ds_remember_checked   = 0;
static int ds_autologin_checked  = 0;

static void ds_delete_conv_btn_cb(lv_event_t * e);

/* 历史对话记录相关 */
#define MAX_HISTORY_COUNT 20
#define MAX_MESSAGE_COUNT 50
#define MAX_MESSAGE_LENGTH 512

// 对话消息结构
typedef struct
{
    char content[MAX_MESSAGE_LENGTH];
    int is_user; // 1表示用户消息，0表示AI消息
} ds_message_t;

// 对话记录结构
typedef struct
{
    char title[128];
    ds_message_t messages[MAX_MESSAGE_COUNT];
    int message_count;
} ds_chat_history_t;

static ds_chat_history_t ds_chat_histories[MAX_HISTORY_COUNT];
static int ds_history_count       = 0;
static int ds_current_chat_index  = -1;
static lv_obj_t * ds_history_list = NULL;

static int send_ai_request(const char * prompt)
{
    // 1. 转义特殊字符（防止JSON出错）
    char escaped_prompt[512] = {0};
    int j                    = 0;
    for(int i = 0; prompt[i] && j < sizeof(escaped_prompt) - 1; i++) {
        if(prompt[i] == '"' || prompt[i] == '\\') {
            escaped_prompt[j++] = '\\';
        }
        escaped_prompt[j++] = prompt[i];
    }

    // 2. 构建请求体
    char body[1024];
    snprintf(body, sizeof(body),
             "{\"model\":\"deepseek-r1:1.5b\",\"prompt\":\"%s\",\"stream\":false,\"include_context\":false}",
             escaped_prompt);

    // 3. 构建HTTP头
    size_t body_len = strlen(body);
    char headers[256];
    snprintf(headers, sizeof(headers),
             "POST /api/generate HTTP/1.1\r\n"
             "Host: %s:%d\r\n"
             "Content-Type: application/json\r\n"
             "Content-Length: %zu\r\n"
             "Connection: close\r\n\r\n",
             SERVER_IP, PORT, body_len);

    // 4. 拼接并发送
    char req[4096];
    int req_len = snprintf(req, sizeof(req), "%s%s", headers, body);

    ssize_t sent = send(ai_sock, req, req_len, 0);
    if(sent < 0) {
        printf("发送失败：%s\n", strerror(errno));
        return -1;
    }
    printf("成功发送文本到WSL：%s\n", prompt);
    return 0;
}

static void ds_set_result_text(const char * txt)
{
    lv_obj_t * current_scr  = lv_scr_act();
    lv_obj_t * target_label = NULL;

    if(current_scr == ds_main_scr) {
        target_label = ds_main_result_label;
    } else {
        target_label = ds_result_label;
    }

    if(!target_label) return;
    lv_label_set_text(target_label, txt);
    lv_obj_set_style_text_font(target_label, &song, 0);
    lv_obj_invalidate(target_label);
}

static void ds_save_login_info(void)
{
    const char * config_path = "deepseek_login_config.txt";
    FILE * fw                = fopen(config_path, "w");
    if(!fw) {
        return;
    }

    fprintf(fw, "username=%s\n", ds_username_buf);
    if(ds_remember_checked) {
        fprintf(fw, "password=%s\n", ds_password_buf);
    } else {
        fprintf(fw, "password=\n");
    }
    fprintf(fw, "remember=%d\n", ds_remember_checked);
    fprintf(fw, "autologin=%d\n", ds_autologin_checked);

    fclose(fw);
}

static void ds_load_login_info(void)
{
    const char * config_path = "deepseek_login_config.txt";
    FILE * fr                = fopen(config_path, "r");
    if(!fr) {
        return;
    }

    char line[256];
    while(fgets(line, sizeof(line), fr)) {
        /* 移除换行符 */
        char * nl = strchr(line, '\n');
        if(nl) *nl = '\0';

        /* 解析键值对 */
        char * sep = strchr(line, '=');
        if(!sep) continue;
        *sep         = '\0';
        char * key   = line;
        char * value = sep + 1;

        if(strcmp(key, "username") == 0) {
            strncpy(ds_username_buf, value, sizeof(ds_username_buf) - 1);
            ds_username_buf[sizeof(ds_username_buf) - 1] = '\0';
        } else if(strcmp(key, "password") == 0) {
            strncpy(ds_password_buf, value, sizeof(ds_password_buf) - 1);
            ds_password_buf[sizeof(ds_password_buf) - 1] = '\0';
        } else if(strcmp(key, "remember") == 0) {
            ds_remember_checked = atoi(value);
        } else if(strcmp(key, "autologin") == 0) {
            ds_autologin_checked = atoi(value);
        }
    }

    fclose(fr);
}

static void ds_login_timer_cb(lv_timer_t * timer)
{
    (void)timer;
    ds_set_result_text("登录成功");
    /* 登录成功后切换到主界面，使用动画效果 */
    deepseek_main_screen_create();
    if(ds_main_scr) {
        lv_scr_load_anim(ds_main_scr, LV_SCR_LOAD_ANIM_MOVE_LEFT, 500, 0, false);
    }
}

static void ds_login_btn_cb(lv_event_t * e)
{
    (void)e;
    const char * u = lv_textarea_get_text(ds_username_ta);
    const char * p = lv_textarea_get_text(ds_password_ta);
    /* 验证用户名/密码：从 deepseek_users.txt 中查找匹配项 */
    if(!u || !p || strlen(u) == 0 || strlen(p) == 0) {
        ds_set_result_text("请输入账号和密码");
        return;
    }

    /* 保存复选框状态 */
    ds_remember_checked  = lv_obj_has_state(ds_remember_check, LV_STATE_CHECKED);
    ds_autologin_checked = lv_obj_has_state(ds_autologin_check, LV_STATE_CHECKED);

    const char * db_path = "deepseek_users.txt";
    FILE * fr            = fopen(db_path, "r");
    if(!fr) {
        ds_set_result_text("用户数据库不存在，请先注册");
        return;
    }

    char line[512];
    int found = 0;
    while(fgets(line, sizeof(line), fr)) {
        /* 支持行格式 username:password\n */
        char * sep = strchr(line, ':');
        if(!sep) continue;
        *sep               = '\0';
        char * stored_user = line;
        char * stored_pass = sep + 1;
        /* 去掉 stored_pass 的换行 */
        char * nl = strchr(stored_pass, '\n');
        if(nl) *nl = '\0';

        if(strcmp(stored_user, u) == 0) {
            found = 1;
            /* 直接比较（当前示例使用明文存储比较） */
            if(strcmp(stored_pass, p) == 0) {
                /* 登录成功：保存缓存并显示 */
                strncpy(ds_username_buf, u, sizeof(ds_username_buf) - 1);
                ds_username_buf[sizeof(ds_username_buf) - 1] = '\0';
                strncpy(ds_password_buf, p, sizeof(ds_password_buf) - 1);
                ds_password_buf[sizeof(ds_password_buf) - 1] = '\0';

                /* 保存登录信息 */
                ds_save_login_info();

                ds_set_result_text("正在登录...");
                lv_timer_t * t = lv_timer_create(ds_login_timer_cb, 800, NULL);
                if(t) lv_timer_set_repeat_count(t, 1);
                fclose(fr);
                return;
            } else {
                ds_set_result_text("密码错误");
                fclose(fr);
                return;
            }
        }
    }
    fclose(fr);
    if(!found) {
        ds_set_result_text("用户未注册");
    }
}

static void ds_register_btn_cb(lv_event_t * e)
{
    (void)e;
    const char * u = lv_textarea_get_text(ds_username_ta);
    const char * p = lv_textarea_get_text(ds_password_ta);

    if(!u || !p || strlen(u) == 0 || strlen(p) == 0) {
        ds_set_result_text("请输入注册的账号和密码");
        return;
    }

    /* 存储路径（可改为更合适的位置） */
    const char * db_path = "deepseek_users.txt";

    /* 检查是否已存在用户名 */
    FILE * fr = fopen(db_path, "r");
    if(fr) {
        char line[512];
        while(fgets(line, sizeof(line), fr)) {
            /* 行格式 username:password\n */
            char * sep = strchr(line, ':');
            if(!sep) continue;
            size_t un_len = sep - line;
            /* 去掉行尾换行 */
            char stored_user[256] = {0};
            if(un_len >= sizeof(stored_user)) un_len = sizeof(stored_user) - 1;
            strncpy(stored_user, line, un_len);
            stored_user[un_len] = '\0';
            if(strcmp(stored_user, u) == 0) {
                fclose(fr);
                ds_set_result_text("该账号已注册");
                return;
            }
        }
        fclose(fr);
    }

    /* 追加写入 */
    FILE * fw = fopen(db_path, "a");
    if(!fw) {
        char errbuf[128];
        snprintf(errbuf, sizeof(errbuf), "写入失败: %s", strerror(errno));
        ds_set_result_text(errbuf);
        return;
    }

    /* 直接存明文（示例）。生产环境请使用哈希+盐并妥善保护文件权限。 */
    if(fprintf(fw, "%s:%s\n", u, p) < 0) {
        char errbuf[128];
        snprintf(errbuf, sizeof(errbuf), "写入失败: %s", strerror(errno));
        ds_set_result_text(errbuf);
        fclose(fw);
        return;
    }
    fclose(fw);

    ds_set_result_text("注册成功");
}

static void ds_autologin_check_cb(lv_event_t * e)
{
    lv_obj_t * checkbox = lv_event_get_target(e);
    bool checked        = lv_obj_has_state(checkbox, LV_STATE_CHECKED);

    if(checked) {
        lv_obj_add_state(ds_remember_check, LV_STATE_CHECKED);
        ds_remember_checked = 1;
    }
    // 实时保存自动登录状态，不再等退出时存
    ds_autologin_checked = checked;
    ds_save_login_info();
}

/* Textarea focus event: show keyboard and attach textarea */
static void ds_ta_event_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * ta        = lv_event_get_target(e);
    if(code == LV_EVENT_FOCUSED) {
        if(ds_kb) {
            lv_keyboard_set_textarea(ds_kb, ta);
            lv_obj_clear_flag(ds_kb, LV_OBJ_FLAG_HIDDEN);
            if(ds_ime) {
                lv_obj_clear_flag(ds_ime, LV_OBJ_FLAG_HIDDEN);
                /* 显示候选框面板 */
                lv_obj_t * cand_panel = lv_ime_pinyin_get_cand_panel(ds_ime);
                if(cand_panel) lv_obj_clear_flag(cand_panel, LV_OBJ_FLAG_HIDDEN);
            }
            /* 把容器略微上移以腾出键盘空间 */
            if(ds_cont) lv_obj_align(ds_cont, LV_ALIGN_CENTER, 0, -100);
        }
    } else if(code == LV_EVENT_DEFOCUSED) {
        if(ds_kb) lv_obj_add_flag(ds_kb, LV_OBJ_FLAG_HIDDEN);
        if(ds_ime) {
            lv_obj_add_flag(ds_ime, LV_OBJ_FLAG_HIDDEN);
            /* 隐藏候选框面板 */
            lv_obj_t * cand_panel = lv_ime_pinyin_get_cand_panel(ds_ime);
            if(cand_panel) lv_obj_add_flag(cand_panel, LV_OBJ_FLAG_HIDDEN);
        }
        if(ds_cont) lv_obj_center(ds_cont);
    }
}

/* Main screen Textarea focus event: show main keyboard and attach textarea */
static void ds_main_ta_event_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * ta        = lv_event_get_target(e);
    if(code == LV_EVENT_FOCUSED) {
        if(ds_main_kb) {
            lv_keyboard_set_textarea(ds_main_kb, ta);
            lv_obj_clear_flag(ds_main_kb, LV_OBJ_FLAG_HIDDEN);
            if(ds_main_ime) {
                lv_obj_clear_flag(ds_main_ime, LV_OBJ_FLAG_HIDDEN);
                /* 显示候选框面板 */
                lv_obj_t * cand_panel = lv_ime_pinyin_get_cand_panel(ds_main_ime);
                if(cand_panel) lv_obj_clear_flag(cand_panel, LV_OBJ_FLAG_HIDDEN);
            }
        }
        /* 上浮输入框容器，避免被键盘和输入法遮挡 */
        lv_obj_t * input_cont = lv_obj_get_parent(ta);
        if(input_cont) {
            lv_obj_align(input_cont, LV_ALIGN_BOTTOM_MID, 100, -350);
        }
    } else if(code == LV_EVENT_DEFOCUSED) {
        if(ds_main_kb) lv_obj_add_flag(ds_main_kb, LV_OBJ_FLAG_HIDDEN);
        if(ds_main_ime) {
            lv_obj_add_flag(ds_main_ime, LV_OBJ_FLAG_HIDDEN);
            /* 隐藏候选框面板 */
            lv_obj_t * cand_panel = lv_ime_pinyin_get_cand_panel(ds_main_ime);
            if(cand_panel) lv_obj_add_flag(cand_panel, LV_OBJ_FLAG_HIDDEN);
        }
        /* 恢复输入框容器位置 */
        lv_obj_t * input_cont = lv_obj_get_parent(ta);
        if(input_cont) {
            lv_obj_align(input_cont, LV_ALIGN_BOTTOM_MID, 100, -16);
        }
    }
}

/* Keyboard event: hide on cancel/ready and restore container position */
static void ds_kb_event_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_CANCEL || code == LV_EVENT_READY) {
        if(ds_kb) lv_obj_add_flag(ds_kb, LV_OBJ_FLAG_HIDDEN);
        if(ds_ime) {
            lv_obj_add_flag(ds_ime, LV_OBJ_FLAG_HIDDEN);
            /* 隐藏候选框面板 */
            lv_obj_t * cand_panel = lv_ime_pinyin_get_cand_panel(ds_ime);
            if(cand_panel) lv_obj_add_flag(cand_panel, LV_OBJ_FLAG_HIDDEN);
        }
        /* restore container position */
        if(ds_cont) lv_obj_center(ds_cont);
    }
}

/* Main keyboard event: hide on cancel/ready */
static void ds_main_kb_event_cb(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_CANCEL || code == LV_EVENT_READY) {
        if(ds_main_kb) lv_obj_add_flag(ds_main_kb, LV_OBJ_FLAG_HIDDEN);
        if(ds_main_ime) {
            lv_obj_add_flag(ds_main_ime, LV_OBJ_FLAG_HIDDEN);
            /* 隐藏候选框面板 */
            lv_obj_t * cand_panel = lv_ime_pinyin_get_cand_panel(ds_main_ime);
            if(cand_panel) lv_obj_add_flag(cand_panel, LV_OBJ_FLAG_HIDDEN);
        }
        /* 恢复输入框容器位置 */
        if(ds_main_input_ta) {
            lv_obj_t * input_cont = lv_obj_get_parent(ds_main_input_ta);
            if(input_cont) {
                lv_obj_align(input_cont, LV_ALIGN_BOTTOM_MID, 100, -16);
            }
        }
    }
}

/* 点击屏幕其他区域时隐藏键盘（如果点击目标不是输入框或键盘本身） */
static void ds_hide_kb_on_click(lv_event_t * e)
{
    lv_obj_t * target = lv_event_get_target(e);
    if(!target) return;

    /* 如果点中了用户名/密码输入框、键盘或 ime，本回调不处理 */
    if(target == ds_username_ta || target == ds_password_ta || target == ds_kb || target == ds_ime) {
        return;
    }

    if(ds_kb) lv_obj_add_flag(ds_kb, LV_OBJ_FLAG_HIDDEN);
    if(ds_ime) {
        lv_obj_add_flag(ds_ime, LV_OBJ_FLAG_HIDDEN);
        /* 隐藏候选框面板 */
        lv_obj_t * cand_panel = lv_ime_pinyin_get_cand_panel(ds_ime);
        if(cand_panel) lv_obj_add_flag(cand_panel, LV_OBJ_FLAG_HIDDEN);
    }
    if(ds_cont) lv_obj_center(ds_cont);
}

/* 主界面点击屏幕其他区域时隐藏键盘 */
static void ds_main_hide_kb_on_click(lv_event_t * e)
{
    lv_obj_t * target = lv_event_get_target(e);
    if(!target) return;

    /* 如果点中了输入框、键盘或 ime，本回调不处理 */
    if(target == ds_main_input_ta || target == ds_main_kb || target == ds_main_ime) {
        return;
    }

    if(ds_main_kb) lv_obj_add_flag(ds_main_kb, LV_OBJ_FLAG_HIDDEN);
    if(ds_main_ime) {
        lv_obj_add_flag(ds_main_ime, LV_OBJ_FLAG_HIDDEN);
        /* 隐藏候选框面板 */
        lv_obj_t * cand_panel = lv_ime_pinyin_get_cand_panel(ds_main_ime);
        if(cand_panel) lv_obj_add_flag(cand_panel, LV_OBJ_FLAG_HIDDEN);
    }
    /* 恢复输入框容器位置 */
    if(ds_main_input_ta) {
        lv_obj_t * input_cont = lv_obj_get_parent(ds_main_input_ta);
        if(input_cont) {
            lv_obj_align(input_cont, LV_ALIGN_BOTTOM_MID, 100, -16);
        }
    }
}

void deepseek_login_screen_create(void)
{
    /* 加载保存的登录信息 */
    ds_load_login_info();

    /* 检查自动登录（严格判断3个条件） */
    if(ds_autologin_checked == 1 && strlen(ds_username_buf) > 0 && strlen(ds_password_buf) > 0) {
        ds_set_result_text("正在自动登录...");
        // 延迟800ms避免界面未渲染完成
        lv_timer_t * t = lv_timer_create(ds_login_timer_cb, 800, NULL);
        if(t) lv_timer_set_repeat_count(t, 1);
    }

    /* 如果已创建则直接显示 */
    if(ds_login_screen) {
        /* 更新输入框内容 */
        if(ds_username_ta) {
            lv_textarea_set_text(ds_username_ta, ds_username_buf);
        }
        if(ds_password_ta) {
            if(ds_remember_checked) {
                lv_textarea_set_text(ds_password_ta, ds_password_buf);
            } else {
                lv_textarea_set_text(ds_password_ta, "");
            }
        }
        if(ds_remember_check) {
            lv_obj_set_state(ds_remember_check, LV_STATE_CHECKED, ds_remember_checked);
        }
        if(ds_autologin_check) {
            lv_obj_set_state(ds_autologin_check, LV_STATE_CHECKED, ds_autologin_checked);
        }
        lv_scr_load_anim(ds_login_screen, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 500, 0, false);
        return;
    }
    ds_login_screen = lv_obj_create(NULL);

    /* 创建居中容器（考虑屏幕 1024x600） */
    lv_obj_t * cont = lv_obj_create(ds_login_screen);
    ds_cont         = cont;
    lv_obj_set_size(cont, 1024, 600);
    lv_obj_center(cont);
    lv_obj_set_style_pad_all(cont, 10, 0);
    lv_obj_set_style_border_width(cont, 2, 0);
    lv_obj_set_scroll_dir(cont, LV_DIR_NONE);

    /* 创建背景图片 */
    lv_obj_t * bg_img = lv_img_create(cont);
    lv_img_set_src(bg_img, "/deepseek1.png");
    lv_obj_set_size(bg_img, 480, 100);
    lv_obj_align(bg_img, LV_ALIGN_TOP_MID, 0, 50);
    lv_obj_move_background(bg_img);

    /* 用户名输入（居中） */
    ds_username_ta = lv_textarea_create(cont);
    lv_obj_set_width(ds_username_ta, 360);
    lv_textarea_set_one_line(ds_username_ta, true);
    lv_textarea_set_placeholder_text(ds_username_ta, "账号");
    lv_obj_set_style_text_font(ds_username_ta, &song, 0);
    lv_obj_align(ds_username_ta, LV_ALIGN_CENTER, 0, -80);
    lv_obj_add_event_cb(ds_username_ta, ds_ta_event_cb, LV_EVENT_FOCUSED, NULL);
    /* 填入保存的用户名 */
    lv_textarea_set_text(ds_username_ta, ds_username_buf);

    /* 密码输入（居中） */
    ds_password_ta = lv_textarea_create(cont);
    lv_obj_set_width(ds_password_ta, 360);
    lv_textarea_set_password_mode(ds_password_ta, true);
    lv_textarea_set_one_line(ds_password_ta, true);
    lv_textarea_set_placeholder_text(ds_password_ta, "密码");
    lv_obj_set_style_text_font(ds_password_ta, &song, 0);
    lv_obj_align(ds_password_ta, LV_ALIGN_CENTER, 0, -10);
    lv_obj_add_event_cb(ds_password_ta, ds_ta_event_cb, LV_EVENT_FOCUSED, NULL);
    /* 填入保存的密码（如果记住密码） */
    if(ds_remember_checked) {
        lv_textarea_set_text(ds_password_ta, ds_password_buf);
    }

    /* 记住密码 与 自动登录 复选（居中下方） */
    ds_remember_check = lv_checkbox_create(cont);
    lv_checkbox_set_text(ds_remember_check, "记住密码");
    lv_obj_set_style_text_font(ds_remember_check, &song, 0);
    lv_obj_align(ds_remember_check, LV_ALIGN_CENTER, -100, 50);
    /* 设置记住密码复选框状态 */
    lv_obj_set_state(ds_remember_check, LV_STATE_CHECKED, ds_remember_checked);

    ds_autologin_check = lv_checkbox_create(cont);
    lv_checkbox_set_text(ds_autologin_check, "自动登录");
    lv_obj_set_style_text_font(ds_autologin_check, &song, 0);
    lv_obj_align(ds_autologin_check, LV_ALIGN_CENTER, 100, 50);
    lv_obj_set_state(ds_autologin_check, LV_STATE_CHECKED, ds_autologin_checked);
    lv_obj_add_event_cb(ds_autologin_check, ds_autologin_check_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* 登录 与 注册 按钮（容器底部居中） */
    lv_obj_t * login_btn = lv_btn_create(cont);
    lv_obj_set_size(login_btn, 140, 48);
    lv_obj_add_event_cb(login_btn, ds_login_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_align(login_btn, LV_ALIGN_BOTTOM_MID, -105, -145);
    lv_obj_t * login_label = lv_label_create(login_btn);
    lv_label_set_text(login_label, "登录");
    lv_obj_set_style_text_font(login_label, &song, 0);
    lv_obj_center(login_label);

    lv_obj_t * reg_btn = lv_btn_create(cont);
    lv_obj_set_size(reg_btn, 140, 48);
    lv_obj_add_event_cb(reg_btn, ds_register_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_align(reg_btn, LV_ALIGN_BOTTOM_MID, 105, -145);
    lv_obj_t * reg_label = lv_label_create(reg_btn);
    lv_label_set_text(reg_label, "注册");
    lv_obj_set_style_text_font(reg_label, &song, 0);
    lv_obj_center(reg_label);

    /* 结果提示标签（居中容器底部上方） */
    ds_result_label = lv_label_create(cont);
    lv_label_set_text(ds_result_label, "");
    lv_obj_set_style_text_font(ds_result_label, &song, 0);
    lv_obj_align(ds_result_label, LV_ALIGN_BOTTOM_MID, 0, -70);

    /* 创建键盘和拼音输入法（初始隐藏） */
    ds_kb = lv_keyboard_create(ds_login_screen);
    lv_obj_add_event_cb(ds_kb, ds_kb_event_cb, LV_EVENT_ALL, NULL);
    lv_obj_add_flag(ds_kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_width(ds_kb, LV_PCT(100));

    /* 创建 ime 为屏幕级对象（参考示例）并设置字体 */
    ds_ime = lv_ime_pinyin_create(ds_login_screen);
    lv_obj_set_style_text_font(ds_ime, &song, 0);
    lv_ime_pinyin_set_keyboard(ds_ime, ds_kb);
    /* 初始隐藏候选框 */
    lv_obj_add_flag(ds_ime, LV_OBJ_FLAG_HIDDEN);

    /* 配置候选面板样式与位置，避免乱码并调整大小 */
    lv_obj_t * cand_panel = lv_ime_pinyin_get_cand_panel(ds_ime);
    if(cand_panel) {
        lv_obj_set_size(cand_panel, LV_PCT(100), LV_PCT(10));
        lv_obj_align_to(cand_panel, ds_kb, LV_ALIGN_OUT_TOP_MID, 0, 0);
        lv_obj_set_style_text_font(cand_panel, &song, 0);
    }

    /* 点击屏幕其他位置隐藏键盘（包括容器外点击） */
    lv_obj_add_event_cb(ds_login_screen, ds_hide_kb_on_click, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(cont, ds_hide_kb_on_click, LV_EVENT_CLICKED, NULL);

    /* 显示该屏幕 */
    lv_scr_load_anim(ds_login_screen, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 500, 0, false);
}

/* 右上返回按钮回调：返回登录界面 */
static void ds_back_btn_cb(lv_event_t * e)
{
    (void)e;
    /* 清除自动登录标志，防止返回后再次自动登录 */
    ds_autologin_checked = 0;
    ds_save_login_info();
    deepseek_login_screen_create();
    if(ds_login_screen) {
        lv_scr_load_anim(ds_login_screen, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 500, 0, false);
    }
}

/* 语音按钮按下回调：开始录音 */
static void ds_voice_btn_pressed_cb(lv_event_t * e)
{
    lv_obj_t * btn = lv_event_get_target(e);

    /* 放大按钮效果 */
    lv_obj_set_style_transform_scale(btn, 1200, LV_STATE_PRESSED);
    lv_obj_set_style_transform_zoom(btn, 256, LV_STATE_PRESSED);

    if(ds_voice_recording) return;

    ds_voice_recording = 1;
    ds_set_result_text("正在录音...");

    /* 执行录音命令，移除-d 5参数，实现按住录制 */
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "arecord -f S16_LE -c 1 -r 16000 -t wav /rknndemo/model/1.wav &");
    system(cmd);
}

/* 语音按钮松开回调：停止录音并开始识别 */
static void ds_voice_btn_released_cb(lv_event_t * e)
{
    lv_obj_t * btn = lv_event_get_target(e);

    /* 还原按钮效果 */
    lv_obj_set_style_transform_scale(btn, 1000, LV_STATE_PRESSED);
    lv_obj_set_style_transform_zoom(btn, 0, LV_STATE_PRESSED);

    if(ds_voice_recording) {
        ds_voice_recording = 0;
        ds_set_result_text("录音完成，正在识别...");

        /* 停止录音进程 */
        system("pkill -f 'arecord.*1.wav'");

        /* 调用识别程序 */
        ds_perform_recognition();
    }
}

/* 执行语音识别 */
static void ds_perform_recognition(void)
{
    /* 构建识别命令，只输出识别结果 */
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
             "./rknn_zipformer_demo ./model/encoder-epoch-99-avg-1.rknn ./model/decoder-epoch-99-avg-1.rknn "
             "./model/joiner-epoch-99-avg-1.rknn ./model/1.wav 2>/dev/null | grep -E '(Zipformer output|Result|Text)' "
             "> /tmp/recognition_result.txt &");

    /* 执行识别命令 */
    system(cmd);

    /* 创建定时器检查识别结果 */
    if(ds_voice_timer) {
        lv_timer_del(ds_voice_timer);
    }
    ds_voice_timer = lv_timer_create(ds_voice_recognize_timer_cb, 500, NULL);
}

/* 识别结果检查定时器回调 */
static void ds_voice_recognize_timer_cb(lv_timer_t * timer)
{
    (void)timer;

    /* 检查识别进程是否完成 */
    FILE * fp = fopen("/tmp/recognition_result.txt", "r");
    if(!fp) {
        return; /* 文件尚未创建，继续等待 */
    }

    /* 读取所有内容 */
    char buffer[2048] = {0};
    size_t bytes_read = fread(buffer, 1, sizeof(buffer) - 1, fp);
    fclose(fp);

    if(bytes_read > 0) {
        /* 查找实际识别结果 */
        char * result = NULL;

        /* 尝试多种可能的格式 */
        char * key1 = strstr(buffer, "Zipformer output: ");
        char * key2 = strstr(buffer, "Result: ");
        char * key3 = strstr(buffer, "Text: ");

        if(key1) {
            result = key1 + strlen("Zipformer output: ");
        } else if(key2) {
            result = key2 + strlen("Result: ");
        } else if(key3) {
            result = key3 + strlen("Text: ");
        } else {
            /* 如果没有找到特定前缀，尝试提取最后一行非空内容 */
            char * last_line = strrchr(buffer, '\n');
            if(last_line) {
                *last_line = '\0'; /* 截断最后一行 */
                last_line  = strrchr(buffer, '\n');
                if(last_line) {
                    result = last_line + 1;
                } else {
                    result = buffer;
                }
            } else {
                result = buffer;
            }
        }

        /* 去除换行符和多余空格 */
        if(result) {
            char * end = result + strlen(result) - 1;
            while(end >= result && (*end == '\n' || *end == '\r' || *end == ' ')) {
                *end = '\0';
                end--;
            }

            /* 过滤掉日志信息 */
            if(strstr(result, "use:") || strstr(result, "ms")) {
                ds_set_result_text("未获取到有效识别结果");
            } else {
                /* 将识别结果放入输入框 */
                if(ds_main_input_ta) {
                    lv_textarea_set_text(ds_main_input_ta, result);
                }
                ds_set_result_text("识别成功");
            }
        } else {
            ds_set_result_text("未获取到识别结果");
        }

        /* 删除定时器 */
        if(ds_voice_timer) {
            lv_timer_del(ds_voice_timer);
            ds_voice_timer = NULL;
        }
    }
}

/* 保存对话历史到文件 */
static void ds_save_chat_histories(void)
{
    FILE * fp = fopen("deepseek_chat_histories.txt", "w");
    if(!fp) return;

    fprintf(fp, "%d\n", ds_history_count);
    for(int i = 0; i < ds_history_count; i++) {
        // 确保标题不超过20个字符
        char title[21] = {0};
        strncpy(title, ds_chat_histories[i].title, 20);
        fprintf(fp, "%s\n", title);

        fprintf(fp, "%d\n", ds_chat_histories[i].message_count);
        for(int j = 0; j < ds_chat_histories[i].message_count; j++) {
            fprintf(fp, "%d\n", ds_chat_histories[i].messages[j].is_user);
            fprintf(fp, "%s\n", ds_chat_histories[i].messages[j].content);
        }
    }
    fclose(fp);
}

/* 从文件加载对话历史 */
static void ds_load_chat_histories(void)
{
    FILE * fp = fopen("deepseek_chat_histories.txt", "r");
    if(!fp) return;

    fscanf(fp, "%d\n", &ds_history_count);
    if(ds_history_count > MAX_HISTORY_COUNT) {
        ds_history_count = MAX_HISTORY_COUNT;
    }

    for(int i = 0; i < ds_history_count; i++) {
        fgets(ds_chat_histories[i].title, sizeof(ds_chat_histories[i].title), fp);
        // 移除换行符
        char * nl = strchr(ds_chat_histories[i].title, '\n');
        if(nl) *nl = '\0';

        // 确保标题不超过20个字符
        if(strlen(ds_chat_histories[i].title) > 20) {
            ds_chat_histories[i].title[20] = '\0';
        }

        fscanf(fp, "%d\n", &ds_chat_histories[i].message_count);
        if(ds_chat_histories[i].message_count > MAX_MESSAGE_COUNT) {
            ds_chat_histories[i].message_count = MAX_MESSAGE_COUNT;
        }

        for(int j = 0; j < ds_chat_histories[i].message_count; j++) {
            fscanf(fp, "%d\n", &ds_chat_histories[i].messages[j].is_user);
            fgets(ds_chat_histories[i].messages[j].content, sizeof(ds_chat_histories[i].messages[j].content), fp);
            // 移除换行符
            nl = strchr(ds_chat_histories[i].messages[j].content, '\n');
            if(nl) *nl = '\0';
        }
    }
    fclose(fp);
}

/* 创建新对话 */
static void ds_create_new_chat(void)
{
    // 新增：先检查 ds_chat_cont 是否为 NULL，避免空指针调用
    if(ds_chat_cont) {
        lv_obj_clean(ds_chat_cont);
        ds_chat_next_y = 8;
    } else {
        ds_chat_next_y = 8; // 初始化默认值
        printf("Warning: ds_chat_cont is NULL, skip clean\n");
        return; // 或根据逻辑决定是否直接返回
    }

    // 创建新对话记录
    if(ds_history_count >= MAX_HISTORY_COUNT) {
        // 移除最旧的对话
        for(int i = MAX_HISTORY_COUNT - 1; i > 0; i--) {
            ds_chat_histories[i] = ds_chat_histories[i - 1];
        }
        ds_history_count = MAX_HISTORY_COUNT - 1;
    }

    // 移动现有对话
    for(int i = ds_history_count; i > 0; i--) {
        ds_chat_histories[i] = ds_chat_histories[i - 1];
    }

    // 创建新对话，初始标题为"新对话"
    strncpy(ds_chat_histories[0].title, "新对话", sizeof(ds_chat_histories[0].title) - 1);
    ds_chat_histories[0].title[sizeof(ds_chat_histories[0].title) - 1] = '\0';
    ds_chat_histories[0].message_count                                 = 0;
    ds_history_count++;
    ds_current_chat_index = 0;

    // 更新历史记录列表
    ds_update_history_list();

    // 保存到文件
    ds_save_chat_histories();

    // 清空输入框
    if(ds_main_input_ta) {
        lv_textarea_set_text(ds_main_input_ta, "");
    }

    // 显示提示
    ds_set_result_text("已开启新对话");
}

/* 左上开启新对话按钮回调：清空输入并提示 */
static void ds_newconv_btn_cb(lv_event_t * e)
{
    (void)e;
    ds_create_new_chat();
}

/* 删除对话按钮回调 */
static void ds_delete_conv_btn_cb(lv_event_t * e)
{
    (void)e;

    /* 如果没有对话历史，提示用户 */
    if(ds_history_count == 0) {
        ds_set_result_text("没有可删除的对话");
        return;
    }

    /* 如果没有选中任何对话，删除当前对话 */
    if(ds_current_chat_index < 0) {
        ds_set_result_text("请先选择要删除的对话");
        return;
    }

    /* 移除选中的对话 */
    for(int i = ds_current_chat_index; i < ds_history_count - 1; i++) {
        ds_chat_histories[i] = ds_chat_histories[i + 1];
    }
    ds_history_count--;

    /* 如果删除的是最后一个对话，调整索引 */
    if(ds_current_chat_index >= ds_history_count) {
        ds_current_chat_index = ds_history_count - 1;
    }

    /* 清空聊天容器 */
    if(ds_chat_cont) {
        lv_obj_clean(ds_chat_cont);
        ds_chat_next_y = 8;
    }

    /* 更新历史记录列表 */
    ds_update_history_list();

    /* 保存更新后的对话历史 */
    ds_save_chat_histories();

    /* 如果还有对话，加载第一个对话 */
    if(ds_history_count > 0) {
        ds_current_chat_index = 0;
        ds_history_item_cb(NULL); /* 加载第一个对话 */
        char msg[128];
        snprintf(msg, sizeof(msg), "已删除对话，当前：%s", ds_chat_histories[0].title);
        ds_set_result_text(msg);
    } else {
        /* 如果没有对话了，创建一个新对话 */
        ds_create_new_chat();
        ds_set_result_text("已删除所有对话，已创建新对话");
    }
}

/* 更新历史记录列表 */
static void ds_update_history_list(void)
{
    if(!ds_history_list) {
        printf("Warning: ds_history_list is NULL, skip update\n");
        return;
    }

    // 清空现有列表
    lv_obj_clean(ds_history_list);

    // 添加历史记录项
    for(int i = 0; i < ds_history_count; i++) {
        lv_obj_t * item = lv_list_add_btn(ds_history_list, NULL, ds_chat_histories[i].title);
        lv_obj_set_style_text_font(item, &song, 0);
        lv_obj_add_event_cb(item, ds_history_item_cb, LV_EVENT_CLICKED, (void *)i);
    }
}

/* 历史记录项点击回调 */
static void ds_history_item_cb(lv_event_t * e)
{
    int index;

    /* 如果事件参数为NULL，使用当前对话索引 */
    if(e == NULL) {
        index = ds_current_chat_index >= 0 ? ds_current_chat_index : 0;
    } else {
        index = (int)lv_event_get_user_data(e);
    }

    if(index >= 0 && index < ds_history_count) {
        // 切换到选中的对话
        ds_current_chat_index = index;

        // 清空当前聊天区
        lv_obj_clean(ds_chat_cont);
        ds_chat_next_y = 8;

        // 显示切换到的对话标题
        char msg[128];
        snprintf(msg, sizeof(msg), "已切换到：%s", ds_chat_histories[index].title);
        ds_set_result_text(msg);

        // 加载对话历史消息
        lv_coord_t chat_w = lv_obj_get_width(ds_chat_cont);
        if(chat_w <= 0) chat_w = 720;
        lv_coord_t max_bubble_w = (lv_coord_t)(chat_w * 0.65f);
        const lv_coord_t pad_h  = 12;
        const lv_coord_t pad_v  = 8;

        for(int i = 0; i < ds_chat_histories[index].message_count; i++) {
            ds_message_t * msg = &ds_chat_histories[index].messages[i];

            // 文字像素测量
            lv_point_t txt_size;
            lv_txt_get_size(&txt_size, msg->content, &song, 0, 0, max_bubble_w, LV_TEXT_FLAG_NONE);
            lv_coord_t lbl_w = txt_size.x;
            lv_coord_t lbl_h = txt_size.y;

            if(msg->is_user) {
                // 用户消息（右侧）
                lv_obj_t * ucont = lv_obj_create(ds_chat_cont);
                lv_obj_set_style_bg_color(ucont, lv_color_hex(0xD9F1FF), 0);
                lv_obj_set_style_radius(ucont, 8, 0);
                lv_obj_set_style_pad_all(ucont, pad_v, 0);
                lv_obj_set_size(ucont, lbl_w + pad_h * 2, lbl_h + pad_v * 2);
                lv_obj_align(ucont, LV_ALIGN_TOP_RIGHT, -8, ds_chat_next_y);
                lv_obj_clear_flag(ucont, LV_OBJ_FLAG_SCROLLABLE);
                lv_obj_set_scroll_dir(ucont, LV_DIR_NONE);
                lv_obj_t * ulbl = lv_label_create(ucont);
                lv_label_set_long_mode(ulbl, LV_LABEL_LONG_WRAP);
                lv_label_set_text(ulbl, msg->content);
                lv_obj_set_style_text_font(ulbl, &song, 0);
                lv_obj_set_width(ulbl, lbl_w);
                lv_obj_align(ulbl, LV_ALIGN_LEFT_MID, pad_h - 8, 0);
            } else {
                // AI消息（左侧）
                lv_obj_t * acont = lv_obj_create(ds_chat_cont);
                lv_obj_set_style_bg_color(acont, lv_color_hex(0xFFFFFF), 0);
                lv_obj_set_style_radius(acont, 8, 0);
                lv_obj_set_style_pad_all(acont, pad_v, 0);
                lv_obj_set_size(acont, lbl_w + pad_h * 2, lbl_h + pad_v * 2);
                lv_obj_align(acont, LV_ALIGN_TOP_LEFT, 8, ds_chat_next_y);
                lv_obj_clear_flag(acont, LV_OBJ_FLAG_SCROLLABLE);
                lv_obj_set_scroll_dir(acont, LV_DIR_NONE);
                lv_obj_t * albl = lv_label_create(acont);
                lv_label_set_long_mode(albl, LV_LABEL_LONG_WRAP);
                lv_label_set_text(albl, msg->content);
                lv_obj_set_style_text_font(albl, &song, 0);
                lv_obj_set_width(albl, lbl_w);
                lv_obj_align(albl, LV_ALIGN_LEFT_MID, pad_h - 8, 0);
            }

            ds_chat_next_y += (lbl_h + pad_v * 2) + 8;
        }

        // 滚动到最新位置
        lv_obj_scroll_to_y(ds_chat_cont, ds_chat_next_y, LV_ANIM_ON);
    }
}

/* 保存消息到当前对话历史 */
static void ds_save_message_to_history(const char * content, int is_user)
{
    if(ds_current_chat_index < 0 || ds_current_chat_index >= ds_history_count) {
        return;
    }

    ds_chat_history_t * chat = &ds_chat_histories[ds_current_chat_index];
    if(chat->message_count >= MAX_MESSAGE_COUNT) {
        // 移除最旧的消息
        for(int i = 0; i < MAX_MESSAGE_COUNT - 1; i++) {
            chat->messages[i] = chat->messages[i + 1];
        }
        chat->message_count = MAX_MESSAGE_COUNT - 1;
    }

    // 添加新消息
    strncpy(chat->messages[chat->message_count].content, content, MAX_MESSAGE_LENGTH - 1);
    chat->messages[chat->message_count].content[MAX_MESSAGE_LENGTH - 1] = '\0';
    chat->messages[chat->message_count].is_user                         = is_user;
    chat->message_count++;

    // 保存到文件
    ds_save_chat_histories();
}

/* 发送按钮回调：读取输入并清空（当前示例行为） */
static void ds_send_btn_cb(lv_event_t * e)
{
    (void)e;
    if(!ds_main_input_ta || !ds_chat_cont) return;
    const char * txt = lv_textarea_get_text(ds_main_input_ta);
    if(!txt || strlen(txt) == 0) return;

    /* 计算最大气泡宽度（占聊天区宽度的 65%） */
    lv_coord_t chat_w = lv_obj_get_width(ds_chat_cont);
    if(chat_w <= 0) chat_w = 720;
    lv_coord_t max_bubble_w = (lv_coord_t)(chat_w * 0.3f);

    /* 文字像素测量 */
    lv_point_t txt_size;
    lv_txt_get_size(&txt_size, txt, &song, 0, 0, max_bubble_w, LV_TEXT_FLAG_NONE);
    lv_coord_t lbl_w = txt_size.x;
    lv_coord_t lbl_h = txt_size.y;

    const lv_coord_t pad_h = 12;
    const lv_coord_t pad_v = 8;

    /* 用户气泡（右侧） */
    lv_obj_t * ucont = lv_obj_create(ds_chat_cont);
    lv_obj_set_style_bg_color(ucont, lv_color_hex(0xD9F1FF), 0);
    lv_obj_set_style_radius(ucont, 8, 0);
    lv_obj_set_style_pad_all(ucont, pad_v, 0);
    lv_obj_set_size(ucont, lbl_w + pad_h * 2, lbl_h + pad_v * 2);
    lv_obj_align(ucont, LV_ALIGN_TOP_RIGHT, -8, ds_chat_next_y);
    lv_obj_clear_flag(ucont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(ucont, LV_DIR_NONE);
    lv_obj_t * ulbl = lv_label_create(ucont);
    lv_label_set_long_mode(ulbl, LV_LABEL_LONG_WRAP);
    lv_label_set_text(ulbl, txt);
    lv_obj_set_style_text_font(ulbl, &song, 0);
    lv_obj_set_width(ulbl, lbl_w);
    lv_obj_align(ulbl, LV_ALIGN_LEFT_MID, pad_h - 8, 0);

    ds_chat_next_y += (lbl_h + pad_v * 2) + 8;

    /* AI 占位回复（左侧） */
    lv_obj_t * acont = lv_obj_create(ds_chat_cont);
    lv_obj_set_style_bg_color(acont, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_radius(acont, 8, 0);
    lv_obj_set_style_pad_all(acont, pad_v, 0);

    // 计算占位文本的实际宽度
    lv_point_t placeholder_size;
    lv_txt_get_size(&placeholder_size, "AI正在思考...", &song, 0, 0, max_bubble_w - pad_h * 2, LV_TEXT_FLAG_NONE);

    // 使用占位文本宽度设置初始气泡大小
    lv_obj_set_size(acont, placeholder_size.x + pad_h * 2, placeholder_size.y + pad_v * 2);
    lv_obj_align(acont, LV_ALIGN_TOP_LEFT, 8, ds_chat_next_y);
    lv_obj_clear_flag(acont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(acont, LV_DIR_NONE);

    lv_obj_t * albl = lv_label_create(acont);
    lv_label_set_long_mode(albl, LV_LABEL_LONG_WRAP);
    lv_label_set_text(albl, "AI正在思考...");
    lv_obj_set_style_text_font(albl, &song, 0);
    lv_obj_set_width(albl, placeholder_size.x);
    lv_obj_align(albl, LV_ALIGN_LEFT_MID, pad_h - 8, 0);

    ds_chat_next_y += (placeholder_size.y + pad_v * 2) + 8;

    /* 滚动到最新位置 */
    lv_obj_scroll_to_y(ds_chat_cont, ds_chat_next_y, LV_ANIM_ON);

    // ====================== 以下是我帮你修复的网络部分 ======================
    /* 发送AI请求 */
    struct sockaddr_in server_addr;

    // 先关闭旧连接（防止多次点击崩溃）
    if(ai_sock > 0) {
        close(ai_sock);
        ai_sock = 0;
    }

    // 创建 socket
    ai_sock = socket(AF_INET, SOCK_STREAM, 0);
    if(ai_sock < 0) {
        lv_label_set_text(albl, "创建socket失败");
        printf("socket创建失败：%s\n", strerror(errno));
        return;
    }

    // 设置超时（防止界面卡死）
    struct timeval timeout = {3, 0}; // 3秒超时
    setsockopt(ai_sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    setsockopt(ai_sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port   = htons(PORT);

    // IP 解析 + 错误判断
    if(inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr) <= 0) {
        lv_label_set_text(albl, "IP地址错误");
        printf("IP解析失败：%s\n", SERVER_IP);
        close(ai_sock);
        ai_sock = 0;
        return;
    }

    // 连接 WSL
    if(connect(ai_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        lv_label_set_text(albl, "连接WSL失败");
        printf("connect失败：%s\n", strerror(errno));
        close(ai_sock);
        ai_sock = 0;
        return;
    }

    printf("成功连接 WSL：%s:%d\n", SERVER_IP, PORT);

    // 发送数据
    if(send_ai_request(txt) < 0) {
        lv_label_set_text(albl, "发送失败");
        close(ai_sock);
        ai_sock = 0;
        return;
    }

    printf("文本已发送到WSL：%s\n", txt);

    // ======================================================================

    ds_set_result_text("消息已发送");

    // 保存用户消息到历史记录
    ds_save_message_to_history(txt, 1);

    // 如果是当前对话的第一条消息，更新对话标题
    if(ds_current_chat_index >= 0 && ds_current_chat_index < ds_history_count) {
        ds_chat_history_t * chat = &ds_chat_histories[ds_current_chat_index];
        if(chat->message_count == 1) {
            // 使用第一条消息作为标题，限制字符数为20
            int title_len = strlen(txt);
            int copy_len  = title_len < 20 ? title_len : 20;
            strncpy(chat->title, txt, copy_len);
            chat->title[copy_len] = '\0';

            // 更新历史记录列表
            ds_update_history_list();

            // 保存到文件
            ds_save_chat_histories();
        }
    }

    lv_textarea_set_text(ds_main_input_ta, "");

    /* 创建定时器检查响应 */
    lv_timer_t * response_timer = lv_timer_create(check_ai_response, POLL_INTERVAL_US / 1000, acont);
    lv_timer_set_repeat_count(response_timer, -1); // 无限循环，直到手动删除
}

static void check_ai_response(lv_timer_t * timer)
{
    lv_obj_t * ai_cont  = (lv_obj_t *)lv_timer_get_user_data(timer);
    lv_obj_t * ai_label = lv_obj_get_child(ai_cont, 0);
    char buffer[2048]   = {0};
    int ret;

    // 超时计数器（static 只会初始化一次）
    static int timeout_cnt = 0;

    // 非阻塞读取数据
    ret = recv(ai_sock, buffer, sizeof(buffer) - 1, MSG_DONTWAIT);
    if(ret > 0) {
        buffer[ret] = '\0';
        printf("收到AI回复：%s\n", buffer);

        // 找到JSON开始位置（跳过HTTP头）
        char * json_start = strstr(buffer, "\r\n\r\n");
        if(json_start)
            json_start += 4;
        else
            json_start = buffer;

        // 解析response字段
        char * response = strstr(json_start, "\"response\":\"");
        if(response) {
            response += strlen("\"response\":\"");
            char * end = response;
            while(*end && !(*end == '"' && *(end - 1) != '\\')) end++;
            *end = '\0';

            // 1. 清理特殊字符序列 \u003cthink\u003e
            char * think_start = strstr(response, "\\u003cthink\\u003e");
            if(think_start) {
                char * think_end = strstr(think_start, "\\u003c/think\\u003e");
                if(think_end) {
                    think_end += strlen("\\u003c/think\\u003e");
                    memmove(think_start, think_end, strlen(think_end) + 1);
                }
            }

            // 2. 将 JSON 转义字符还原为真实字符（解决字面量 \n\n 问题）
            char * read_ptr  = response;
            char * write_ptr = response;
            while(*read_ptr) {
                if(*read_ptr == '\\' && *(read_ptr + 1) == 'n') {
                    *write_ptr++ = '\n';
                    read_ptr += 2;
                } else if(*read_ptr == '\\' && *(read_ptr + 1) == 't') {
                    *write_ptr++ = '\t';
                    read_ptr += 2;
                } else if(*read_ptr == '\\' && *(read_ptr + 1) == 'r') {
                    *write_ptr++ = '\r';
                    read_ptr += 2;
                } else if(*read_ptr == '\\' && *(read_ptr + 1) == '\\') {
                    *write_ptr++ = '\\';
                    read_ptr += 2;
                } else if(*read_ptr == '\\' && *(read_ptr + 1) == '"') {
                    *write_ptr++ = '"';
                    read_ptr += 2;
                } else {
                    *write_ptr++ = *read_ptr++;
                }
            }
            *write_ptr = '\0';

            // 3. 清理开头所有的真实空白字符（包括刚才还原出来的 \n）
            char * cleaned = response;
            while(*cleaned && (*cleaned == ' ' || *cleaned == '\t' || *cleaned == '\n' || *cleaned == '\r')) {
                cleaned++;
            }

            // 空内容保护
            if(strlen(cleaned) == 0) {
                cleaned = "AI返回空内容";
            }

            // 设置文本
            lv_label_set_text(ai_label, cleaned);

            // 保存AI回复到历史记录
            ds_save_message_to_history(cleaned, 0);

            // 重新计算气泡大小
            lv_coord_t chat_w       = lv_obj_get_width(ds_chat_cont);
            lv_coord_t max_bubble_w = (lv_coord_t)(chat_w * 0.65f);
            lv_point_t txt_size;
            lv_txt_get_size(&txt_size, cleaned, &song, 0, 0, max_bubble_w - 24, LV_TEXT_FLAG_NONE);

            // 更新气泡和标签大小
            lv_obj_set_size(ai_cont, txt_size.x + 24, txt_size.y + 16);
            lv_obj_set_width(ai_label, txt_size.x);

            // 更新聊天位置
            ds_chat_next_y += (txt_size.y + 16) + 8;

            // 滚动到最新位置
            lv_obj_scroll_to_y(ds_chat_cont, ds_chat_next_y, LV_ANIM_ON);
        } else {
            lv_label_set_text(ai_label, "解析失败");
        }

        // 关闭连接
        close(ai_sock);
        ai_sock = 0;
        lv_timer_del(timer);
        timeout_cnt = 0; // 收到有效回复，重置超时
    } else if(ret == 0 || (ret < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
        lv_label_set_text(ai_label, "连接断开");
        close(ai_sock);
        ai_sock = 0;
        lv_timer_del(timer);
        timeout_cnt = 0;
    }

    // 超时计数 +1
    timeout_cnt++;
    if(timeout_cnt > 600) {
        lv_label_set_text(ai_label, "响应超时");
        close(ai_sock);
        ai_sock = 0;
        lv_timer_del(timer);
        timeout_cnt = 0;
    }
}

void deepseek_main_screen_create(void)
{
    if(ds_main_scr) {
        lv_scr_load_anim(ds_main_scr, LV_SCR_LOAD_ANIM_MOVE_LEFT, 500, 0, false);
        return;
    }

    system("restart_rknn.sh &");

    ds_main_scr    = lv_obj_create(NULL);
    lv_obj_t * scr = ds_main_scr;

    /* 创建logo图片 */
    lv_obj_t * logo_img = lv_img_create(scr);
    lv_img_set_src(logo_img, "/deepseek2.png");
    lv_obj_set_size(logo_img, 100, 100);
    lv_obj_align(logo_img, LV_ALIGN_TOP_MID, -50, 0);

    /* 顶部中央标题 */
    lv_obj_t * title = lv_label_create(scr);
    lv_label_set_text(title, "与DeepSeek开始对话");
    lv_obj_set_style_text_font(title, &song, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 100, 33);

    /* 主界面结果提示标签 */
    ds_main_result_label = lv_label_create(scr);
    lv_label_set_text(ds_main_result_label, "");
    lv_obj_set_style_text_font(ds_main_result_label, &song, 0);
    lv_obj_align(ds_main_result_label, LV_ALIGN_TOP_MID, 110, 55);

    /* 左上：开启新对话按钮 */
    lv_obj_t * new_conv_btn = lv_btn_create(scr);
    lv_obj_set_size(new_conv_btn, 140, 40);
    lv_obj_align(new_conv_btn, LV_ALIGN_TOP_LEFT, 12, 12);
    lv_obj_t * new_conv_lbl = lv_label_create(new_conv_btn);
    lv_label_set_text(new_conv_lbl, "开启新对话");
    lv_obj_set_style_text_font(new_conv_lbl, &song, 0);
    lv_obj_center(new_conv_lbl);
    lv_obj_add_event_cb(new_conv_btn, ds_newconv_btn_cb, LV_EVENT_CLICKED, NULL);

    /* 删除对话按钮 */
    lv_obj_t * delete_conv_btn = lv_btn_create(scr);
    lv_obj_set_size(delete_conv_btn, 140, 40);
    lv_obj_align_to(delete_conv_btn, new_conv_btn, LV_ALIGN_OUT_RIGHT_MID, 10, 0);
    lv_obj_t * delete_conv_lbl = lv_label_create(delete_conv_btn);
    lv_label_set_text(delete_conv_lbl, "删除对话");
    lv_obj_set_style_text_font(delete_conv_lbl, &song, 0);
    lv_obj_center(delete_conv_lbl);
    lv_obj_add_event_cb(delete_conv_btn, ds_delete_conv_btn_cb, LV_EVENT_CLICKED, NULL);

    /* 后续放历史对话记录 */
    lv_obj_t * hist_cont = lv_obj_create(scr);
    lv_obj_set_size(hist_cont, 230, 500);
    lv_obj_align_to(hist_cont, new_conv_btn, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 30);
    lv_obj_set_style_bg_color(hist_cont, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(hist_cont, 1, 0);
    lv_obj_set_style_border_color(hist_cont, lv_color_hex(0xE0E0E0), 0);
    lv_obj_set_style_pad_all(hist_cont, 5, 0);

    /* 创建历史记录列表 */
    ds_history_list = lv_list_create(hist_cont);
    lv_obj_set_size(ds_history_list, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(ds_history_list, LV_OPA_TRANSP, 0);
    lv_obj_set_scrollbar_mode(ds_history_list, LV_SCROLLBAR_MODE_AUTO);

    /* 加载对话历史 */
    ds_load_chat_histories();

    /* 聊天对话区：显示用户/AI消息，位于 hist_cont 的右侧 */
    ds_chat_cont = lv_obj_create(scr);
    lv_obj_set_size(ds_chat_cont, 720, 420);
    lv_obj_align_to(ds_chat_cont, hist_cont, LV_ALIGN_OUT_RIGHT_TOP, 12, 0);
    lv_obj_set_style_bg_opa(ds_chat_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(ds_chat_cont, 8, 0);
    lv_obj_add_flag(ds_chat_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(ds_chat_cont, LV_SCROLLBAR_MODE_ON);
    lv_obj_set_scroll_dir(ds_chat_cont, LV_DIR_VER);
    ds_chat_next_y = 8;

    /* 必须先创建控件，再更新历史/新对话 */
    ds_update_history_list();

    /* 如果没有对话历史，创建一个新对话（现在控件已创建，不会空指针） */
    if(ds_history_count == 0) {
        ds_create_new_chat();
    }

    /* 添加点击事件，使输入框失去焦点 */
    lv_obj_add_event_cb(hist_cont, ds_main_hide_kb_on_click, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(ds_chat_cont, ds_main_hide_kb_on_click, LV_EVENT_CLICKED, NULL);

    /* 右上：返回登录按钮 */
    lv_obj_t * back_btn = lv_btn_create(scr);
    lv_obj_set_size(back_btn, 90, 40);
    lv_obj_align(back_btn, LV_ALIGN_TOP_RIGHT, -12, 12);
    lv_obj_t * back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, "返回");
    lv_obj_set_style_text_font(back_lbl, &song, 0);
    lv_obj_center(back_lbl);
    lv_obj_add_event_cb(back_btn, ds_back_btn_cb, LV_EVENT_CLICKED, NULL);

    /* 底部输入容器（单一输入框，贴底） */
    lv_obj_t * input_cont = lv_obj_create(scr);
    lv_obj_set_size(input_cont, 602, 58);
    lv_obj_align(input_cont, LV_ALIGN_BOTTOM_MID, 100, -16);
    lv_obj_set_style_radius(input_cont, 12, 0);
    lv_obj_set_style_bg_color(input_cont, lv_color_hex(0xF7F8FA), 0);
    lv_obj_clear_flag(input_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(input_cont, LV_SCROLLBAR_MODE_OFF);

    /* 语音识别按钮（左侧） */
    lv_obj_t * voice_btn = lv_btn_create(input_cont);
    lv_obj_set_size(voice_btn, 48, 48);
    lv_obj_align(voice_btn, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_radius(voice_btn, 24, 0);
    lv_obj_set_style_bg_color(voice_btn, lv_color_hex(0x6EA0FF), 0);
    lv_obj_t * voice_lbl = lv_label_create(voice_btn);
    lv_label_set_text(voice_lbl, LV_SYMBOL_AUDIO);
    lv_obj_set_style_text_font(voice_lbl, &lv_font_montserrat_16, 0);
    lv_obj_center(voice_lbl);
    lv_obj_add_event_cb(voice_btn, ds_voice_btn_pressed_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(voice_btn, ds_voice_btn_released_cb, LV_EVENT_RELEASED, NULL);

    /* 可编辑输入框 */
    lv_obj_t * input_ta = lv_textarea_create(input_cont);
    ds_main_input_ta    = input_ta;
    lv_obj_set_size(input_ta, 504, 56);
    lv_textarea_set_one_line(input_ta, true);
    lv_textarea_set_placeholder_text(input_ta, "给 DeepSeek 发送消息");
    lv_obj_set_style_text_font(input_ta, &song, 0);
    lv_obj_align(input_ta, LV_ALIGN_LEFT_MID, 54, 0);
    lv_obj_add_event_cb(input_ta, ds_main_ta_event_cb, LV_EVENT_FOCUSED, NULL);
    lv_obj_set_style_border_width(input_ta, 0, 0);
    lv_obj_clear_flag(input_ta, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(input_ta, LV_SCROLLBAR_MODE_OFF);

    /* 发送按钮（右侧） */
    lv_obj_t * send_btn = lv_btn_create(input_cont);
    lv_obj_set_size(send_btn, 48, 48);
    lv_obj_align(send_btn, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_radius(send_btn, 24, 0);
    lv_obj_set_style_bg_color(send_btn, lv_color_hex(0x6EA0FF), 0);
    lv_obj_t * send_lbl = lv_label_create(send_btn);
    lv_label_set_text(send_lbl, ">");
    lv_obj_set_style_text_font(send_lbl, &song, 0);
    lv_obj_center(send_lbl);
    lv_obj_add_event_cb(send_btn, ds_send_btn_cb, LV_EVENT_CLICKED, NULL);

    /* 为主界面创建独立的键盘与拼音 IME */
    if(ds_main_kb) {
        lv_obj_set_parent(ds_main_kb, ds_main_scr);
        lv_obj_add_event_cb(ds_main_kb, ds_main_kb_event_cb, LV_EVENT_ALL, NULL);
        lv_obj_add_flag(ds_main_kb, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_width(ds_main_kb, LV_PCT(100));
    } else {
        ds_main_kb = lv_keyboard_create(ds_main_scr);
        lv_obj_add_event_cb(ds_main_kb, ds_main_kb_event_cb, LV_EVENT_ALL, NULL);
        lv_obj_add_flag(ds_main_kb, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_width(ds_main_kb, LV_PCT(100));
    }

    if(ds_main_ime) {
        lv_obj_set_parent(ds_main_ime, ds_main_scr);
        lv_obj_set_style_text_font(ds_main_ime, &song, 0);
        lv_ime_pinyin_set_keyboard(ds_main_ime, ds_main_kb);
        lv_obj_add_flag(ds_main_ime, LV_OBJ_FLAG_HIDDEN);
    } else {
        ds_main_ime = lv_ime_pinyin_create(ds_main_scr);
        lv_obj_set_style_text_font(ds_main_ime, &song, 0);
        lv_ime_pinyin_set_keyboard(ds_main_ime, ds_main_kb);
        lv_obj_add_flag(ds_main_ime, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_t * cand_panel2 = lv_ime_pinyin_get_cand_panel(ds_main_ime);
    if(cand_panel2) {
        lv_obj_set_size(cand_panel2, LV_PCT(100), LV_PCT(10));
        lv_obj_align_to(cand_panel2, ds_main_kb, LV_ALIGN_OUT_TOP_MID, 0, 0);
        lv_obj_set_style_text_font(cand_panel2, &song, 0);
    }

    /* 点击屏幕其他位置隐藏键盘 */
    lv_obj_add_event_cb(ds_main_scr, ds_main_hide_kb_on_click, LV_EVENT_CLICKED, NULL);

    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_MOVE_LEFT, 500, 0, false);
}
void deepseek_exit_save(void)
{
    // 退出前强制保存当前登录配置
    ds_save_login_info();
}