#include "VideoDecoder.hpp"

VideoDecoder::VideoDecoder(QObject *parent)
    : QThread(parent), m_keepRunning(true), m_width(0), m_height(0) {
    init();
}

VideoDecoder::~VideoDecoder() {
    m_keepRunning = false;
    m_wait.wakeAll();
    wait();
}

void VideoDecoder::pushNal(const QByteArray &nal) {
    QMutexLocker lock(&m_mutex);
    if (m_queue.size() >= m_maxQueueSize) {
        qWarning() << "Decoder queue full. Dropping frame";
        m_queue.dequeue();
    }
    m_queue.enqueue(nal);
    m_wait.wakeOne();
}

void VideoDecoder::run() {
    while (m_keepRunning) {
        QByteArray nal;
        {
            QMutexLocker lock(&m_mutex);
            while (m_queue.isEmpty() && m_keepRunning) {
                m_wait.wait(&m_mutex);
            }
            if (!m_keepRunning) break;
            nal = m_queue.dequeue();
        }
        decode(nal);
    }

    flush();

    while(!m_queue.empty()) {
        m_queue.dequeue();
    }

    sws_freeContext(m_swsCtx);
    av_frame_free(&m_frame);
    avcodec_free_context(&m_decCtx);
    if (m_hwDeviceCtx) av_buffer_unref(&m_hwDeviceCtx); 
}

void VideoDecoder::init() {
    av_log_set_level(AV_LOG_VERBOSE);

    const AVCodec *dec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!dec) {
        qWarning("Decoder not found");
        return;
    }

    m_decCtx = avcodec_alloc_context3(dec);

    if (findVAAPIPixelFormat(dec)) {
        int err = av_hwdevice_ctx_create(&m_hwDeviceCtx, AV_HWDEVICE_TYPE_VAAPI, nullptr, nullptr, 0);
        if (err < 0) {
            char errbuf[128];
            av_strerror(err, errbuf, sizeof(errbuf));
            qWarning("Failed to create VAAPI device context (%s), falling back to SW.", errbuf);
        } else {
            m_decCtx->hw_device_ctx = av_buffer_ref(m_hwDeviceCtx);
            m_decCtx->get_format = getHwFormat;
            m_decCtx->opaque = this;
            qInfo("VAAPI Hardware Decoding initialized.");
        }
    } else {
         qWarning("VAAPI not supported by H.264 decoder or config, using SW.");
    }

    if (avcodec_open2(m_decCtx, dec, nullptr) < 0) {
        qWarning("Cannot open decoder");
        avcodec_free_context(&m_decCtx);
        return;
    }

    m_frame = av_frame_alloc();
}

void VideoDecoder::decode(const QByteArray &nal) {
    if (nal.isEmpty())
        return;

    AVPacket *pkt = av_packet_alloc();
    pkt->data = (uint8_t *)nal.data();
    pkt->size = nal.size();

    if (avcodec_send_packet(m_decCtx, pkt) < 0) {
        av_packet_free(&pkt);
        return;
    }

    AVFrame *sw_frame = av_frame_alloc();
    if (!sw_frame) {
        qFatal("Failed to allocate SW frame.");
    }

    while (avcodec_receive_frame(m_decCtx, m_frame) == 0) {
        AVFrame *final_frame = m_frame;
        bool used_hw_frame = false;
        if (m_frame->format == m_hwPixFmt) {
            if (av_hwframe_transfer_data(sw_frame, m_frame, 0) < 0) {
                qWarning("Failed to transfer frame from GPU to CPU");
                av_frame_unref(m_frame);
                continue;
            }
            av_frame_copy_props(sw_frame, m_frame);

            if (sw_frame->format == AV_PIX_FMT_NONE) {
                sw_frame->format = AV_PIX_FMT_NV12;
            }
            final_frame = sw_frame;
            used_hw_frame = true;
        }

        // re-init sws if frame properties change
        if (m_swsCtx == nullptr || 
            final_frame->width != m_width || 
            final_frame->height != m_height || 
            (AVPixelFormat)final_frame->format != m_currentSwsSrcFmt) 
        {
            sws_freeContext(m_swsCtx);
            m_swsCtx = sws_getContext(
                final_frame->width, final_frame->height, (AVPixelFormat)final_frame->format,
                final_frame->width, final_frame->height, AV_PIX_FMT_RGB24,
                SWS_BILINEAR, nullptr, nullptr, nullptr
            );
            
            if (m_swsCtx) {
                m_width = final_frame->width;
                m_height = final_frame->height;
                m_currentSwsSrcFmt = (AVPixelFormat)final_frame->format;
            } else {
                qWarning("Failed to create SWS context.");
                av_frame_unref(m_frame);
                if (m_frame->format == m_hwPixFmt) {
                    av_frame_unref(sw_frame);
                    av_frame_free(&sw_frame);
                }
                av_packet_unref(pkt);
                av_packet_free(&pkt);
                return;
            }
        }

        // convert to rgb
        QImage img(final_frame->width, final_frame->height, QImage::Format_RGB888);
        uint8_t *dest[4] = { img.bits(), nullptr, nullptr, nullptr };
        int destLinesize[4] = { img.bytesPerLine(), 0, 0, 0 };

        sws_scale(m_swsCtx,
            final_frame->data, final_frame->linesize,
            0, final_frame->height,
            dest, destLinesize);

        av_frame_unref(m_frame);
        if (used_hw_frame) {
            av_frame_unref(sw_frame); 
        }

        emit frameReady(img.copy());
    }

    av_frame_free(&sw_frame);
    av_packet_unref(pkt);
    av_packet_free(&pkt);
}

void VideoDecoder::flush()
{
    if (!m_decCtx)
        return;

    avcodec_send_packet(m_decCtx, nullptr);
    AVFrame *frame = av_frame_alloc();
    while (true) {
        int ret = avcodec_receive_frame(m_decCtx, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            break;
    }
    av_frame_free(&frame);
}

enum AVPixelFormat VideoDecoder::getHwFormat(AVCodecContext *ctx, const enum AVPixelFormat *pix_fmts) {
    VideoDecoder *self = (VideoDecoder *)ctx->opaque;
    for (const enum AVPixelFormat *p = pix_fmts; *p != -1; ++p) {
        if (*p == self->m_hwPixFmt) {
            return *p;
        }
    }
    return avcodec_default_get_format(ctx, pix_fmts);
}

bool VideoDecoder::findVAAPIPixelFormat(const AVCodec *dec) {
    for (int i = 0;; i++) {
        const AVCodecHWConfig *config = avcodec_get_hw_config(dec, i);
        if (!config) break;

        if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX &&
            config->device_type == AV_HWDEVICE_TYPE_VAAPI) {
            
            m_hwPixFmt = config->pix_fmt;
            return true;
        }
    }
    return false;
}