#ifndef COMMON_H
#define COMMON_H
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <stdbool.h>
#include <termios.h>
#include <unistd.h>
#include <math.h>
#include <time.h>
#include <pthread.h>


#include <linux/videodev2.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavutil/opt.h> 

#include <display.h>
#include <drm_fourcc.h>
#include <drm_mode.h>  

#include <sys/time.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <sys/types.h>  // 提供基本系统数据类型
#include <sys/stat.h>   // 提供文件状态相关的定义
#include <sys/mman.h>

#include <fcntl.h>      // 提供 open() 和 O_RDWR, O_CLOEXEC
#include <errno.h>      // 提供 errno 和 perror()

#include <xf86drm.h>
#include <xf86drmMode.h>

// 
#include "../include/v4l2.h"                // 摄像头相关
#include "../include/show.h"                // 显示相关
#include "../include/saveVideo.h"           // 视频保存相关
#include "../include/person_detect_capi.h"  // 识别检测的对外接口C接口

#define CLEAR(x) memset(&(x), 0, sizeof(x))

#endif // COMMON_H
