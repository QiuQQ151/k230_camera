#include "person_detect_capi.h"
#include "person_detect.h"
#include "show.h"
#include <opencv2/opencv.hpp>
#include <vector>
#include <stdint.h>
#include <time.h>

// 全局变量，用于保存模型实例
static personDetect* g_pd = nullptr;

// 初始化函数，加载模型
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
struct all_det_location* detectframe(uint8_t* nv12_data, int width, int height) {
    if (g_pd == nullptr) {
        fprintf(stderr, "Error: Person detector not initialized\n");
        return NULL;
    }
    // 将NV12转换为BGR格式（OpenCV常用格式）
    cv::Mat nv12_mat(height + height/2, width, CV_8UC1, nv12_data);
    cv::Mat ori_img;
    cv::cvtColor(nv12_mat, ori_img, cv::COLOR_YUV2BGR_NV12);
    if(ori_img.empty()) {
        fprintf(stderr, "Error: Failed to convert NV12 to BG3P\n");
        return NULL; 
    }
    
    //fprintf(stderr, "ori_img size: %d x %d\n", ori_img.cols, ori_img.rows);
    const int ori_w = ori_img.cols;
    const int ori_h = ori_img.rows;
    
    // 处理流水线
    g_pd->pre_process(ori_img);
    g_pd->inference();

    std::vector<BoxInfo> results;
    g_pd->post_process({ori_img.cols, ori_img.rows}, results);

    // 如果没有检测到人，直接返回NULL
    if (results.empty()) {
        //fprintf(stderr, "No persons detected\n");
        return NULL;
    }

    // 返回的结构体
    struct all_det_location* ret_all = (struct all_det_location*) malloc( sizeof(all_det_location) );
    ret_all->count = results.size(); // 检测到的行人数量
    ret_all->locations = (struct det_location**) malloc( sizeof(struct det_location*) * ret_all->count );
    for( int i = 0; i < ret_all->count; i++ ) {
        ret_all->locations[i] = (struct det_location*) malloc( sizeof(struct det_location) );
    }

    // 绘制检测结果
    int i = 0;
    for (const auto& r : results) {
        const std::string text = "person:" + std::to_string(r.score).substr(0, 4);
        // 标准化输出格式
        // printf("[Person] Box: (%.2f, %.2f) -> (%0.2f, %.2f) | Score: %.2f\n", 
        //         r.x1, r.y1, r.x2, r.y2, r.score);
        // 将检测结果存入返回结构体
        ret_all->locations[i]->x1 = (int)r.x1 -1; // 左上角x坐标
        ret_all->locations[i]->y1 = (int)r.y1 -1; // 左上角y坐标   
        ret_all->locations[i]->x2 = (int)r.x2 -1; // 右下角x坐标
        ret_all->locations[i]->y2 = (int)r.y2 -1; // 右下角y坐标
        ret_all->locations[i]->score = r.score; // 检测得分
        i++;
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
       // cv::imwrite(filename, ori_img); //耗时60ms
    }
   return ret_all; // 返回检测结果位置
}

