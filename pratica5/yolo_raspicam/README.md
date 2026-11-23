# yolo_raspicam — YOLO11n (ncnn) na Raspberry Pi 3 com a raspicam, em C++ (sem OpenCV)

Versão em C++ (sem ROS, sem Python e sem OpenCV) do `vision_detector_node`:

- **captura** da raspicam pela pilha libcamera, com o GStreamer (`libcamerasrc → appsink`);
- **inferência** do modelo exportado com `format="ncnn"` (entrada fixa 320x320, saída `5 x 2100`);
- **uma linha JSON por quadro** no stdout (e/ou por UDP), no mesmo formato que o nó publicava em
  `/vision/yolo/detections`; lista vazia também é emitida;
- **vídeo anotado opcional** também pelo GStreamer (`appsrc`): janela (`--show`), arquivo (`--save`),
  stream pela rede (`--stream`) ou um pipeline seu (`--sink`). O frame só é desenhado se houver
  alguma dessas saídas, como no nó Python.

Do GStreamer só é usado o **núcleo** (`libgstreamer1.0-dev`): o `appsink`/`appsrc` são acessados
pelos sinais do GObject, então não é preciso o `libgstreamer-plugins-base1.0-dev`. As caixas e os
textos são desenhados pelo próprio programa (fonte bitmap embutida), sem biblioteca gráfica.

```
yolo_raspicam/
├── CMakeLists.txt
├── model_ncnn_model/      # model.ncnn.param, model.ncnn.bin, metadata.yaml (do export)
├── src/
│   ├── main.cpp           # argumentos, laço principal, JSON, UDP, saídas de vídeo
│   ├── gst_io.hpp/.cpp    # GstCapture (appsink) e GstOutput (appsrc)
│   ├── yolo_ncnn.hpp/.cpp # classe YoloNcnn (letterbox + ncnn)
│   ├── postprocess.hpp    # letterbox, decodificação, NMS, leitura do metadata.yaml
│   ├── draw.hpp           # caixas, rótulos e FPS em RGB
│   └── font.hpp           # fonte bitmap (gerada por tools/gen_font.py)
└── tools/gen_font.py      # só para regerar a fonte em outro tamanho (roda no PC, usa OpenCV-Python)
```

---

## 1. Sistema

- **Raspberry Pi OS 64-bit** (Bookworm ou Trixie). O ncnn é bem mais rápido em aarch64 do que no
  sistema 32-bit. Com 1 GB de RAM, a versão **Lite** (sem desktop) deixa mais memória livre.
- Câmera oficial (v1/v2/v3/HQ) no conector CSI. Teste antes de tudo:

```bash
rpicam-hello --list-cameras        # nos sistemas antigos: libcamera-hello --list-cameras
```

## 2. Pacotes do apt

```bash
sudo apt update
# obrigatórios
sudo apt install -y build-essential cmake git pkg-config \
    libgstreamer1.0-dev gstreamer1.0-libcamera gstreamer1.0-plugins-base
# opcionais: gravação, stream, modo foto, webcam USB e gst-launch para testes
sudo apt install -y gstreamer1.0-plugins-good gstreamer1.0-tools
```

| Pacote | Para quê |
|---|---|
| `build-essential`, `cmake`, `git`, `pkg-config` | compilar o ncnn e este projeto (o OpenMP do ncnn vem com o gcc) |
| `libgstreamer1.0-dev` | cabeçalhos do núcleo do GStreamer (só para compilar) |
| `gstreamer1.0-libcamera` | elemento `libcamerasrc`, que entrega os quadros da raspicam |
| `gstreamer1.0-plugins-base` | `videoconvert`, `appsink`, `appsrc`, `decodebin` |
| `gstreamer1.0-plugins-good` *(opcional)* | `jpegenc`, `avimux`, `matroskamux`, `rtpjpegpay`, `udpsink`, `v4l2src`, `jpegdec`/`pngdec`, `autovideosink` — usados por `--save`, `--stream`, `--image`, `--source 0` e `--show` |
| `gstreamer1.0-tools` *(opcional)* | `gst-launch-1.0`, para testar pipelines na mão |

Para a janela (`--show`), além do `plugins-good`, é preciso algum sink de vídeo do desktop
(`gstreamer1.0-gl` ou `gstreamer1.0-x`); na Pi sem monitor, prefira `--stream`.

Teste o `libcamerasrc` (Ctrl+C para parar):

```bash
gst-launch-1.0 libcamerasrc ! video/x-raw,width=640,height=480 ! videoconvert ! fakesink -v
```

## 3. ncnn (compilar da fonte)

Use uma versão recente: o modelo foi exportado pelo pnnx de um Ultralytics novo e usa camadas
(MatMul/atenção do bloco C2PSA do YOLO11) que versões antigas podem não carregar.

```bash
cd ~
git clone --depth 1 https://github.com/Tencent/ncnn.git
cd ncnn && mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=/usr/local \
      -DNCNN_VULKAN=OFF \
      -DNCNN_BUILD_EXAMPLES=OFF -DNCNN_BUILD_TOOLS=OFF \
      -DNCNN_BUILD_BENCHMARK=OFF -DNCNN_BUILD_TESTS=OFF ..
make -j2          # -j2 (ou -j1) para não estourar o 1 GB de RAM da Pi 3
sudo make install
```

- Sistema **32-bit**: acrescente `-DCMAKE_TOOLCHAIN_FILE=../toolchains/pi3.toolchain.cmake` ao `cmake`.
- Sem o `-DCMAKE_INSTALL_PREFIX`, o ncnn instala em `build/install`; nesse caso, compile este projeto
  com `cmake -Dncnn_DIR=$HOME/ncnn/build/install/lib/cmake/ncnn ..`.
- Depois de instalado, a pasta `~/ncnn` pode ser apagada (o ncnn é ligado estaticamente).

## 4. Compilar e rodar

```bash
cd yolo_raspicam
mkdir build && cd build
cmake ..
make -j2

# 1) teste o modelo numa foto (gera resultado.jpg e imprime o JSON)
./yolo_raspicam --model ../model_ncnn_model --image ../teste.jpg

# 2) tempo real com a raspicam
./yolo_raspicam --model ../model_ncnn_model
```

### Opções

| Opção | Padrão | Descrição |
|---|---|---|
| `--model DIR` | `model_ncnn_model` | pasta do export ncnn |
| `--source S` | `libcamera` | `libcamera`, índice V4L2 (`0` = `/dev/video0`, webcam USB), arquivo de vídeo, ou o início de um pipeline GStreamer (o programa completa com `videoconvert ! appsink`) |
| `--width/--height/--fps` | `640/480/30` | resolução pedida à câmera |
| `--image FOTO` | — | roda numa imagem só e salva o resultado (`--save`, padrão `resultado.jpg`; aceita `.png`) |
| `--conf` | `0.35` | limiar de confiança (igual ao nó Python) |
| `--iou` | `0.7` | limiar do NMS (padrão do Ultralytics, que o nó Python usava implicitamente) |
| `--threads` | `4` | threads do ncnn |
| `--show` | — | janela com o vídeo anotado (precisa de display) |
| `--save ARQ` | — | grava o vídeo anotado em `.avi` ou `.mkv` (MJPEG) |
| `--stream IP:PORTA` | — | envia o vídeo anotado por RTP/JPEG via UDP |
| `--sink "PIPE"` | — | saída GStreamer sua, que recebe RGB (ex.: `"videoconvert ! glimagesink"`) |
| `--no-fps` | — | não desenha o FPS no frame |
| `--udp IP:PORTA` | — | envia o JSON de cada quadro por UDP |
| `--quiet` | — | não imprime o JSON no stdout |
| `--max-frames N` | `0` | para depois de N quadros (benchmark) |

Opções do próprio GStreamer também funcionam, por exemplo `--gst-debug=2` para ver avisos.
Logs (FPS, tempo médio de inferência) vão para o **stderr**; o **stdout** fica só com o JSON.

As saídas de vídeo ao vivo abrem depois dos 10 primeiros quadros, para o arquivo usar o FPS real do
detector. Encerre com **Ctrl+C**: o programa envia fim de stream e o `.avi`/`.mkv` fica fechado direito.

## 5. Formato da saída

Uma linha por quadro, igual à mensagem `std_msgs/String` do nó Python:

```json
[{"x1": 178, "y1": 52, "x2": 271, "y2": 177, "conf": 0.8249, "class": "face", "class_id": 0, "stamp": 1763900000123456789}]
```

`stamp` é o instante da **captura** em nanossegundos, no relógio do sistema (a mesma base de tempo
do ROS). Ele vem do timestamp do quadro (PTS), que o `libcamerasrc` preenche a partir do timestamp
do sensor; assim a latência da câmera e da inferência não entra no carimbo.

Para receber no PC:

```bash
./yolo_raspicam --model ../model_ncnn_model --quiet --udp 192.168.4.2:5005   # na Pi
nc -ul 5005                                                                  # no PC
```

## 6. Ver o vídeo com a Pi sem monitor

```bash
# na Pi
./yolo_raspicam --model ../model_ncnn_model --stream IP_DO_PC:5000

# no PC
gst-launch-1.0 udpsrc port=5000 \
  caps="application/x-rtp,media=video,encoding-name=JPEG,payload=26,clock-rate=90000" \
  ! rtpjpegdepay ! jpegdec ! videoconvert ! autovideosink sync=false
```

## 7. Desempenho na Pi 3

- A inferência domina o tempo; meça com `--quiet --max-frames 100` e olhe a "inferência média".
- Use o sistema 64-bit e um dissipador: a Pi 3 reduz o clock quando esquenta
  (`vcgencmd measure_temp`, `vcgencmd get_throttled`).
- Deixe as saídas de vídeo desligadas quando não precisar: desenhar e codificar JPEG também custam CPU.
- `--width 320 --height 240` reduz a conversão e o resize (a rede vê 320x320 de qualquer jeito),
  mas as coordenadas passam a ser nessa resolução.
- Para mais FPS, reexporte com entrada menor (múltiplo de 32) — o código lê `imgsz` do
  `metadata.yaml` e se adapta sem mudanças: `yolo export model=model.pt format=ncnn imgsz=256 half=True`.
  Em troca, rostos pequenos/distantes ficam mais difíceis de detectar.

## 8. Problemas comuns

| Sintoma | Causa provável |
|---|---|
| `no element "libcamerasrc"` | falta o `gstreamer1.0-libcamera` |
| `no element "jpegenc"` (ou `avimux`, `udpsink`…) | falta o `gstreamer1.0-plugins-good` |
| Não abre a fonte / nenhum quadro em 5 s | câmera não detectada (`rpicam-hello --list-cameras`) ou resolução/fps não suportados; teste outro `--width/--height/--fps` |
| Janela não abre (`--show`) | sem sink de vídeo no desktop (instale `gstreamer1.0-gl`) ou sem display; use `--stream` |
| `falha ao carregar model.ncnn.param` | caminho errado em `--model` ou ncnn antigo demais |
| `find_package(ncnn)` falha | ncnn instalado fora do `/usr/local`; use `-Dncnn_DIR=...` |
| Nenhuma detecção | confira primeiro com `--image` numa foto em que o modelo do PC detecta |
