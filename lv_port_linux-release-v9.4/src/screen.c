/*******************************************************************
 *
 * main.c - LVGL simulator for GNU/Linux
 *
 * Based on the original file from the repository
 *
 * @note eventually this file won't contain a main function and will
 * become a library supporting all major operating systems
 *
 * To see how each driver is initialized check the
 * 'src/lib/display_backends' directory
 *
 * - Clean up
 * - Support for multiple backends at once
 *   2025 EDGEMTech Ltd.
 *
 * Author: EDGEMTech Ltd, Erik Tagirov (erik.tagirov@edgemtech.ch)
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

#include "src/lib/driver_backends.h"
#include "src/lib/simulator_util.h"
#include "src/lib/simulator_settings.h"

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
 * @description process arguments recieved by the program to select
 * appropriate options
 * @param argc the count of arguments in argv
 * @param argv The arguments
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

/**
 * @brief entry point
 * @description start a demo
 * @param argc the count of arguments in argv
 * @param argv The arguments
 */
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

    /*Create a Demo*/
  //  lv_demo_widgets();/* Create a 3x3 Color Grid */
    lv_obj_t *scr = lv_scr_act(); /* 获取当前活动屏幕 */
    
    /* 定义9种不同的颜色 */
    lv_color_t grid_colors[9] = {
        lv_color_hex(0xFF0000), /* 红 */
        lv_color_hex(0x00FF00), /* 绿 */
        lv_color_hex(0x0000FF), /* 蓝 */
        lv_color_hex(0xFFFF00), /* 黄 */
        lv_color_hex(0x00FFFF), /* 青 */
        lv_color_hex(0xFF00FF), /* 紫 */
        lv_color_hex(0xFFA500), /* 橙 */
        lv_color_hex(0x800080), /* 紫 */
        lv_color_hex(0x008000)  /* 深绿 */
    };

    /* 为了留出一点边距，我们可以稍微减小一点尺寸并居中，这里简化为平铺 */
    /* 使用较小的值作为正方形边长，确保圆形显示正确 */
    int32_t grid_size = 1024 / 3 < 600 / 3 ? 1024 / 3 : 600 / 3;
    int32_t grid_w = grid_size;
    int32_t grid_h = grid_size;

    /* 循环创建3x3的格子 */
    for (int row = 0; row < 3; row++) {
        for (int col = 0; col < 3; col++) {
            /* 计算当前格子的索引 */
            int index = row * 3 + col;

            /* 创建一个对象作为格子 */
            lv_obj_t * cell = lv_obj_create(scr);
            
            /* 设置对象的大小 */
            lv_obj_set_size(cell, grid_w, grid_h);
            
            /* 设置对象的位置 */
            lv_obj_set_pos(cell, col * grid_w, row * grid_h);
            
            /* 设置背景颜色 */
            lv_obj_set_style_bg_color(cell, grid_colors[index], 0);
            
            /* 移除默认的边框样式（可选，为了美观） */
            lv_obj_set_style_border_width(cell, 0, 0);

            /* 设置圆角半径为圆形 */
            lv_obj_set_style_radius(cell, LV_RADIUS_CIRCLE, 0);
        }
    }

   //lv_demo_widgets_start_slideshow();

    /* Enter the run loop of the selected backend */
    driver_backends_run_loop();

    return 0;
}
