#include "person_detect.h"
#include "vi_vo.h"

// 源码来源：k230_sdk 例程

// for image
personDetect::personDetect(const char *kmodel_file, float obj_thresh,float nms_thresh, const int debug_mode) 
: obj_thresh_(obj_thresh),nms_thresh_(nms_thresh), AIBase(kmodel_file,"personDetect", debug_mode)
{

    model_name_ = "personDetect";
    ai2d_out_tensor_ = get_input_tensor(0);
}   

// for video
personDetect::personDetect(const char *kmodel_file, float obj_thresh,float nms_thresh, FrameCHWSize isp_shape, const int debug_mode) 
: obj_thresh_(obj_thresh),nms_thresh_(nms_thresh), AIBase(kmodel_file,"personDetect", debug_mode)
{
    model_name_ = "personDetect";
    isp_shape_ = isp_shape;
    dims_t in_shape{1, isp_shape_.channel, isp_shape_.height, isp_shape_.width};
    int isp_size = isp_shape_.channel * isp_shape_.height * isp_shape_.width;

    // ai2d_out_tensor
    ai2d_out_tensor_ = get_input_tensor(0);
    // fixed padding resize param
    Utils::padding_resize(isp_shape_, {input_shapes_[0][3], input_shapes_[0][2]}, ai2d_builder_, ai2d_in_tensor_, ai2d_out_tensor_, cv::Scalar(114, 114, 114),false);
}

personDetect::~personDetect()
{

}

// ai2d for image
void personDetect::pre_process(cv::Mat ori_img)
{
    ScopedTiming st(model_name_ + " pre_process image", debug_mode_);
    std::vector<uint8_t> chw_vec;
    Utils::hwc_to_chw(ori_img, chw_vec);
    Utils::padding_resize({ori_img.channels(), ori_img.rows, ori_img.cols}, chw_vec, {input_shapes_[0][3], input_shapes_[0][2]}, ai2d_out_tensor_, cv::Scalar(114, 114, 114));
}

// ai2d for video
void personDetect::pre_process(runtime_tensor& img_data)
{
    ScopedTiming st(model_name_ + " pre_process video", debug_mode_);
    ai2d_builder_->invoke(img_data,ai2d_out_tensor_).expect("error occurred in ai2d running");
}

void personDetect::inference()
{
    this->run();
    this->get_output();
}

static float sigmoid(float x)
{
    return 1.0f / (1.0f + expf(-x));
}

void nms(std::vector<BoxInfo> &input_boxes, float NMS_THRESH)
{
    std::sort(input_boxes.begin(), input_boxes.end(), [](BoxInfo a, BoxInfo b) { return a.score > b.score; });
    std::vector<float> vArea(input_boxes.size());
    for (int i = 0; i < int(input_boxes.size()); ++i)
    {
        vArea[i] = (input_boxes.at(i).x2 - input_boxes.at(i).x1 + 1)
            * (input_boxes.at(i).y2 - input_boxes.at(i).y1 + 1);
    }
    for (int i = 0; i < int(input_boxes.size()); ++i)
    {
        for (int j = i + 1; j < int(input_boxes.size());)
        {
            float xx1 = std::max(input_boxes[i].x1, input_boxes[j].x1);
            float yy1 = std::max(input_boxes[i].y1, input_boxes[j].y1);
            float xx2 = std::min(input_boxes[i].x2, input_boxes[j].x2);
            float yy2 = std::min(input_boxes[i].y2, input_boxes[j].y2);
            float w = std::max(float(0), xx2 - xx1 + 1);
            float h = std::max(float(0), yy2 - yy1 + 1);
            float inter = w * h;
            float ovr = inter / (vArea[i] + vArea[j] - inter);
            if (ovr >= NMS_THRESH)
            {
                input_boxes.erase(input_boxes.begin() + j);
                vArea.erase(vArea.begin() + j);
            }
            else
            {
                j++;
            }
        }
    }
}

// for NHWC
std::vector<BoxInfo> decode_infer(float *data, int net_size, int stride, int num_classes, FrameSize frame_size, float anchors[][2], float threshold)
{
    float ratiow = (float)net_size / frame_size.width;
    float ratioh = (float)net_size / frame_size.height;
    float gain = ratiow < ratioh ? ratiow : ratioh;
    std::vector<BoxInfo> result;
    int grid_size = net_size / stride;
    int one_rsize = num_classes + 5;
    float cx, cy, w, h;
    for (int shift_y = 0; shift_y < grid_size; shift_y++)
    {
        for (int shift_x = 0; shift_x < grid_size; shift_x++)
        {
            int loc = shift_x + shift_y * grid_size;
            for (int i = 0; i < 3; i++)
            {
                float *record = data + (loc * 3 + i) * one_rsize;
                float *cls_ptr = record + 5;
                for (int cls = 0; cls < num_classes; cls++)
                {
                    // float score = sigmoid(cls_ptr[cls]) * sigmoid(record[4]);
                    float score = cls_ptr[cls] * record[4];
                    if (score > threshold)
                    {
                        // cx = (sigmoid(record[0]) * 2.f - 0.5f + (float)shift_x) * (float)stride;
                        // cy = (sigmoid(record[1]) * 2.f - 0.5f + (float)shift_y) * (float)stride;
                        // w = pow(sigmoid(record[2]) * 2.f, 2) * anchors[i][0];
                        // h = pow(sigmoid(record[3]) * 2.f, 2) * anchors[i][1];

                        cx = (record[0] * 2.f - 0.5f + (float)shift_x) * (float)stride;
                        cy = (record[1] * 2.f - 0.5f + (float)shift_y) * (float)stride;
                        w = pow(record[2] * 2.f, 2) * anchors[i][0];
                        h = pow(record[3] * 2.f, 2) * anchors[i][1];

                        cx -= ((net_size - frame_size.width * gain) / 2);
                        cy -= ((net_size - frame_size.height * gain) / 2);
                        cx /= gain;
                        cy /= gain;
                        w /= gain;
                        h /= gain;
                        BoxInfo box;
                        box.x1 = std::max(0, std::min<int>(frame_size.width, int(cx - w / 2.f)));
                        box.y1 = std::max(0, std::min<int>(frame_size.height, int(cy - h / 2.f)));
                        box.x2 = std::max(0, std::min<int>(frame_size.width, int(cx + w / 2.f)));
                        box.y2 = std::max(0, std::min<int>(frame_size.height, int(cy + h / 2.f)));
                        box.score = score;
                        box.label = cls;
                        result.push_back(box);
                    }
                }
            }
        }
    }
    return result;
}



void personDetect::post_process(FrameSize frame_size,std::vector<BoxInfo> &result)
{
    ScopedTiming st(model_name_ + " post_process", debug_mode_);
    int net_len = input_shapes_[0][2];
    // first output
    {

        float *output_0 = p_outputs_[0];

        int first_len = net_len / 8;
        int first_size = first_len * first_len;
       
        auto boxes0 = decode_infer(output_0, net_len, 8, classes_num_, frame_size, anchors_0_, obj_thresh_);
        result.insert(result.begin(), boxes0.begin(), boxes0.end());
    }

    // second output
    {

        float *output_1 = p_outputs_[1];

        int second_len = net_len / 16;
        int second_size = second_len * second_len;

        auto boxes1 = decode_infer(output_1, net_len, 16, classes_num_, frame_size, anchors_1_, obj_thresh_);
        result.insert(result.begin(), boxes1.begin(), boxes1.end());
    }
    
    // third output
    {
        float *output_2 = p_outputs_[2];

        int third_len = net_len / 32;
        int third_size = third_len * third_len;

        auto boxes2 = decode_infer(output_2, net_len, 32, classes_num_, frame_size, anchors_2_, obj_thresh_);
        result.insert(result.begin(), boxes2.begin(), boxes2.end());
    }

    nms(result, nms_thresh_);
}
