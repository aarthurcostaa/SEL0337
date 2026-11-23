#include "yolo_ncnn.hpp"

#include <cstdio>
#include <stdexcept>

namespace {
// Nomes dos blobs que o pnnx gera no export do Ultralytics (ver model.ncnn.param).
constexpr const char* kInputBlob = "in0";
constexpr const char* kOutputBlob = "out0";
}  // namespace

YoloNcnn::YoloNcnn(const std::string& model_dir, const Options& opt) : opt_(opt) {
    if (!yolo::load_metadata(model_dir + "/metadata.yaml", meta_)) {
        std::fprintf(stderr, "[aviso] metadata.yaml não encontrado; usando imgsz=%dx%d e classes numéricas\n",
                     meta_.imgsz_w, meta_.imgsz_h);
    }

    // As opções precisam ser definidas ANTES de load_param/load_model.
    net_.opt.use_vulkan_compute = false;   // Pi 3 não tem Vulkan utilizável
    net_.opt.num_threads = opt_.num_threads;
    net_.opt.lightmode = true;             // libera blobs intermediários assim que possível
    net_.opt.use_fp16_storage = true;      // pesos já estão em fp16 (half=True no export)
    net_.opt.use_packing_layout = true;

    const std::string param = model_dir + "/model.ncnn.param";
    const std::string bin = model_dir + "/model.ncnn.bin";
    if (net_.load_param(param.c_str()) != 0)
        throw std::runtime_error("falha ao carregar " + param);
    if (net_.load_model(bin.c_str()) != 0)
        throw std::runtime_error("falha ao carregar " + bin);
}

std::vector<yolo::Detection> YoloNcnn::detect(const uint8_t* rgb, int width, int height, int stride) {
    const yolo::Letterbox lb = yolo::make_letterbox(width, height, meta_.imgsz_w, meta_.imgsz_h);

    // 1) resize (a imagem já vem em RGB, que é o que o Ultralytics usa)
    ncnn::Mat resized = ncnn::Mat::from_pixels_resize(rgb, ncnn::Mat::PIXEL_RGB, width, height, stride,
                                                      lb.new_w, lb.new_h);

    // 2) bordas cinza 114 até fechar 320x320 (o modelo exportado tem entrada fixa)
    ncnn::Mat in;
    ncnn::copy_make_border(resized, in, lb.top, lb.bottom, lb.left, lb.right, ncnn::BORDER_CONSTANT, 114.f);

    // 3) normaliza para [0, 1]
    const float norm[3] = {1 / 255.f, 1 / 255.f, 1 / 255.f};
    in.substract_mean_normalize(nullptr, norm);

    // 4) inferência
    ncnn::Extractor ex = net_.create_extractor();
    ex.input(kInputBlob, in);
    ncnn::Mat out;
    if (ex.extract(kOutputBlob, out) != 0)
        throw std::runtime_error("falha na inferência (extract out0)");

    // out: h = 4 + num_classes linhas, w = num_anchors colunas (5 x 2100 aqui)
    std::vector<const float*> rows(out.h);
    for (int r = 0; r < out.h; ++r) rows[r] = out.row(r);

    return yolo::decode(rows.data(), out.h, out.w, lb, width, height,
                        opt_.conf, opt_.iou, opt_.max_det, opt_.agnostic_nms);
}
