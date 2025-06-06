#include "v4l2.h"   

/*
* V4L2摄像头初始化
* @vcap: v4l2_capture结构体指针
* @dev: 摄像头设备文件路径
* @width: 采集宽度
* @height: 采集高度
* @buffer_count: 申请的缓冲区数量 
* @return: 0 成功, -1 失败  
*/
int v4l2_init(struct v4l2_capture *vcap, const char *dev, uint32_t width, uint32_t height, uint32_t buffer_count) {
    if(!vcap || !dev || width == 0 || height == 0 || buffer_count < 3) {
        fprintf(stderr, "无效的参数\n");
        return -1;
    }
    vcap->width = width;   // 设置摄像头宽度,实际工作时不一定是这个（设置后查询）
    vcap->height = height; // 
    vcap->fd = -1;         // 初始化文件描述符为-1
    vcap->buffers = NULL;  // 初始化缓冲区指针为NULL

    // 1、获得设备文件描述符 
    if ((vcap->fd = open(dev, O_RDWR)) < 0) {
        perror("打开摄像头设备失败");
        return -1;
    }
    
    // 必要的设备检查
    struct v4l2_capability cap;  // 查询设备能力
    CLEAR(cap);
    if (ioctl(vcap->fd, VIDIOC_QUERYCAP, &cap) < 0) {
        perror("设备能力查询失败");
        goto error;
    }
    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        fprintf(stderr, "设备不支持视频捕获\n");
        goto error;
    }

    // 2、设置视频捕获格式，并验证结果（记录在fmt中）
    struct v4l2_format fmt; // 设置采集格式
    CLEAR(fmt);
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;  // 设置缓冲区类型
    fmt.fmt.pix.width = vcap->width;
    fmt.fmt.pix.height = vcap->height;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_NV12; // 设置像素格式为NV12
    fmt.fmt.pix.field = V4L2_FIELD_NONE;         // 设置为逐行扫描
    if (ioctl(vcap->fd, VIDIOC_S_FMT, &fmt) < 0) {
        perror("设置格式失败");
        goto error;
    }
    // 查询实际的采集尺寸
    fprintf(stderr, "设置采集格式: %ux%u, 像素格式: %c%c%c%c\n", 
            fmt.fmt.pix.width, fmt.fmt.pix.height,
            (fmt.fmt.pix.pixelformat >> 0) & 0xFF,
            (fmt.fmt.pix.pixelformat >> 8) & 0xFF,
            (fmt.fmt.pix.pixelformat >> 16) & 0xFF,
            (fmt.fmt.pix.pixelformat >> 24) & 0xFF);
    vcap->pitch = fmt.fmt.pix.bytesperline;// 更新行步长，用于后期解码
    fprintf(stderr, "摄像头行步长: %u bytes\n", vcap->pitch);

    // 3、申请帧缓冲区
    struct v4l2_requestbuffers req; //
    CLEAR(req);
    req.count = buffer_count; //缓冲区数量过多会导致画面延迟
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;  // 设置缓冲区类型
    req.memory = V4L2_MEMORY_MMAP;  // 使用内存映射方式
    if (ioctl(vcap->fd, VIDIOC_REQBUFS, &req) < 0) {
        perror("采集缓冲区申请失败");
        goto error;
    }
    if (req.count < 2) {
        fprintf(stderr, "采集缓冲区数量不足:%u\n", req.count);
        goto error;
    }
    else{
        fprintf(stderr, "申请采集缓冲区数量: %u\n", req.count);
    }

    // 4、映射内核缓冲区到用户空间 
    if (!(vcap->buffers = calloc(req.count, sizeof(struct buffer)))) {
        perror("采集缓冲区记录结构体的内存分配失败");
        goto error;
    }    
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
    
    // 5、缓冲区入队
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

/*
* 释放v4l2资源
* 
*/
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