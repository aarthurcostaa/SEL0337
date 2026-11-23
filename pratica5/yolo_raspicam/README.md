# yolo_raspicam

Detecção com YOLO11n em C++ na Raspberry Pi 3, usando a câmera oficial.

## Visão geral

Este projeto executa o YOLO11n, exportado para o formato ncnn, numa Raspberry Pi 3 com a câmera oficial. A câmera é lida pela libcamera por meio do GStreamer, e a inferência é feita pelo ncnn, sem OpenCV nem ROS.

Para cada quadro processado, o programa imprime uma linha JSON com as detecções. Quadros sem detecção geram uma lista vazia, o que permite distinguir a ausência de objetos de uma falha do detector.

O vídeo anotado é opcional e pode ser enviado para uma janela, para um arquivo ou pela rede. As caixas só são desenhadas quando alguma dessas saídas está ligada, para poupar processamento na Pi.

## Organização do código

O código está na pasta `src`:

- `main.cpp` contém o laço principal;
- `gst_io` trata a entrada e a saída de vídeo;
- `yolo_ncnn` realiza a inferência;
- `postprocess.hpp` aplica o NMS.

Os arquivos do export do modelo estão na pasta `model_ncnn_model`.

## Preparação da Raspberry Pi

A detecção da câmera foi verificada inicialmente com o `rpicam-hello`:

```bash
rpicam-hello --list-cameras
```

Em seguida, foram instalados os pacotes necessários. O primeiro grupo reúne as ferramentas de compilação, o núcleo do GStreamer e os plugins de captura, e é obrigatório. O segundo traz os codificadores e sinks usados pelas saídas de vídeo, além das ferramentas de teste.

```bash
sudo apt update
sudo apt install -y build-essential cmake git pkg-config \
    libgstreamer1.0-dev gstreamer1.0-libcamera gstreamer1.0-plugins-base
sudo apt install -y gstreamer1.0-plugins-good gstreamer1.0-tools
```

O funcionamento da câmera no GStreamer foi confirmado com o pipeline abaixo, interrompido com Ctrl+C:

```bash
gst-launch-1.0 libcamerasrc ! video/x-raw,width=640,height=480 ! videoconvert ! fakesink -v
```

## Compilação do ncnn

O ncnn foi compilado a partir do código-fonte, porque versões antigas da biblioteca podem não carregar o modelo exportado. A compilação foi feita com `make -j2` para não esgotar a memória da Pi.

```bash
cd ~
git clone --depth 1 https://github.com/Tencent/ncnn.git
cd ncnn && mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local \
      -DNCNN_VULKAN=OFF -DNCNN_BUILD_EXAMPLES=OFF -DNCNN_BUILD_TOOLS=OFF \
      -DNCNN_BUILD_BENCHMARK=OFF -DNCNN_BUILD_TESTS=OFF ..
make -j2
sudo make install
```

## Compilação e execução

O projeto é compilado com o CMake:

```bash
cd yolo_raspicam
mkdir build && cd build
cmake ..
make -j2
```

Quando o ncnn está instalado fora de `/usr/local`, o caminho da instalação é passado ao CMake pela variável `ncnn_DIR`:

```bash
cmake -Dncnn_DIR=$HOME/ncnn/build/install/lib/cmake/ncnn ..
```

O modelo foi validado primeiro numa imagem estática. Nesse modo, o resultado anotado é salvo em `resultado.jpg`:

```bash
./yolo_raspicam --model ../model_ncnn_model --image ../teste.jpg
```

Em seguida, o programa foi executado com a câmera:

```bash
./yolo_raspicam --model ../model_ncnn_model
```

## Parâmetros

O comportamento do programa é configurado pelos parâmetros de linha de comando da Tabela 1.

*Tabela 1 — Parâmetros de linha de comando.*

| Parâmetro | Padrão | Função |
|---|---|---|
| `--model` | `model_ncnn_model` | pasta com os arquivos do modelo |
| `--source` | `libcamera` | fonte de vídeo: libcamera, webcam USB pelo número, arquivo ou pipeline GStreamer |
| `--width`, `--height`, `--fps` | 640, 480, 30 | resolução e taxa de quadros da câmera |
| `--image` | — | inferência numa única imagem |
| `--conf` | 0,35 | limiar de confiança |
| `--iou` | 0,7 | limiar do NMS |
| `--threads` | 4 | número de threads do ncnn |
| `--show` | desligado | janela com o vídeo anotado |
| `--save` | — | gravação do vídeo anotado em AVI ou MKV |
| `--stream` | — | envio do vídeo anotado pela rede |
| `--sink` | — | pipeline GStreamer próprio para o vídeo anotado |
| `--no-fps` | — | remove o FPS do quadro anotado |
| `--udp` | — | envio do JSON por UDP |
| `--quiet` | — | desliga a impressão do JSON no terminal |
| `--max-frames` | sem limite | encerramento após N quadros |

Os logs são escritos no stderr e o JSON no stdout. Quando o programa é encerrado com Ctrl+C, o arquivo de vídeo é fechado corretamente.

## Formato da saída

Cada quadro gera uma linha no formato abaixo. As coordenadas estão em pixels do quadro original, e o campo `stamp` registra o instante da captura em nanossegundos.

```json
[{"x1": 178, "y1": 52, "x2": 271, "y2": 177, "conf": 0.8249, "class": "face", "class_id": 0, "stamp": 1763900000123456789}]
```

## Resultados

A Figura 1 mostra a montagem utilizada no laboratório.

<img width="1200" height="1600" alt="WhatsApp Image 2026-11-23 at 15 27 36" src="https://github.com/user-attachments/assets/dbad1e12-de92-43b1-9f8a-9387cb1937b7" />

*Figura 1 — Montagem no laboratório.*

A Figura 2 mostra o resultado da inferência sobre a imagem de teste.

<img width="416" height="414" alt="face" src="https://github.com/user-attachments/assets/c86cd6d1-5bb7-456d-a1ec-fdf81123cb47" />

*Figura 2 — Inferência na imagem de teste.*

O vídeo a seguir mostra a inferência sobre as imagens capturadas pela raspicam.

https://github.com/user-attachments/assets/d8189263-e42e-46f9-8963-479523406aa7
