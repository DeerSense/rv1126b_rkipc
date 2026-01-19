#pragma once
#include "engine/engine.hpp"

#include <memory>

#include <opencv2/opencv.hpp>
#include "process/preprocess.h"
#include "types/yolo_datatype.h"

class Yolo26
{
public:
    Yolo26();
    ~Yolo26();

    nn_error_e LoadModel(const char *model_path);

    nn_error_e Run(const cv::Mat &img, std::vector<Detection> &objects);

private:
    nn_error_e Preprocess(const cv::Mat &img, cv::Mat &image_letterbox);
    nn_error_e Inference();
    nn_error_e Postprocess(const cv::Mat &img, std::vector<Detection> &objects);

    bool ready_;
    LetterBoxInfo letterbox_info_;
    tensor_data_s input_tensor_;
    std::vector<tensor_data_s> output_tensors_;
    bool want_float_;
    std::vector<int32_t> out_zps_;
    std::vector<float> out_scales_;
    std::shared_ptr<NNEngine> engine_;
};
