//
#include "../include/common.h"   


#define CAM_DEV     "/dev/video1"  // 摄像头设备路径
#define OUTPUT_FILE "./video/output.mp4"  // 视频输出文件名
#define FPS 60         // 设置帧率
#define camera_width 800
#define camera_height 480


// 检测线程的数据---------------------------------------------------------


typedef struct {
    pthread_mutex_t mutex;             // 互斥锁
    pthread_cond_t cond;               // 条件变量
    bool isready;                      // 就绪标志 // 有帧在处理时为false，无时为true
    struct mydisplay* det_disp;        // 显示设备
    uint8_t* frame_copy;               // 待检测数据
    int frame_width;
    int frame_height;    
} ThreadData;

// 检测线程函数
void* detection_thread(void* arg) {
    ThreadData* data = (ThreadData*)arg;
    while (1) {
        pthread_mutex_lock(&data->mutex);  // 上锁
        // 等待任务唤醒
        while (data->isready) {
            pthread_cond_wait(&data->cond, &data->mutex);
        }
        pthread_mutex_unlock(&data->mutex); // 释放锁
        
        // 执行检测
        clear_box(data->det_disp); // 清除方框显示
        struct all_det_location * all_location = detectframe(data->frame_copy, data->frame_width, data->frame_height);
        if( all_location != NULL) {
            //fprintf(stderr, "检测到 %d 个行人\n", num);
            draw_box(data->det_disp, all_location); // 绘制检测到的行人方框
        }
        
        // 任务结束，标记线程可接受新任务
        pthread_mutex_lock(&data->mutex);
        data->isready = true;
        pthread_mutex_unlock(&data->mutex);       

    }
    return NULL;
}


// 获取时间差的函数（返回毫秒）
double get_elapsed_ms(struct timespec *start, struct timespec *end) {
    return (end->tv_sec - start->tv_sec) * 1000.0 + 
           (end->tv_nsec - start->tv_nsec) / 1000000.0;
}

int main() {

    struct timespec start, end; // 用于计时的结构体

    // // 初始化显示
    struct mydisplay mydisp = { .width = 800, .height = 480, .disp_buf_index = 0 };
    if (drm_nv12_init(&mydisp) != 0) {
        fprintf(stderr, "显示初始化失败\n");
        return EXIT_FAILURE;
    }

    // 初始化行人检测模型
    if (!init_person_detector(
        "./model/person_detect_yolov5n.kmodel", 
        0.5, 0.3, 0)) {
        fprintf(stderr, "行人检测模型初始化失败\n");
        return EXIT_FAILURE;
    }

    // 初始化摄像头
    struct v4l2_capture cam = {0};
    if ( v4l2_init( &cam, CAM_DEV, camera_width, camera_height, 5)) {
        fprintf(stderr, "V4L2初始化失败\n");
        mydisplay_destroy(&mydisp);
        return EXIT_FAILURE;
    }

    // 初始化视频保存
    VideoEncoder enc = {
        .width = camera_width, .height = camera_height,
        .frame_rate = FPS, .bit_rate = 200000,
        .max_rate = 4000000, .output_file = OUTPUT_FILE
    };
    if (video_encoder_init(&enc) != 0) {
        fprintf(stderr, "编码器初始化失败\n");
        mydisplay_destroy(&mydisp);
        v4l2_destroy(&cam);
        return -1;
    }


    // 初始化线程数据
    ThreadData thread_data = {
        .isready = true,
        .det_disp = &mydisp,
        .frame_copy = malloc(camera_width * camera_height * 3 / 2), //NV12
        .frame_width = camera_width,
        .frame_height = camera_height
    };
    if (!thread_data.frame_copy) {
        fprintf(stderr, "帧缓冲区分配失败\n");
        return EXIT_FAILURE;
    }   
    pthread_mutex_init(&thread_data.mutex, NULL);  // 初始化互斥锁
    pthread_cond_init(&thread_data.cond, NULL);  

    // 创建检测线程
    pthread_t det_thread;
    if (pthread_create(&det_thread, NULL, detection_thread, &thread_data)) {
        fprintf(stderr, "无法创建识别线程\n");
        free(thread_data.frame_copy);
        return EXIT_FAILURE;
    }

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

        clock_gettime(CLOCK_MONOTONIC, &start);
        // 1、检查退出键
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

        // 2、获取帧数据
        //fprintf(stderr, "等待摄像头数据...\n");
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
        
        // 3、LCD显示处理
        int frame_index = (mydisp.disp_buf_index + 1)%3; // 计算下一个缓冲区索引
        memcpy(mydisp.disp_buf[frame_index]->map, cam_data, 
               mydisp.width * mydisp.height * 3 / 2);
                clock_gettime(CLOCK_MONOTONIC, &end);
        display_update_buffer(mydisp.disp_buf[frame_index], 0, 0); 
        int ret = display_commit(mydisp.disp);  
        if (ret < 0) {
            fprintf(stderr, "提交显示缓冲区失败: %d\n", ret);
            break;
        }
        else{
            //fprintf(stderr, "提交显示缓冲区成功，等待垂直同步\n");
            display_wait_vsync(mydisp.disp);  // 等待垂直同步
            mydisp.disp_buf_index = frame_index;  // 更新当前显示缓冲区索引
        }

        

        // 4、视频编码处理
        // fprintf(stderr, "处理视频编码...\n");
        if (video_encoder_process(&enc, cam_data) != 0) {
            fprintf(stderr, "视频编码处理失败\n");
            break;
        }
        
        // 5、线程识别
        pthread_mutex_lock(&thread_data.mutex);  // 互斥锁
        if (thread_data.isready ) {
            // 复制帧到识别缓冲区
            memcpy(thread_data.frame_copy, cam_data, camera_width * camera_height * 3 / 2);
            thread_data.isready = false; // 标记忙
            pthread_cond_signal(&thread_data.cond);  // 唤醒检测线程
        }
        pthread_mutex_unlock(&thread_data.mutex); // 释放锁

        // 6、重新入队缓冲区
        if (ioctl(cam.fd, VIDIOC_QBUF, &buf) < 0) {
            perror("入队失败");
            break;
        }
        //clock_gettime(CLOCK_MONOTONIC, &end);
        //printf("整个流程耗时: %.3f 毫秒\n", get_elapsed_ms(&start, &end));

        // 精确控制帧率
        struct timespec ts = {
            .tv_sec = 0,
            .tv_nsec = (long)(1.0/FPS * 1e9)
        };
        nanosleep(&ts, NULL);
    }
    // 清理线程
    pthread_mutex_lock(&thread_data.mutex);  // 
    thread_data.isready = false;
    pthread_cond_signal(&thread_data.cond); // 
    pthread_mutex_unlock(&thread_data.mutex); //
    pthread_join(det_thread, NULL);
    pthread_mutex_destroy(&thread_data.mutex);
    pthread_cond_destroy(&thread_data.cond); 
    free( thread_data.frame_copy);


    destroy_person_detector(); // 销毁识别资源
 
    // 恢复终端设置
    tcsetattr(STDIN_FILENO, TCSANOW, &old_term);
    
    // 释放资源
    mydisplay_destroy(&mydisp);
    v4l2_destroy(&cam);
    video_encoder_release(&enc);
   
    
    printf("资源已释放,程序已退出\n");
    return EXIT_SUCCESS;
}
