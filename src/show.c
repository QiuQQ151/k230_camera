#include "show.h"
#include "../include/person_detect_capi.h"  // 识别检测的对外接口C接口
// 在文件作用域定义（show.c 顶部或底部）
static void dummy_page_flip_handler(int fd, unsigned int sequence, unsigned int q,
                                   unsigned int tv_sec, void *data) 
{
    // 这是一个空实现，什么也不做
    (void)fd; (void)sequence; (void)tv_sec; (void)data;
}


/*
* 初始化显示
* 1. 初始化显示设备
* 2. 获取NV12格式的plane
* 3. 预分配显示缓冲区（三缓冲）
* 4. 提交第一个缓冲区
* 5. 分配处理帧缓冲区（NV12格式）（存储处理后的帧数据）
*/
int drm_nv12_init(struct mydisplay* mydis)  // 初始化显示
{
    // 1. 初始化主显示层
    mydis->disp = display_init(0);
    if (!mydis->disp) {
        fprintf(stderr, "Display初始化失败\n");
        goto error;
    }
    int rotation_flag = 0;  // 旋转标志，0表示不旋转，1表示旋转90度
    if( mydis->width != mydis->disp->width || mydis->height != mydis->disp->height) {
        fprintf(stderr, "显示尺寸不匹配，硬件的默认尺寸: %ux%u，设置旋转90度\n", mydis->disp->width, mydis->disp->height);
        rotation_flag = 1;           // 设置旋转标志
        mydis->disp->drm_rotation = rotation_90; // 设置旋转为90度
    }
    mydis->disp->drm_event_ctx.page_flip_handler = dummy_page_flip_handler;  // 必须设置


    // 2. 获取NV12格式的plane
    mydis->plane = display_get_plane( mydis->disp, DRM_FORMAT_NV12);
    if( rotation_flag){
        mydis->plane->drm_rotation = rotation_90; // 设置plane的旋转为90度
    }
    if (!mydis->plane) {
        fprintf(stderr, "没有NV12 plane可用\n");
        goto error;
    }


    // 3. 预分配显示缓冲区（双缓冲）// 根据物理硬件尺寸填写
    for( int i = 0; i < 3; i++) {
        mydis->disp_buf[i] = display_allocate_buffer( mydis->plane,mydis->disp->width, mydis->disp->height);  
        if (!mydis->disp_buf[i]) {
            fprintf(stderr, "分配显示缓冲区%u失败\n",i);
            goto error;
        }
        // 设置缓冲区的旋转角度
        fprintf(stderr, "缓冲区%u旋转角度: %d\n", i, mydis->disp_buf[i]->drm_rotation);
    }

    // 4. 初始提交第一个缓冲
    mydis->disp_buf_index = 0;  // 初始化显示缓冲区索引为0
    display_commit_buffer(mydis->disp_buf[0], 0, 0);
    
    // 初始化方框显示层---------------------------------------------------------------------------
    mydis->box_plane = display_get_plane(mydis->disp, DRM_FORMAT_ARGB8888);
    if (!mydis->box_plane) {
        fprintf(stderr, "警告：无法获取ARGB平面，方框功能将禁用\n");
        mydis->box_plane = NULL; // 标记为不可用
    } else {
        // 配置方框层旋转
        if(rotation_flag) {
            mydis->box_plane->drm_rotation = rotation_90;
        }
        
        // 分配方框缓冲区
        mydis->box_buf = display_allocate_buffer(mydis->box_plane,
                                               mydis->disp->width,
                                               mydis->disp->height);
        if (!mydis->box_buf) {
            fprintf(stderr, "警告：方框缓冲区分配失败\n");
            display_free_plane(mydis->box_plane);
            mydis->box_plane = NULL;
        } else {
            // 初始化为全透明
            memset(mydis->box_buf->map, 0x00000000, mydis->box_buf->size); // 少一个0都不行
            display_commit_buffer(mydis->box_buf, 0, 0);
            // fprintf(stderr, "方框层初始化成功 (缓冲区 stride=%u)\n", 
            //        mydis->box_buf->stride);
        }
    }

    
    // 2. 分配处理帧缓冲区（NV12格式） LCD显示
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
    if (mydis->box_plane) {
        display_free_buffer(mydis->box_buf);  // 释放方框缓冲区
        mydis->box_buf = NULL;  // 清空指针
        display_free_plane(mydis->box_plane);  // 释放显示平面
        mydis->box_buf = NULL;  // 清空指针
        fprintf(stderr, "方框平面已释放\n");
    }    
    if (mydis->plane) {
        for( int i = 0; i < 3; i++) {
            if (mydis->disp_buf[i]) {
                display_free_buffer(mydis->disp_buf[i]);  // 释放显示缓冲区
                mydis->disp_buf[i] = NULL;  // 清空指针
            }
        }
        display_free_plane(mydis->plane);  // 释放显示平面
        mydis->plane = NULL;  // 清空指针
        fprintf(stderr, "显示平面已释放\n");
    }

    if (mydis->process_frame) {
        free(mydis->process_frame);  // 释放处理帧缓冲区
        mydis->process_frame = NULL;  // 清空指针
    }
    if (mydis->disp) {
        display_exit(mydis->disp);  // 退出显示
        mydis->disp = NULL;  // 清空指针
        fprintf(stderr, "显示设备已退出\n");
    }
    fprintf(stderr, "显示资源已销毁\n");
}



void draw_box(struct mydisplay *mydis, struct all_det_location* all_loc) 
{
    // 简单有效性检查
    if(!mydis->box_buf || !all_loc) return;  

    // 清空方框区域（透明）
    memset(mydis->box_buf->map, 0x00000000, mydis->box_buf->size);
    
    // 绘制第i个框
    for( int i = 0; i < all_loc->count; i++) {
        struct det_location* loc = all_loc->locations[i];
        draw_one_box(mydis, loc->x1, loc->y1, loc->x2, loc->y2);
        // 释放内存
        free(loc);  // 释放每个检测结果的内存
    }
    free(all_loc);
    // 提交显示
    display_commit_buffer(mydis->box_buf, 0, 0); 
}

/**
 * 简易版方框绘制（K230适用）
 * @param mydis  显示控制结构体
 * @param x1,y1 左上角坐标
 * @param x2,y2 右下角坐标
 * 格式：ARGB8888
 */
void draw_one_box(struct mydisplay *mydis, int x10, int y10, int x20, int y20) 
{
    if(!mydis->box_buf) return;  // 简单有效性检查

    uint32_t *pixels = mydis->box_buf->map;
    uint32_t pitch = mydis->box_buf->stride / 4;  // 每行像素数
    // 计算方框坐标
    int x1 = 480 - y20;
    int y1 = x10;
    int x2 = 480 - y10;
    int y2 = x20;

    // 检查坐标是否在显示范围内
    if (x1 < 0 || y1 < 0 || x2 >= mydis->disp->width || y2 >= mydis->disp->height) {
        fprintf(stderr, "方框坐标超出显示范围：width%d, height%d \n", 
                mydis->width, mydis->height);
        fprintf(stderr, "x1=%d, y1=%d, x2=%d, y2=%d\n", x1, y1, x2, y2);
        return;  // 坐标超出范围，直接返回
    }
    // 绘制2像素宽的红色边框
    uint32_t red = 0xFFFF0000; // ARGB红色
    for(int t = 0; t < 2; t++) {
        // 上边框
        for(int x = x1; x <= x2; x++) pixels[(y1+t)*pitch + x] = red;
        // 下边框
        for(int x = x1; x <= x2; x++) pixels[(y2-t)*pitch + x] = red;
        // 左边框
        for(int y = y1; y <= y2; y++) pixels[y*pitch + (x1+t)] = red;
        // 右边框
        for(int y = y1; y <= y2; y++) pixels[y*pitch + (x2-t)] = red;
    }
}

void clear_box(struct mydisplay *mydis) 
{
    if(!mydis->box_buf) return;  // 简单有效性检查
    // 清空方框区域（透明）
    memset(mydis->box_buf->map, 0x00000000, mydis->box_buf->size);
    // 提交显示
    display_commit_buffer(mydis->box_buf, 0, 0); 
}

/*
* 软件帧处理函数
* 输入 NV12 格式的帧数据，输出处理后的帧数据
* 支持旋转和缩放，保持宽高比
*/

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

// 生成NV12测试帧（YUV420格式）
void generate_nv12_test_frame(uint8_t *nv12_data, int width, int height) {
    // Y分量（亮度）
    uint8_t *y_plane = nv12_data;
    // UV分量（交错存储）
    uint8_t *uv_plane = nv12_data + width * height;
    // 1. 生成Y平面（渐变灰阶+十字线）
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            // 渐变彩条（水平方向）
            uint8_t y_value;
            if (x < width / 4) {
                y_value = 255 * x / (width / 4);          // 左到右渐亮
            } else if (x < width / 2) {
                y_value = 255 - 255 * (x - width/4) / (width/4); // 右到左渐暗
            } else if (x < 3 * width / 4) {
                y_value = 128 + 127 * (x - width/2) / (width/4); // 中间渐变
            } else {
                y_value = 255 - 128 * (x - 3*width/4) / (width/4); // 最后一段
            }
            // 添加绿色十字线（5像素宽）
            if (abs(x - width/2) <= 2 || abs(y - height/2) <= 2) {
                y_value = 150;  // 十字线亮度
            }
            y_plane[y * width + x] = y_value;
        }
    }
    // 2. 生成UV平面（固定色度）
    for (int y = 0; y < height / 2; y++) {
        for (int x = 0; x < width / 2; x++) {
            int uv_index = y * width + x * 2;
            // 分区设置不同色度（示例：左上红色，右上绿色，左下蓝色，右下黄色）
            if (x < width/4 && y < height/4) {
                uv_plane[uv_index]     = 84;   // U
                uv_plane[uv_index + 1] = 255;  // V (红色)
            } else if (x >= width/4 && y < height/4) {
                uv_plane[uv_index]     = 44;   // U
                uv_plane[uv_index + 1] = 21;   // V (绿色)
            } else if (x < width/4 && y >= height/4) {
                uv_plane[uv_index]     = 255;  // U
                uv_plane[uv_index + 1] = 107;  // V (蓝色)
            } else {
                uv_plane[uv_index]     = 16;   // U
                uv_plane[uv_index + 1] = 146;  // V (黄色)
            }
        }
    }
}
