#include "saveVideo.h"

int video_encoder_init(VideoEncoder* enc) {
    // 检查参数有效性
    if (!enc || !enc->output_file || enc->width <= 0 || enc->height <= 0) {
        return -1;
    }

    enc->fmt_ctx = NULL;
    enc->codec_ctx = NULL;
    enc->stream = NULL;
    enc->frame = NULL;
    enc->sws_ctx = NULL;
    enc->frame_count = 0;
    enc->initialized = 0;
    // 创建输出上下文
    if (avformat_alloc_output_context2(&enc->fmt_ctx, NULL, NULL, enc->output_file) < 0) {
        fprintf(stderr, "无法创建输出上下文\n");
        goto error;
    }
    // 查找编码器
    AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!codec) {
        fprintf(stderr, "找不到H.264编码器\n");
        goto error;
    }
    // 创建输出流
    enc->stream = avformat_new_stream(enc->fmt_ctx, NULL);
    if (!enc->stream) {
        fprintf(stderr, "无法创建输出流\n");
        goto error;
    }
    // 分配编码器上下文
    enc->codec_ctx = avcodec_alloc_context3(codec);
    if (!enc->codec_ctx) {
        fprintf(stderr, "无法分配编码器上下文\n");
        goto error;
    }
    // 配置编码参数
    enc->codec_ctx->codec_id = AV_CODEC_ID_H264; // 设置编码器ID
    enc->codec_ctx->width = enc->width; 
    enc->codec_ctx->height = enc->height;
    enc->codec_ctx->pix_fmt = AV_PIX_FMT_NV12; // 设置像素格式
    enc->codec_ctx->time_base = (AVRational){1, enc->frame_rate}; // 设置时间基准
    enc->codec_ctx->framerate = (AVRational){enc->frame_rate, 1}; // 设置帧率
    enc->codec_ctx->bit_rate = enc->bit_rate;     // 设置比特率
    enc->codec_ctx->rc_max_rate = enc->max_rate;  // 设置最大比特率
    enc->codec_ctx->rc_buffer_size = enc->max_rate; // 设置缓冲区大小
    enc->codec_ctx->qmin = 5;  // 最低量化参数（值越小质量越高）
    enc->codec_ctx->qmax = 25; // 最高量化参数（值越大质量越低）
    // 必须添加的硬件编码控制参数
    AVDictionary *options = NULL;
    av_dict_set(&options, "preset", "ultrafast", 0);  // 降低CPU消耗
    av_dict_set(&options, "tune", "zerolatency", 0);  // 嵌入式必选
    av_dict_set(&options, "profile", "baseline", 0);  // 兼容性优先


    // 打开编码器
    if (avcodec_open2(enc->codec_ctx, codec, NULL) < 0) {
        fprintf(stderr, "无法打开编码器\n");
        goto error;
    }
    // 复制编码参数到流
    avcodec_parameters_from_context(enc->stream->codecpar, enc->codec_ctx);
    // 打开输出文件
    if (!(enc->fmt_ctx->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&enc->fmt_ctx->pb, enc->output_file, AVIO_FLAG_WRITE) < 0) {
            fprintf(stderr, "无法打开输出文件\n");
            goto error;
        }
    }
    // 写入文件头
    if (avformat_write_header(enc->fmt_ctx, NULL) < 0) {
        fprintf(stderr, "写入头文件失败\n");
        goto error;
    }
    // 分配帧
    enc->frame = av_frame_alloc();
    if (!enc->frame) {
        fprintf(stderr, "无法分配视频帧\n");
        goto error;
    }
    enc->frame->format = enc->codec_ctx->pix_fmt;
    enc->frame->width = enc->codec_ctx->width;
    enc->frame->height = enc->codec_ctx->height;
    if (av_frame_get_buffer(enc->frame, 0) < 0) {
        fprintf(stderr, "无法分配帧缓冲区\n");
        goto error;
    }
    enc->initialized = 1;
    return 0;
error:
    video_encoder_release(enc);
    return -1;
}

int video_encoder_process(VideoEncoder* enc, uint8_t* cam_data) {
    if (!enc || !enc->initialized || !cam_data) {
        return -1;
    }
    // 直接填充NV12数据（零拷贝优化）
    enc->frame->data[0] = cam_data;                        // Y平面
    enc->frame->data[1] = cam_data + enc->width * enc->height;  // UV交错平面
    enc->frame->linesize[0] = enc->width;
    enc->frame->linesize[1] = enc->width;
    
    // 设置时间戳
    enc->frame->pts = enc->frame_count++;
    // 编码帧
    int ret = avcodec_send_frame(enc->codec_ctx, enc->frame);
    if (ret < 0) {
        fprintf(stderr, "发送帧到编码器失败: %d\n", ret);
        return -1;
    }
    // 接收并写入编码后的包
    while (ret >= 0) {
        AVPacket pkt = {0};
        av_init_packet(&pkt);
        ret = avcodec_receive_packet(enc->codec_ctx, &pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        } else if (ret < 0) {
            fprintf(stderr, "编码错误: %d\n", ret);
            av_packet_unref(&pkt);
            break;
        }
        
        // 设置时间戳
        pkt.pts = av_rescale_q(enc->frame_count - 1, 
                              enc->codec_ctx->time_base,
                              enc->stream->time_base);
        pkt.dts = pkt.pts;

        // 写入文件
        if (av_interleaved_write_frame(enc->fmt_ctx, &pkt) < 0) {
            fprintf(stderr, "写入帧失败\n");
            av_packet_unref(&pkt);
            break;
        }
        av_packet_unref(&pkt);
    }
    return 0;
}

void video_encoder_release(VideoEncoder* enc) {
    if (!enc) return;
    // 写入文件尾
    if (enc->fmt_ctx && enc->initialized) {
        av_write_trailer(enc->fmt_ctx);
    }
    // 释放资源
    if (enc->sws_ctx) {
        sws_freeContext(enc->sws_ctx);
        enc->sws_ctx = NULL;
    }
    if (enc->frame) {
        av_frame_free(&enc->frame);
        enc->frame = NULL;
    }
    if (enc->codec_ctx) {
        avcodec_free_context(&enc->codec_ctx);
        enc->codec_ctx = NULL;
    }
    if (enc->fmt_ctx) {
        if (!(enc->fmt_ctx->oformat->flags & AVFMT_NOFILE) && enc->fmt_ctx->pb) {
            avio_closep(&enc->fmt_ctx->pb);
        }
        avformat_free_context(enc->fmt_ctx);
        enc->fmt_ctx = NULL;
    }
    enc->initialized = 0;
    fprintf(stderr, "视频编码器资源已释放\n");
}