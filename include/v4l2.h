#ifndef V4L2_H
#define V4L2_H

#include "../include/common.h"   

// 记录帧缓冲区信息
struct buffer {
    void *start;  // 缓冲区起始地址
    size_t length;
};

struct v4l2_capture {
    int fd;   // 设备文件描述符
    struct buffer *buffers;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;          // 摄像头行步长
    unsigned int n_buffers;  // 缓冲区数量
    uint32_t pix_format;     // 像素格式
};

int v4l2_init(struct v4l2_capture *vcap, const char *dev, uint32_t width, uint32_t height, uint32_t buffer_count) ;
void v4l2_destroy(struct v4l2_capture *vcap);

#endif // V4L2_H
