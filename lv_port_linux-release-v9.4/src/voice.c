/*******************************************************************
 *
 * main.c - LVGL simulator for GNU/Linux
 *
 * Based on the original file from the repository
 *
 ******************************************************************/
#include <unistd.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>


#include "lvgl/lvgl.h"
#include "lvgl/demos/lv_demos.h"
#include "../lvglUI/ui.h"

#include "src/lib/driver_backends.h"
#include "src/lib/simulator_util.h"
#include "src/lib/simulator_settings.h"

#include "src/lib/voice_recognize.h"

/* Internal functions */
static void configure_simulator(int argc, char **argv);
static void print_lvgl_version(void);
static void print_usage(void);

/* contains the name of the selected backend if user
 * has specified one on the command line */
static char *selected_backend;

/* Global simulator settings, defined in lv_linux_backend.c */
extern simulator_settings_t settings;

/**
 * @brief Print LVGL version
 */
static void print_lvgl_version(void)
{
    fprintf(stdout, "%d.%d.%d-%s\n",
            LVGL_VERSION_MAJOR,
            LVGL_VERSION_MINOR,
            LVGL_VERSION_PATCH,
            LVGL_VERSION_INFO);
}

/**
 * @brief Print usage information
 */
static void print_usage(void)
{
    fprintf(stdout, "\nlvglsim [-V] [-B] [-b backend_name] [-W window_width] [-H window_height]\n\n");
    fprintf(stdout, "-V print LVGL version\n");
    fprintf(stdout, "-B list supported backends\n");
}

/**
 * @brief Configure simulator
 */
static void configure_simulator(int argc, char **argv)
{
    int opt = 0;

    selected_backend = NULL;
    driver_backends_register();

    const char *env_w = getenv("LV_SIM_WINDOW_WIDTH");
    const char *env_h = getenv("LV_SIM_WINDOW_HEIGHT");
    /* Default values */
    settings.window_width = atoi(env_w ? env_w : "800");
    settings.window_height = atoi(env_h ? env_h : "480");

    /* Parse the command-line options. */
    while ((opt = getopt (argc, argv, "b:fmW:H:BVh")) != -1) {
        switch (opt) {
        case 'h':
            print_usage();
            exit(EXIT_SUCCESS);
            break;
        case 'V':
            print_lvgl_version();
            exit(EXIT_SUCCESS);
            break;
        case 'B':
            driver_backends_print_supported();
            exit(EXIT_SUCCESS);
            break;
        case 'b':
            if (driver_backends_is_supported(optarg) == 0) {
                die("error no such backend: %s\n", optarg);
            }
            selected_backend = strdup(optarg);
            break;
        case 'W':
            settings.window_width = atoi(optarg);
            break;
        case 'H':
            settings.window_height = atoi(optarg);
            break;
        case ':':
            print_usage();
            die("Option -%c requires an argument.\n", optopt);
            break;
        case '?':
            print_usage();
            die("Unknown option -%c.\n", optopt);
        }
    }
}

// 声明全局变量
static lv_obj_t *kb = NULL;
static lv_obj_t *login_screen = NULL;
static lv_obj_t *info_screen = NULL;
static lv_obj_t *result_label = NULL;
static void voice_recognize_btn_event_handler(lv_event_t *e);

LV_FONT_DECLARE(song); // 声明中文字体

static void voice_recognize_btn_event_handler(lv_event_t *e)
{
    lv_label_set_text(result_label, "正在识别...");
    lv_obj_set_style_text_font(result_label, &song, 0);  // 设置中文字体
    lv_obj_invalidate(result_label);
    
    // 调用语音识别函数
    char *recognize_text = get_zipformer_result();
    
    if (recognize_text != NULL) {
        lv_label_set_text(result_label, recognize_text);
        free(recognize_text);
    } else {
        lv_label_set_text(result_label, "识别失败或无结果");
        lv_obj_set_style_text_font(result_label, &song, 0);  // 设置中文字体
    }
    
    lv_obj_invalidate(result_label);
}



int main(int argc, char **argv)
{
    configure_simulator(argc, argv);

    /* Initialize LVGL. */
    lv_init();

    /* Initialize the configured backend */
    if (driver_backends_init_backend(selected_backend) == -1) {
        die("Failed to initialize display backend");
    }

    /* Enable for EVDEV support */
#if LV_USE_EVDEV
    if (driver_backends_init_backend("EVDEV") == -1) {
        die("Failed to initialize evdev");
    }
#endif


// 创建结果显示标签
result_label = lv_label_create(lv_scr_act());
lv_label_set_text(result_label, "点击按钮开始语音识别");
lv_obj_set_style_text_font(result_label, &song, 0);  // 设置中文字体
lv_obj_align(result_label, LV_ALIGN_TOP_MID, 0, 50);

// 创建语音识别按钮
lv_obj_t *btn = lv_btn_create(lv_scr_act());
lv_obj_set_size(btn, 120, 50);
lv_obj_align(btn, LV_ALIGN_CENTER, 0, 0);
lv_obj_add_event_cb(btn, voice_recognize_btn_event_handler, LV_EVENT_CLICKED, NULL);

// 添加按钮标签
lv_obj_t *btn_label = lv_label_create(btn);
lv_obj_set_style_text_font(btn_label, &song, 0);  // 设置中文字体
lv_label_set_text(btn_label, "语音识别");
lv_obj_center(btn_label);


    //lv_example_ime_pinyin_1(); // 拼音输入法

    /* Enter the run loop of the selected backend */
    driver_backends_run_loop();

    return 0;
}