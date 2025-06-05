#include "person_detect_capi.h"
#include "person_detect.h"

#include <opencv2/opencv.hpp>
#include <vector>
#include <stdint.h>
#include <time.h>

// 全局变量，用于保存模型实例
static personDetect* g_pd = nullptr;

// 初始化函数，加载模型（只调用一次）
bool init_person_detector(const char* model_path, float conf_threshold, float nms_threshold, int num_class) {
    if (g_pd != nullptr) {
        // 已经初始化过了
        return true;
    }
    
    try {
        g_pd = new personDetect(model_path, conf_threshold, nms_threshold, num_class);
        return true;
    } catch (...) {
        g_pd = nullptr;
        return false;
    }
}

// 销毁函数，释放模型资源
void destroy_person_detector() {
    if (g_pd != nullptr) {
        delete g_pd;
        g_pd = nullptr;
    }
}

// 检测JPG图像
void detectjpg() {
    if (g_pd == nullptr) {
        fprintf(stderr, "Error: Person detector not initialized\n");
        return;
    }
    
    cv::Mat ori_img = cv::imread("./frame_result.jpg");
    if(ori_img.empty()) {
        fprintf(stderr, "Error: Failed to load image\n");
        return; 
    }
    
    const int ori_w = ori_img.cols;
    const int ori_h = ori_img.rows;
    
    // 处理流水线
    g_pd->pre_process(ori_img);
    g_pd->inference();

    std::vector<BoxInfo> results;
    g_pd->post_process({ori_w, ori_h}, results);

    // 绘制检测结果
    for (const auto& r : results) {
        const std::string text = "person:" + std::to_string(r.score).substr(0, 4);
        cv::rectangle(ori_img, 
                     cv::Rect(cv::Point(r.x1, r.y1), cv::Point(r.x2, r.y2)),
                     cv::Scalar(0, 0, 255), 2);
        cv::putText(ori_img, text, 
                   cv::Point(r.x1, r.y1 - 5),
                   cv::FONT_HERSHEY_SIMPLEX, 0.5,
                   cv::Scalar(0, 255, 255), 1);
    }
    fprintf(stderr, "Detected %zu persons\n", results.size());
    cv::imwrite("pd_result.jpg", ori_img);
}

// 检测帧数据
int detectframe(uint8_t* nv12_data, int width, int height) {
    if (g_pd == nullptr) {
        fprintf(stderr, "Error: Person detector not initialized\n");
        return -1;
    }
    // 将NV12转换为BGR格式（OpenCV常用格式）
    cv::Mat nv12_mat(height + height/2, width, CV_8UC1, nv12_data);
    cv::Mat ori_img;
    cv::cvtColor(nv12_mat, ori_img, cv::COLOR_YUV2BGR_NV12);
    if(ori_img.empty()) {
        fprintf(stderr, "Error: Failed to convert NV12 to BG3P\n");
        return -1; 
    }
    
    fprintf(stderr, "ori_img size: %d x %d\n", ori_img.cols, ori_img.rows);
    const int ori_w = ori_img.cols;
    const int ori_h = ori_img.rows;
    
    // 处理流水线
    g_pd->pre_process(ori_img);
    g_pd->inference();

    std::vector<BoxInfo> results;
    g_pd->post_process({ori_img.cols, ori_img.rows}, results);

    // 绘制检测结果
    for (const auto& r : results) {
        const std::string text = "person:" + std::to_string(r.score).substr(0, 4);
        fprintf(stderr, "Detected person at [%d, %d, %d, %d] with score %.2f\n", 
                r.x1, r.y1, r.x2, r.y2, r.score);
        cv::rectangle(ori_img, 
                     cv::Rect(cv::Point(r.x1, r.y1), cv::Point(r.x2, r.y2)),
                     cv::Scalar(0, 0, 255), 2);
        cv::putText(ori_img, text, 
                   cv::Point(r.x1, r.y1 - 5),
                   cv::FONT_HERSHEY_SIMPLEX, 0.5,
                   cv::Scalar(0, 255, 255), 1);
    }
    // fprintf(stderr, "Detected %zu persons\n", results.size());
    if( results.size() > 0 ){
        char filename[64];
        time_t now = time(NULL);
        struct tm *tm = localtime(&now);
        snprintf(filename, sizeof(filename), 
                "./pic/det_%04d%02d%02d_%02d%02d%02d.jpg",
                tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday,
                tm->tm_hour, tm->tm_min, tm->tm_sec);
        cv::imwrite(filename, ori_img);
        //cv::imwrite("./pic/frame_result.jpg", ori_img);
    }
   return results.size();
}




// // 实现C接口
// void detectjpg()
// {
//     cv::Mat ori_img = cv::imread("./test.jpg");
//     if(ori_img.empty()) {
//         // 错误处理：图像加载失败
//         return; 
//     }
    
//     const int ori_w = ori_img.cols;
//     const int ori_h = ori_img.rows;
    
//     // 创建检测器实例
//     personDetect pd("./model/person_detect_yolov5n.kmodel", 0.5f, 0.45f, 2);
    
//     // 处理流水线
//     pd.pre_process(ori_img);
//     pd.inference();

//     std::vector<BoxInfo> results;
//     pd.post_process({ori_w, ori_h}, results);

//     // 绘制检测结果
//     for (const auto& r : results)
//     {
//         const std::string text = "person:" + std::to_string(r.score).substr(0, 4);
//         cv::rectangle(ori_img, 
//                      cv::Rect(cv::Point(r.x1, r.y1), cv::Point(r.x2, r.y2)),
//                      cv::Scalar(0, 0, 255), 2);
//         cv::putText(ori_img, text, 
//                    cv::Point(r.x1, r.y1 - 5),
//                    cv::FONT_HERSHEY_SIMPLEX, 0.5,
//                    cv::Scalar(0, 255, 255), 1);
//     }
    
//     cv::imwrite("pd_result.jpg", ori_img);
// }

// // 实现C接口
// void detectframe(uint8_t* nv12_data, int width, int height)
// {
//     // 将NV12转换为BGR格式（OpenCV常用格式）
//     cv::Mat nv12_mat(height + height/2, width, CV_8UC1, nv12_data);
//     cv::Mat ori_img;
//     cv::cvtColor(nv12_mat, ori_img, cv::COLOR_YUV2BGR_NV12);
//     if(ori_img.empty()) {
//         // 错误处理：图像转换失败
//         return; 
//     }
//     fprintf(stderr, "ori_img size: %d x %d\n", ori_img.cols, ori_img.rows);
//     const int ori_w = ori_img.cols;
//     const int ori_h = ori_img.rows;
    
//     // 创建检测器实例
//     personDetect pd("./model/person_detect_yolov5n.kmodel", 0.5f, 0.45f, 2);
    
//     // 处理流水线
//     pd.pre_process(ori_img);
//     pd.inference();

//     std::vector<BoxInfo> results;
//     pd.post_process({ori_w, ori_h}, results);

//     // 绘制检测结果
//     for (const auto& r : results)
//     {
//         const std::string text = "person:" + std::to_string(r.score).substr(0, 4);
//         cv::rectangle(ori_img, 
//                      cv::Rect(cv::Point(r.x1, r.y1), cv::Point(r.x2, r.y2)),
//                      cv::Scalar(0, 0, 255), 2);
//         cv::putText(ori_img, text, 
//                    cv::Point(r.x1, r.y1 - 5),
//                    cv::FONT_HERSHEY_SIMPLEX, 0.5,
//                    cv::Scalar(0, 255, 255), 1);
//     }
    
//     cv::imwrite("frame_result.jpg", ori_img);
// }
