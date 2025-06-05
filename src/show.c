#include "show.h"

// 在文件作用域定义（show.c 顶部或底部）
static void dummy_page_flip_handler(int fd, unsigned int sequence, unsigned int q,
                                   unsigned int tv_sec, void *data) 
{
    // 这是一个空实现，什么也不做
    (void)fd; (void)sequence; (void)tv_sec; (void)data;
}

int drm_nv12_init(struct mydisplay* mydis)  // 初始化显示
{
    // 1. 初始化显示
    mydis->disp = display_init(0);
    if (!mydis->disp) {
        fprintf(stderr, "Display初始化失败\n");
        goto error;
    }
    mydis->disp->drm_event_ctx.page_flip_handler = dummy_page_flip_handler;
    // 2. 获取NV12格式的plane
    mydis->plane = display_get_plane( mydis->disp, DRM_FORMAT_NV12);
    if (!mydis->plane) {
        fprintf(stderr, "没有NV12 plane可用\n");
        goto error;
    }
    // 3. 预分配显示缓冲区（双缓冲）
    for( int i = 0; i < 3; i++) {
        mydis->disp_buf[i] = display_allocate_buffer( mydis->plane, 480, 800);
        if (!mydis->disp_buf[i]) {
            fprintf(stderr, "分配显示缓冲区%u失败\n",i);
            goto error;
        }
    }
    // 4. 初始提交第一个缓冲
    mydis->disp_buf_index = 0;  // 初始化显示缓冲区索引为0
    display_commit_buffer(mydis->disp_buf[0], 0, 0);
    
    
    // 2. 分配处理帧缓冲区（480x800的NV12格式） LCD显示
    mydis->process_frame = malloc(mydis->width*mydis->height * 3 / 2);
    if (! mydis->process_frame) {
        perror("分配处理帧缓冲区失败");
        goto error;
    }
    return 0;  // 成功返回0
error:
    mydisplay_destroy(mydis);  // 销毁显示资源
    return -1;  // 失败返回-1
}

void mydisplay_destroy(struct mydisplay* mydis) {
    if (!mydis) return;  // 检查指针有效性
    for( int i = 0; i < 3; i++) {
        if (mydis->disp_buf[i]) {
            display_free_buffer(mydis->disp_buf[i]);  // 释放显示缓冲区
            mydis->disp_buf[i] = NULL;  // 清空指针
        }
    }
    if (mydis->plane) {
        display_free_plane(mydis->plane);  // 释放显示平面
        mydis->plane = NULL;  // 清空指针
    }
    if (mydis->process_frame) {
        free(mydis->process_frame);  // 释放处理帧缓冲区
        mydis->process_frame = NULL;  // 清空指针
    }
    if (mydis->disp) {
        display_exit(mydis->disp);  // 退出显示
        mydis->disp = NULL;  // 清空指针
    }
    fprintf(stderr, "显示资源已销毁\n");
}

void process_frame_nv12(
    uint8_t* nv12_frame,  // 输入 NV12 帧地址 (Y 平面 + UV 交织平面)
    int width,            // 帧宽 (需为偶数)
    int height,           // 帧高 (需为偶数)
    uint8_t* out_buffer,  // 输出缓冲区 (转换后的 NV12 帧)
    int screen_width,     // 屏幕宽 (需为偶数)
    int screen_height,    // 屏幕高 (需为偶数)
    int rotation          // 旋转角度 (0, 90, 180, 270)
) {
    // 1. 参数检查和强制偶数对齐
    if (width <= 0 || height <= 0 || screen_width <= 0 || screen_height <= 0) 
        return;
    
    width &= ~1;
    height &= ~1;
    screen_width &= ~1;
    screen_height &= ~1;

    // 2. 清空输出缓冲区（设置为黑色）
    memset(out_buffer, 0, screen_width * screen_height);      // Y平面设为0（黑色）
    // 修改点1：确保UV平面完全初始化为128（中性色）
    memset(out_buffer + screen_width * screen_height, 128, screen_width * screen_height / 2); // UV平面设为128（中性色）

    // 3. 计算旋转后的逻辑尺寸
    int src_width = (rotation % 180) ? height : width;
    int src_height = (rotation % 180) ? width : height;

    // 4. 计算缩放比例（保持宽高比）
    long scale_x = (long)screen_width * 1024 / src_width;
    long scale_y = (long)screen_height * 1024 / src_height;
    long scale = (scale_x < scale_y) ? scale_x : scale_y;
    int scaled_w = (scale * src_width) / 1024;
    int scaled_h = (scale * src_height) / 1024;
    scaled_w &= ~1;  // 确保为偶数
    scaled_h &= ~1;

    // 5. 计算居中位置
    int start_x = (screen_width - scaled_w) / 2;
    int start_y = (screen_height - scaled_h) / 2;
    start_x &= ~1;
    start_y &= ~1;

    // 6. 获取输入帧的 Y 和 UV 平面指针
    uint8_t* y_plane = nv12_frame;
    uint8_t* uv_plane = nv12_frame + width * height;

    // 7. 获取输出帧的 Y 和 UV 平面指针
    uint8_t* out_y = out_buffer;
    uint8_t* out_uv = out_buffer + screen_width * screen_height;

    // 8. 处理旋转和缩放
    for (int dy = 0; dy < scaled_h; dy++) {
        for (int dx = 0; dx < scaled_w; dx++) {
            // 计算归一化坐标 (定点数)
            long u = (long)dx * 1024 * 1024 / scaled_w;
            long v = (long)dy * 1024 * 1024 / scaled_h;

            // 根据旋转角度计算源坐标
            int x_orig, y_orig;
            switch (rotation) {
                case 0:   // 无旋转
                    x_orig = (u * width) >> 20;
                    y_orig = (v * height) >> 20;
                    break;
                case 90:  // 顺时针90°
                    x_orig = (v * width) >> 20;
                    y_orig = height - 1 - ((u * height) >> 20);
                    break;
                case 180: // 180°
                    x_orig = width - 1 - ((u * width) >> 20);
                    y_orig = height - 1 - ((v * height) >> 20);
                    break;
                case 270: // 270° (逆时针90°)
                    x_orig = width - 1 - ((v * width) >> 20);
                    y_orig = (u * height) >> 20;
                    break;
                default:  // 无效角度按0°处理
                    x_orig = (u * width) >> 20;
                    y_orig = (v * height) >> 20;
            }

            // 边界保护
            x_orig = (x_orig < 0) ? 0 : (x_orig >= width) ? width - 1 : x_orig;
            y_orig = (y_orig < 0) ? 0 : (y_orig >= height) ? height - 1 : y_orig;

            // 计算目标位置（居中）
            int out_x = start_x + dx;
            int out_y_pos = start_y + dy;

            // 复制 Y 分量
            out_y[out_y_pos * screen_width + out_x] = y_plane[y_orig * width + x_orig];

            // 修改点2：确保所有UV分量都被正确处理
            if ((dx % 2 == 0) && (dy % 2 == 0)) {
                int uv_x = x_orig / 2;
                int uv_y = y_orig / 2;
                int uv_offset = uv_y * width + 2 * uv_x;
                int out_uv_offset = (out_y_pos / 2) * screen_width + (out_x & ~1);
                out_uv[out_uv_offset] = uv_plane[uv_offset];      // U
                out_uv[out_uv_offset + 1] = uv_plane[uv_offset + 1]; // V
            }
        }
    }
}