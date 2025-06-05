#ifndef SHOW_H
#define SHOW_H
#include "../include/common.h"   


struct mydisplay {
    // 显示硬件相关
    int width;          // 显示宽度
    int height;         // 显示高度
    struct display* disp;          // 显示设备
    struct display_plane* plane;        // osd叠加层平面
    struct display_buffer* disp_buf[3]; // 显示缓冲区(双缓冲)
    int disp_buf_index;                 // 当前显示缓冲区索引
    // 处理帧缓冲区
    uint8_t* process_frame;             // 存储用于LCD显示的NV12帧（处理后的帧）
};



int drm_nv12_init(struct mydisplay* mydis);  // 初始化显示
void mydisplay_destroy(struct mydisplay* mydis); // 销毁显示资源

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