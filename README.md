# CamStream

This project demonstrates server and client aplication for two camera streams using Qt5, WebSockets, FFmpeg for vide coding/decoding, OpenCV for image processing and Protocol Buffers for server commanding.

## Build and Run
Tested on Ubuntu 22.04.5 LTS (AMD) and Ubuntu 24.04.3 LTS (Intel)

### Prerequisities
Install libraries needed:
```bash
sudo apt update
sudo apt install build-essential cmake pkg-config \
  qtbase5-dev libqt5websockets5-dev libqt5network5 \
  libavformat-dev libavcodec-dev libavutil-dev libswscale-dev \
  protobuf-compiler libprotobuf-dev libprotobuf-c-dev libopencv-dev
```

### Build
```bash
git clone https://github.com/janpapaj/camstream.git
cd camstream
mkdir build && cd build
cmake ..
make
```

### Test
To run server at port 8443:
```bash
./server -c ../cert.pem -k ../key.pem -d ../output_day.mp4 -e ../output_heat.mp4 -p 8443
```

To run client:
```bash
 ./client -s localhost -p 8443
```

## Description and Structure
Code is divided to `client` part and `server` part with common protobuf definition.

### Client
- `Client.cpp` Implements the UI and ressebles the video processing pipeline like this:
```
WssClient->VideoDecoder->VideoWidget
```
- class `WssClient` Implements WSS client using QWebsocket for secure communication
- class `VideoDecoder` H.264 HW accelerated video decoder that uses FFmpeg and vaapi
- class `VideoWidget` QWidget class that implements display label and zoom control and shows status
  - Uses simple QLabel to render the streched video image
  - TODO: use OpenGL to accelerate stretching and rendering

### Server
- `Server.cpp` Implements Qt console application that instantiates two video stream and a single command stream. Pipeline for a single stream looks like this:
```
VideoReader->VideoProcessing->VideoEncoder->StreamServer
```
- class `VideoReader` Helper class that reads a video file, decodes the frames using HW accelerated codec in a thread and emits `cv::Mat` frames for further image processing
  - Decoder fills an output queue with frames up to a certain level and then waits for it's releave
- class `VideoProcessing` Class to perform image processing operations (zoom) using OpenCV
- class `VideoEncoder` Threaded class for HW accelerated frames encoding to H.264 NAL units.
  - Frames are first pushed into a queue buffer before being picked for processing.
  - Eventually, frames could be dropped if the processing of the pipeline isn't fast enough and the queue gets filled up
- class `StreamServer` Starts WSS server and provides API for IO operations on connected clients
   - Expects exactly 3 WSS client in total to connect at maximum and therefore could serve just one client application. The purpose is to simplify the implementation and prevent command collisions.

### Protobuf Message
Command message consists of `CameraChannel`, `CommandType` and `payload` (zoomLevel).

### Notes and Thoughts
- For the sake of simplicity, the `VideoReader` runs the stream even with no connected clients.
- Class `VideoDecoder` nearly duplicates `VideoReader`, which is just a helper class used for simulation and in real usecase it would be dropped.
- Server does not enforce mutual TLS to authorize the clients.
- The streaming device could have limited connectivity bandwith to handle multiple request for streaming. In that case a proxyserver should be setup to handle this.
- Restarting server causes client decoder corruption - artefacts in the decoded image. I beleave more effort would be needed to implement SPS/PPS frames tracking in the client's decoder.
- Video reader uses CPU decoder only - no GPU acceleration.
- For video resolutions non-dividible by 32 (`HEAT` Camera Channel) the codec align the image to 32 which results in artefact strip in the video.
- Much of the effort was spend on tuning the HW accelerated codec, because dual stream SW codec operations were too demanding even for a powerful CPU.