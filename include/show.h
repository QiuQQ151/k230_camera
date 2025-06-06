#ifndef SHOW_H
#define SHOW_H
#include "../include/common.h"   
#include "../include/person_detect_capi.h"  // 识别检测的对外接口C接口

struct mydisplay {
    // 显示硬件相关
    int width;          // 显示宽度
    int height;         // 显示高度
    struct display* disp;          // 显示设备
    struct display_plane* plane;        // 主显示平面
    struct display_buffer* disp_buf[3]; // 显示缓冲区(双缓冲)

    struct display_plane* box_plane;        // 方框平面
    struct display_buffer* box_buf;         // 方框缓冲区 单个

    int disp_buf_index;                 // 当前显示缓冲区索引
    // 处理帧缓冲区
    uint8_t* process_frame;             // 存储用于LCD显示的NV12帧（处理后的帧）
};



int drm_nv12_init(struct mydisplay* mydis);  // 初始化显示
void mydisplay_destroy(struct mydisplay* mydis); // 销毁显示资源
void draw_box(struct mydisplay *mydis, struct all_det_location* all_loc); // 绘制检测到的行人方框
void draw_one_box(struct mydisplay *mydis, int x1, int y1, int x2, int y2);  // 绘制方框
void clear_box(struct mydisplay *mydis); // 清除方框显示

// 处理 NV12 帧，旋转并缩放到指定屏幕尺寸
void process_frame_nv12(  // // 处理 NV12 帧，旋转并缩放到指定屏幕尺寸
    uint8_t* nv12_frame,  // 输入 NV12 帧地址 (Y 平面 + UV 交织平面)
    int width,            // 帧宽 (需为偶数)
    int height,           // 帧高 (需为偶数)
    uint8_t* out_buffer,  // 输出缓冲区 (转换后的 NV12 帧)
    int screen_width,     // 屏幕宽 (需为偶数)
    int screen_height,    // 屏幕高 (需为偶数)
    int rotation          // 旋转角度 (0, 90, 180, 270)
);



#endif // SHOW_H