#ifndef PERSON_DETECT_CAPI_H
#define PERSON_DETECT_CAPI_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct det_location{
    int x1;   // 左上角x坐标
    int y1;   // 左上角y坐标
    int x2;   // 右下角x坐标
    int y2;   // 右下角y坐标
    float score; // 检测得分
};

struct all_det_location{
    struct det_location** locations; // 检测到的行人位置数组
    int count; // 检测到的行人数量
};

bool init_person_detector(const char* model_path, float conf_threshold, float nms_threshold, int num_class);
void destroy_person_detector();
void detectjpg();
struct all_det_location* detectframe(uint8_t* nv12_data, int width, int height);


#ifdef __cplusplus
}
#endif



#endif // PERSON_DETECT_CAPI_H