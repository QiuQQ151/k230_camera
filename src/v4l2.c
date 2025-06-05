#include "v4l2.h"   

int v4l2_init(struct v4l2_capture *vcap, const char *dev, uint32_t width, uint32_t height) {
    // vcap是重要参数，整个流程都依靠vcap来进行
    vcap->width = width;  // 设置摄像头宽度,实际工作时不一定是这个
    vcap->height = height; // 设置摄像头高度,实际工作时不一定是这个

    struct v4l2_capability cap;  // 查询设备能力
    struct v4l2_format fmt; // 设置时评采集格式
    struct v4l2_requestbuffers req; //
    
    // 获得设备文件描述符
    if ((vcap->fd = open(dev, O_RDWR)) < 0) {
        perror("打开摄像头设备失败");
        return -1;
    }
    
    // 查询设备能力
    CLEAR(cap);
    if (ioctl(vcap->fd, VIDIOC_QUERYCAP, &cap) < 0) {
        perror("设备能力查询失败");
        goto error;
    }
    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        fprintf(stderr, "设备不支持视频捕获\n");
        goto error;
    }

    // 设置视频捕获格式，并验证结果（记录在fmt中）
    CLEAR(fmt);
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = vcap->width;
    fmt.fmt.pix.height = vcap->height;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_NV12; //V4L2_PIX_FMT_BGR24
    fmt.fmt.pix.field = V4L2_FIELD_INTERLACED; // 隔行扫描
    if (ioctl(vcap->fd, VIDIOC_S_FMT, &fmt) < 0) {
        perror("设置格式失败");
        goto error;
    }
    if (fmt.fmt.pix.pixelformat != V4L2_PIX_FMT_NV12) {
        fprintf(stderr, "警告：摄像头格式非NV12 (实际: %#x)\n", 
                fmt.fmt.pix.pixelformat);
    }
    
    // 更新行步长，用于后期解码
    vcap->pitch = fmt.fmt.pix.bytesperline;
    fprintf(stderr, "摄像头行步长: %u bytes\n", vcap->pitch);

    // 申请缓冲区,并验证
    CLEAR(req);
    req.count =  10; //申请10个缓冲区
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;  // 设置缓冲区类型
    req.memory = V4L2_MEMORY_MMAP;  // 使用内存映射方式
    
    if (ioctl(vcap->fd, VIDIOC_REQBUFS, &req) < 0) {
        perror("采集缓冲区申请失败");
        goto error;
    }
    fprintf(stderr, "申请采集缓冲区数量: %u\n", req.count);
    if (req.count < 2) {
        fprintf(stderr, "采集缓冲区数量不足\n");
        goto error;
    }

    if (!(vcap->buffers = calloc(req.count, sizeof(struct buffer)))) {
        perror("采集缓冲区记录结构体的内存分配失败");
        goto error;
    }
    
    // 映射内核缓冲区到用户空间 
    for (vcap->n_buffers = 0; vcap->n_buffers < req.count; vcap->n_buffers++) {
        //依次映射申请的缓冲区
        struct v4l2_buffer buf; 
        CLEAR(buf);
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE; // 设置缓冲区类型
        buf.memory = V4L2_MEMORY_MMAP; // 设置内存映射方式
        buf.index = vcap->n_buffers;  // 设置缓冲区索引
        
        if (ioctl(vcap->fd, VIDIOC_QUERYBUF, &buf) < 0) {
            perror("查询采集缓冲区失败");
            goto buffer_error;
        }
        
        // 分配缓冲区内存 // mmap映射
        vcap->buffers[vcap->n_buffers].length = buf.length;
        vcap->buffers[vcap->n_buffers].start = mmap(  // 记录帧缓冲区的起始地址
            NULL, // 映射地址 自动选择
            buf.length, // 映射长度
            PROT_READ | PROT_WRITE, // 映射权限
            MAP_SHARED, // 共享映射
            vcap->fd, // 文件描述符 // 映射的设备内核缓冲区
            buf.m.offset  // 缓冲区偏移
        );
           // 打印映射的缓冲区信息
        fprintf(stderr, "映射缓冲区 %u: 地址=%p, 长度=%u bytes\n", 
                vcap->n_buffers, vcap->buffers[vcap->n_buffers].start, 
                vcap->buffers[vcap->n_buffers].length);
        
        if (vcap->buffers[vcap->n_buffers].start == MAP_FAILED) {
            perror("mmap失败");
            goto buffer_error;
        }
    }
    fprintf(stderr, "采集缓冲区映射成功: %u buffers\n", vcap->n_buffers);
    
    // 将所有缓冲区入队
    for (unsigned int i = 0; i < vcap->n_buffers; i++) {
        struct v4l2_buffer buf;
        CLEAR(buf);
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        
        if (ioctl(vcap->fd, VIDIOC_QBUF, &buf) < 0) {
            perror("入队采集缓冲区失败");
            goto stream_error;
        }
    }
    fprintf(stderr, "所有采集缓冲区入队成功\n");
    
    // 启动视频流
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(vcap->fd, VIDIOC_STREAMON, &type) < 0) {
        perror("启动流失败");
        goto stream_error;
    }
    fprintf(stderr, "视频流已启动\n");
    
    return 0;

stream_error:
    for (unsigned int i = 0; i < vcap->n_buffers; i++) {
        munmap(vcap->buffers[i].start, vcap->buffers[i].length);  // 取消映射
    }
buffer_error:
    free(vcap->buffers);  // 释放记录采集缓冲区的结构体内存
error:
    close(vcap->fd);  // 关闭设备文件描述符
    return -1;
}

void v4l2_destroy(struct v4l2_capture *vcap) {
    if (!vcap || vcap->fd == -1) return;
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE; // 定义缓冲区类型
    ioctl(vcap->fd, VIDIOC_STREAMOFF, &type); // 停止视频流
    fprintf(stderr, "视频流已停止\n");
    if (vcap->buffers) {
        for (unsigned int i = 0; i < vcap->n_buffers; i++) {
            munmap(vcap->buffers[i].start, vcap->buffers[i].length); // 取消映射
        }
        free(vcap->buffers);
        vcap->buffers = NULL; // 清空指针
    }
    fprintf(stderr, "释放采集缓冲区资源\n");
    close(vcap->fd); // 关闭设备文件描述符
    vcap->fd = -1;
    fprintf(stderr, "摄像头设备已关闭\n");
}