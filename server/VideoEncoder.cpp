#include "VideoEncoder.hpp"
#include <QDebug>

VideoEncoder::VideoEncoder(int width, int height, int fps, int bitrate, QObject* parent)
    : QThread(parent), m_width(width), m_height(height), m_fps(fps), m_bitrate(bitrate), m_keepRunning(true) {
    init();
}

VideoEncoder::~VideoEncoder() {
    if (m_swsCtx) {
        sws_freeContext(m_swsCtx);
        m_swsCtx = nullptr;
    }
    if (m_swFrame) av_frame_free(&m_swFrame);
    if (m_hwFrame) av_frame_free(&m_hwFrame);
    if (m_pkt) av_packet_free(&m_pkt);
    if (m_ctx) avcodec_free_context(&m_ctx);

    if (m_hwFramesRef) av_buffer_unref(&m_hwFramesRef);
    if (m_hwDeviceCtx) av_buffer_unref(&m_hwDeviceCtx);
}

void VideoEncoder::pushBitmap(const cv::Mat &mat) {
    QMutexLocker lock(&m_mutex);
    if (m_queue.size() >= m_maxQueueSize) {
        qWarning() << "Encoder queue full. Dropping frame";
        m_queue.dequeue();
    }
    m_queue.enqueue(mat);
    m_wait.wakeOne();
}

void VideoEncoder::run() {
    while (m_keepRunning) {
        cv::Mat mat;
        {
            QMutexLocker lock(&m_mutex);
            while (m_queue.isEmpty() && m_keepRunning) {
                m_wait.wait(&m_mutex);
            }

            if (!m_keepRunning) break;
            mat = m_queue.dequeue();
        }
        encode(mat);
    }

    flush();
}

void VideoEncoder::init() {
    av_log_set_level(AV_LOG_VERBOSE);

    // use h264 vaapi
    const AVCodec *codec = avcodec_find_encoder_by_name("h264_vaapi");
    if (!codec) {
        qFatal("h264_vaapi encoder not found");
    }

    m_ctx = avcodec_alloc_context3(codec);
    if (!m_ctx) qFatal("Failed to alloc codec context");

    m_ctx->width = m_width;
    m_ctx->height = m_height;
    m_ctx->time_base = {1, m_fps};
    m_ctx->framerate = {m_fps, 1};
    m_ctx->gop_size = 30;
    m_ctx->max_b_frames = 0;
    m_ctx->bit_rate = m_bitrate;

    m_ctx->pix_fmt = AV_PIX_FMT_VAAPI;

    int err = av_hwdevice_ctx_create(&m_hwDeviceCtx, AV_HWDEVICE_TYPE_VAAPI,
                                     "/dev/dri/renderD128", nullptr, 0);
    if (err < 0) {
        char buf[128];
        av_strerror(err, buf, sizeof(buf));
        qFatal("Failed to create VAAPI device: %s", buf);
    }
    m_ctx->hw_device_ctx = av_buffer_ref(m_hwDeviceCtx);

    m_hwFramesRef = av_hwframe_ctx_alloc(m_hwDeviceCtx);
    if (!m_hwFramesRef) {
        qFatal("Failed to allocate m_hwFramesRef");
    }

    m_hwFramesCtx = (AVHWFramesContext*)m_hwFramesRef->data;
    m_hwFramesCtx->format = AV_PIX_FMT_VAAPI;
    m_hwFramesCtx->sw_format = AV_PIX_FMT_NV12;
    m_hwFramesCtx->width  = m_width;
    m_hwFramesCtx->height = m_height;
    m_hwFramesCtx->initial_pool_size = 20;

    if (av_hwframe_ctx_init(m_hwFramesRef) < 0) {
        qFatal("Failed to initialize hw frames context");
    }

    m_ctx->hw_frames_ctx = av_buffer_ref(m_hwFramesRef);

    if (avcodec_open2(m_ctx, codec, nullptr) < 0) {
        qFatal("Failed to open VAAPI encoder");
    }

    m_swFrame = av_frame_alloc();
    if (!m_swFrame) qFatal("Failed to alloc m_swFrame");
    m_swFrame->format = m_hwFramesCtx->sw_format;
    m_swFrame->width  = m_width;
    m_swFrame->height = m_height;
    if (av_frame_get_buffer(m_swFrame, 32) < 0) {
        qFatal("Failed to allocate m_swFrame buffers");
    }

    m_swsCtx = sws_getContext(m_width, m_height, AV_PIX_FMT_BGR24,
                              m_width, m_height, m_hwFramesCtx->sw_format,
                              SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!m_swsCtx) {
        qFatal("Failed to create sws context");
    }

    m_hwFrame = av_frame_alloc();
    if (!m_hwFrame) qFatal("Failed to alloc m_hwFrame");

    m_pkt = av_packet_alloc();
    if (!m_pkt) qFatal("Failed to alloc packet");
}

void VideoEncoder::encode(const cv::Mat &mat) {
    if (mat.empty()) {
        qWarning("Empty input mat");
        return;
    }
    if (mat.cols != m_width || mat.rows != m_height) {
        qWarning("Input mat size mismatch: got %dx%d expected %dx%d", mat.cols, mat.rows, m_width, m_height);
        return;
    }
    if (mat.channels() != 3) {
        qWarning("Input mat must be 3-channel BGR");
        return;
    }

    // rgb to nv12
    const uint8_t* srcSlice[1] = { mat.data };
    int srcStride[1] = { static_cast<int>(mat.step) };

    int out_h = sws_scale(m_swsCtx, srcSlice, srcStride, 0, m_height,
                         m_swFrame->data, m_swFrame->linesize);
    if (out_h != m_height) {
        qWarning("sws_scale returned %d, expected %d", out_h, m_height);
        return;
    }

    m_swFrame->pts = m_pts++;

    if (av_hwframe_get_buffer(m_hwFramesRef, m_hwFrame, 0) < 0) {
        qWarning("av_hwframe_get_buffer failed");
        return;
    }

    if (av_hwframe_transfer_data(m_hwFrame, m_swFrame, 0) < 0) {
        qWarning("av_hwframe_transfer_data failed");
        av_frame_unref(m_hwFrame);
        return;
    }

    m_hwFrame->pts = m_swFrame->pts;

    int ret = avcodec_send_frame(m_ctx, m_hwFrame);
    if (ret < 0) {
        char errbuf[128];
        av_strerror(ret, errbuf, sizeof(errbuf));
        qWarning("avcodec_send_frame failed: %s", errbuf);
        av_frame_unref(m_hwFrame);
        return;
    }

    av_frame_unref(m_hwFrame);

    while (true) {
        ret = avcodec_receive_packet(m_ctx, m_pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            break;
        if (ret < 0) {
            char errbuf[128];
            av_strerror(ret, errbuf, sizeof(errbuf));
            qWarning("avcodec_receive_packet failed: %s", errbuf);
            return;
        }

        QByteArray out;
        out.append(reinterpret_cast<char*>(m_pkt->data), m_pkt->size);
        av_packet_unref(m_pkt);
        emit frameReady(out);
    }
}

void VideoEncoder::flush() {
    int ret = avcodec_send_frame(m_ctx, nullptr);
    if (ret < 0) {
        char errbuf[128];
        av_strerror(ret, errbuf, sizeof(errbuf));
        qWarning("flush: avcodec_send_frame(NULL) failed: %s", errbuf);
        return;
    }

    while (true) {
        ret = avcodec_receive_packet(m_ctx, m_pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            break;
        if (ret < 0) {
            char errbuf[128];
            av_strerror(ret, errbuf, sizeof(errbuf));
            qWarning("flush: avcodec_receive_packet failed: %s", errbuf);
            return;
        }
        av_packet_unref(m_pkt);
    }
}
