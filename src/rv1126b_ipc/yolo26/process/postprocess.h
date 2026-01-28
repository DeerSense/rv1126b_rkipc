#pragma once
#include <stdint.h>
#include <vector>

int get_top(float *pfProb, float *pfMaxProb, uint32_t *pMaxClass, uint32_t outputCount,
            uint32_t topNum);

namespace yolo {
int GetConvDetectionResult(float **pBlob, std::vector<float> &DetectiontRects); // 浮点数版本
int GetConvDetectionResultInt8(int8_t **pBlob, std::vector<int> &qnt_zp,
                               std::vector<float> &qnt_scale,
                               std::vector<float> &DetectiontRects); // int8版本
} // namespace yolo
