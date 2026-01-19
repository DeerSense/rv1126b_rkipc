#pragma once


#include <opencv2/opencv.hpp>

#include "types/yolo_datatype.h"

// draw detections on img
void DrawDetections(cv::Mat& img, const std::vector<Detection>& objects);

