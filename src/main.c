//
#include "../include/common.h"   


#define CAM_DEV     "/dev/video1"  // 摄像头设备路径
#define OUTPUT_FILE "./video/output.mp4"  // 视频输出文件名
#define FPS 60         // 设置帧率
#define BUFFER_SIZE 3  // 环形缓冲区大小

typedef struct {
    uint8_t *data;      // 帧数据指针
    int index;          // 缓冲区索引
    int width;          // 帧宽度
    int height;         // 帧高度
    int processed;      // 是否已处理标志
} FrameBuffer;

typedef struct {
    FrameBuffer buffers[BUFFER_SIZE];  // 环形缓冲区
    int write_idx;                     // 写入位置
    int read_idx;                      // 读取位置
    int count;                         // 当前帧数
    pthread_mutex_t mutex;             // 互斥锁
    pthread_cond_t cond;               // 条件变量
    int shutdown;                      // 终止标志
} ThreadData;

// 检测线程函数
void* detection_thread(void* arg) {
    ThreadData* data = (ThreadData*)arg;
    
    while (1) {
        pthread_mutex_lock(&data->mutex);
        
        // 等待有可处理帧或终止信号
        while (data->count == 0 && !data->shutdown) {
            pthread_cond_wait(&data->cond, &data->mutex);
        }
        
        if (data->shutdown) {
            pthread_mutex_unlock(&data->mutex);
            break;
        }
        
        // 获取当前帧
        FrameBuffer *frame = &data->buffers[data->read_idx];
        data->read_idx = (data->read_idx + 1) % BUFFER_SIZE;
        data->count--;
        pthread_mutex_unlock(&data->mutex);
        
        // 执行检测
        int num = detectframe(frame->data, frame->width, frame->height);
        fprintf(stderr, "检测到 %d 个行人\n", num);
    }
    
    return NULL;
}

int main() {
    // // 初始化行人检测模型
    // if (!init_person_detector(
    //     "./model/person_detect_yolov5n.kmodel", 
    //     0.5, 0.3, 2)) {
    //     fprintf(stderr, "行人检测模型初始化失败\n");
    //     return EXIT_FAILURE;
    // }

    // 初始化显示
    struct mydisplay mydisp = {
        .width = 480, .height = 800
    };
    if (drm_nv12_init(&mydisp) != 0) {
        fprintf(stderr, "显示初始化失败\n");
        return EXIT_FAILURE;
    }

    // 初始化摄像头
    struct v4l2_capture cam = {0};
    if ( v4l2_init( &cam, CAM_DEV, 1920, 1080, 3)) {
        fprintf(stderr, "V4L2初始化失败\n");
        mydisplay_destroy(&mydisp);
        return EXIT_FAILURE;
    }

    // 初始化视频保存
    VideoEncoder enc = {
        .width = 1920, .height = 1080,
        .frame_rate = FPS, .bit_rate = 200000,
        .max_rate = 4000000, .output_file = OUTPUT_FILE
    };
    if (video_encoder_init(&enc) != 0) {
        fprintf(stderr, "编码器初始化失败\n");
        mydisplay_destroy(&mydisp);
        v4l2_destroy(&cam);
        return -1;
    }
    
    // // 初始化线程数据
    // ThreadData thread_data = {
    //     .write_idx = 0, .read_idx = 0, .count = 0, .shutdown = 0
    // };
    // pthread_mutex_init(&thread_data.mutex, NULL);
    // pthread_cond_init(&thread_data.cond, NULL);

    // // 创建检测线程
    // pthread_t det_thread;
    // if (pthread_create(&det_thread, NULL, detection_thread, &thread_data)) {
    //     fprintf(stderr, "无法创建识别线程\n");
    //     return EXIT_FAILURE;
    // }

    // 设置终端为非阻塞模式
    struct termios old_term, new_term;
    tcgetattr(STDIN_FILENO, &old_term);
    new_term = old_term;
    new_term.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &new_term);
    printf("已启动摄像头到显示屏的流媒体\n");
    printf("按回车键退出程序\n");
    int t =0;
    while (1) {
        // 检查退出键
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(STDIN_FILENO, &readfds);
        struct timeval timeout = {.tv_sec = 0, .tv_usec = 0};
        if (select(STDIN_FILENO + 1, &readfds, NULL, NULL, &timeout) > 0) {
            if (getchar() != EOF) {
                printf("检测到按键，退出程序\n");
                break;
            }
        }
        
        // 获取帧数据
        fprintf(stderr, "等待摄像头数据...\n");
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        if (ioctl(cam.fd, VIDIOC_DQBUF, &buf) < 0) {
            if (errno == EAGAIN) {
                usleep(5000);
                continue;
            }
            perror("出队失败");
            break;
        }
        uint8_t *cam_data = (uint8_t*)cam.buffers[buf.index].start;
        
        // 视频显示处理
        fprintf(stderr, "处理摄像头数据...\n");
        process_frame_nv12(
            cam_data, 1920, 1080,
            mydisp.process_frame, mydisp.width, mydisp.height, 90
        );
  
        // 将处理后的帧数据复制到显示缓冲区
        int frame_index = (mydisp.disp_buf_index + 1)%3; // 计算下一个缓冲区索引
        memcpy(mydisp.disp_buf[frame_index]->map, mydisp.process_frame, 
               mydisp.width * mydisp.height * 3 / 2);
        display_update_buffer(mydisp.disp_buf[frame_index], 0, 0); 
        int ret = display_commit(mydisp.disp);  
        if (ret < 0) {
            fprintf(stderr, "提交显示缓冲区失败: %d\n", ret);
            break;
        }
        else{
            fprintf(stderr, "提交显示缓冲区成功，等待垂直同步\n");
            display_wait_vsync(mydisp.disp);  // 等待垂直同步
            mydisp.disp_buf_index = frame_index;  // 更新当前显示缓冲区索引
        }
        
  


        // 视频编码处理
        fprintf(stderr, "处理视频编码...\n");
        if (video_encoder_process(&enc, cam_data) != 0) {
            fprintf(stderr, "视频编码处理失败\n");
            break;
        }
        
        // // 将帧加入检测队列（使用互斥锁保护）
        // fprintf(stderr, "将帧加入检测队列...\n");
        // pthread_mutex_lock(&thread_data.mutex);
        // if (thread_data.count < BUFFER_SIZE) {
        //     FrameBuffer *frame = &thread_data.buffers[thread_data.write_idx];
        //     frame->data = cam_data;
        //     frame->width = 1920;
        //     frame->height = 1080;
        //     frame->index = thread_data.write_idx;
            
        //     thread_data.write_idx = (thread_data.write_idx + 1) % BUFFER_SIZE;
        //     thread_data.count++;
            
        //     pthread_cond_signal(&thread_data.cond);  // 唤醒检测线程
        // }
        // pthread_mutex_unlock(&thread_data.mutex);
        
        // 重新入队缓冲区
        if (ioctl(cam.fd, VIDIOC_QBUF, &buf) < 0) {
            perror("入队失败");
            break;
        }
        
        // 精确控制帧率
        struct timespec ts = {
            .tv_sec = 0,
            .tv_nsec = (long)(1.0/FPS * 1e9)
        };
        nanosleep(&ts, NULL);
    }

    // // 清理线程
    // pthread_mutex_lock(&thread_data.mutex);
    // thread_data.shutdown = 1;
    // pthread_cond_signal(&thread_data.cond);
    // pthread_mutex_unlock(&thread_data.mutex);
    // pthread_join(det_thread, NULL);
    // pthread_mutex_destroy(&thread_data.mutex);
    // pthread_cond_destroy(&thread_data.cond);
    // destroy_person_detector();

    // 恢复终端设置
    tcsetattr(STDIN_FILENO, TCSANOW, &old_term);
    
    // 释放资源
    mydisplay_destroy(&mydisp);
    v4l2_destroy(&cam);
    video_encoder_release(&enc);
   
    
    printf("资源已释放,程序已退出\n");
    return EXIT_SUCCESS;
}
