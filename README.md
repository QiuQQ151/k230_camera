# 基于庐山派的camera监控（识别）

# 待解决的性能问题
## 摄像头帧率输出达不到60hz（硬件参数）

## d display_commit(mydisp.disp); 等待时间 10ms

# Bug记录
## display_commit(mydisp.disp) 报错(已解决)20250606
- drmModeAtomicCommit ret is -12
- 运行一段时间后出现报错，出现报错之后会开始卡顿
    - 资源耗尽
- 解决方案：使用原子操作，查看源码
    - display_update_buffer
        - 源码内申请了drmModeAtomicAlloc
    - display_commit
    - display_wait_vsync（一开始缺少，调用后进一步报错）
        - 由于未添加等待垂直同步，运行久之后会出现drmModeAtomicCommit ret is -12
        - 源码中，垂直同步将会删除display_update_buffer申请的drmModeAtomicAlloc
            - 但是在调用display_wait_vsync时会出现段错误！
            - 即非法内存访问
            - 查看源码中的访问内容，涉及文件描述符、上下文操作等
            - 从display_init()中可知，虽然都分配了struct display的内容，但是回调函数'display->drm_event_ctx.page_flip_handler = page_flip_handler'仅作标识，未找到具体实现
            - 在初始化函数文件中添加回调函数实例（可为空），修改display->drm_event_ctx.page_flip_handler =具体回调函数
    - 问题解决

## 程序退出问题（解决）20250606
- 原因：识别线程缺少退出标志，主线程一直等待识别线程退出
- 添加退出标志位，主线程退出while时，唤醒线程进行退出操作
- 线程唤醒后立刻进行退出检查，并安全退出
- 使用互斥锁保护共享标志位（退出标志、空闲标志）

## 保存视频函数导致间断性卡顿
- 读写速度问题
- 采用的mount nfs挂载Ubuntu主机文件夹会导致间断性写入卡顿，改为本地运行

## v4l2在应用重复运行结束再运行时会无法启动
- 20250605
```c
摄像头行步长: 1920 bytes
申请采集缓冲区数量: 3
映射缓冲区 0: 地址=0x3fb99dc000, 长度=3110400 bytes
映射缓冲区 1: 地址=0x3fb96e4000, 长度=3110400 bytes
映射缓冲区 2: 地址=0x3fb93ec000, 长度=3110400 bytes
采集缓冲区映射成功: 3 buffers
所有采集缓冲区入队成功
启动流失败: Invalid argument
V4L2初始化失败
显示资源已销毁
[root@canaan /mnt/k230_camera ]#
```
## mmpeg编码器错误
```c
[h264_v4l2m2m @ 0x2ab7db5010] Using device /dev/video0
[h264_v4l2m2m @ 0x2ab7db5010] driver 'mvx' on card 'Linlon Video device' in mplane mode
[h264_v4l2m2m @ 0x2ab7db5010] requesting formats: output=NV12 capture=H264
[h264_v4l2m2m @ 0x2ab7db5010] Failed to set timeperframe
```

## 更新函设置数帧率过低会显示异常
- 数据帧格式问题

## 模型导入不能先于drm初始化，否则导致display_wait_vsync段错误
- 先初始化显示能解决，原理？