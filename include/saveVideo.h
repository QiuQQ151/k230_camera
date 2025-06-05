#ifndef SAVE_VIDEO_H
#define SAVE_VIDEO_H

#include "../include/common.h"   

typedef struct {
    // 配置参数
    int width;
    int height;
    int frame_rate;
    int bit_rate;
    int max_rate;
    const char* output_file;
    
    // FFmpeg相关对象
    AVFormatContext* fmt_ctx;
    AVCodecContext* codec_ctx;
    AVStream* stream;
    AVFrame* frame;
    struct SwsContext* sws_ctx;
    
    // 状态变量
    int64_t frame_count;
    int initialized;
} VideoEncoder;


int video_encoder_init(VideoEncoder* enc);
int video_encoder_process(VideoEncoder* enc, uint8_t* cam_data) ;
void video_encoder_release(VideoEncoder* enc);


#endif // SAVE_VIDEO_H