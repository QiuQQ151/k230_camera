#ifndef PERSON_DETECT_CAPI_H
#define PERSON_DETECT_CAPI_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool init_person_detector(const char* model_path, float conf_threshold, float nms_threshold, int num_class);
void destroy_person_detector();
void detectjpg();
int detectframe(uint8_t* nv12_data, int width, int height);


#ifdef __cplusplus
}
#endif



#endif // PERSON_DETECT_CAPI_H