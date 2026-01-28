#pragma once

#include "types/datatype.h"
#include <opencv2/opencv.hpp>

struct LetterBoxInfo {
	bool hor;
	int pad;
};

LetterBoxInfo letterbox(const cv::Mat &img, cv::Mat &img_letterbox, float wh_ratio);
LetterBoxInfo letterbox_rga(const cv::Mat &img, cv::Mat &img_letterbox, float wh_ratio);
void cvimg2tensor(const cv::Mat &img, uint32_t width, uint32_t height, tensor_data_s &tensor);
void cvimg2tensor_rga(const cv::Mat &img, uint32_t width, uint32_t height, tensor_data_s &tensor);
