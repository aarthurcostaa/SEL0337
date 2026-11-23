// Detector YOLO (Ultralytics, exportado com format="ncnn") rodando no ncnn.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#if __has_include(<ncnn/net.h>)
#include <ncnn/net.h>
#else
#include <net.h>
#endif

#include "postprocess.hpp"

class YoloNcnn {
public:
    struct Options {
        int num_threads = 4;     // Pi 3 tem 4 núcleos Cortex-A53
        float conf = 0.35f;      // mesmo padrão do nó Python
        float iou = 0.7f;        // padrão do Ultralytics (o nó Python não mudava)
        int max_det = 300;
        bool agnostic_nms = false;
    };

    // model_dir = pasta gerada pelo export (model_ncnn_model/), contendo
    // model.ncnn.param, model.ncnn.bin e metadata.yaml. Lança std::runtime_error.
    YoloNcnn(const std::string& model_dir, const Options& opt);

    // Imagem RGB 8 bits; stride em bytes. Caixas em pixels da própria imagem.
    std::vector<yolo::Detection> detect(const uint8_t* rgb, int width, int height, int stride);

    const yolo::ModelMeta& meta() const { return meta_; }

private:
    ncnn::Net net_;
    yolo::ModelMeta meta_;
    Options opt_;
};
