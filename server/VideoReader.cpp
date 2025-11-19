#include "VideoReader.hpp"

VideoReader::VideoReader(const QString &filePath, QObject *parent)
    : QThread(parent), m_path(filePath), m_width(0), m_height(0) {

    m_initialized = init();

    connect(&m_timer, &QTimer::timeout, this, &VideoReader::publishFrame);
    m_timer.start(40);   // 25 FPS
}

VideoReader::~VideoReader() {
    stop();
    wait();

    if(m_swsCtx) {
        sws_freeContext(m_swsCtx);
        m_swsCtx = nullptr;
    }
}

void VideoReader::stop() {
    m_running = false;
    m_notFull.wakeOne();
}
    

void VideoReader::run() {
    if (m_initialized) {
        m_running = true;
        decoderLoop();
    }
}

void VideoReader::publishFrame() {
    QMutexLocker locker(&m_mutex);

    // queue is missing frame, fire sooner
    if (m_queue.isEmpty()) {
        m_timer.setInterval(5);
        return;
    }

    cv::Mat frame = m_queue.dequeue();
    m_notFull.wakeOne();

    m_timer.setInterval(40);
    emit frameReady(frame);
}

bool VideoReader::init() {
    av_log_set_level(AV_LOG_VERBOSE);

    if (avformat_open_input(&m_fmtCtx, m_path.toUtf8().constData(), nullptr, nullptr) < 0) {
        qWarning("Cannot open file %s", m_path.toUtf8().constData());
        return false;
    }

    if (avformat_find_stream_info(m_fmtCtx, nullptr) < 0) {
        qWarning("Cannot find stream info");
        avformat_close_input(&m_fmtCtx);
        return false;
    }

    for (unsigned i = 0; i < m_fmtCtx->nb_streams; i++) {
        if (m_fmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            m_videoStreamIndex = i;
            break;
        }
    }
    if (m_videoStreamIndex < 0) {
        qWarning("No video stream found");
        avformat_close_input(&m_fmtCtx);
        return false;
    }

    const AVCodec *dec = avcodec_find_decoder(m_fmtCtx->streams[m_videoStreamIndex]->codecpar->codec_id);
    if (!dec) {
        qWarning("Decoder not found");
        return false;
    }

    m_decCtx = avcodec_alloc_context3(dec);
    avcodec_parameters_to_context(m_decCtx, m_fmtCtx->streams[m_videoStreamIndex]->codecpar);

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
        avformat_close_input(&m_fmtCtx);
        return false;
    }

    m_width = m_decCtx->width;
    m_height = m_decCtx->height;

    m_frame = av_frame_alloc();
    m_pkt = av_packet_alloc();

    AVPixelFormat sws_src_format = m_decCtx->pix_fmt; 

    if (m_hwPixFmt != AV_PIX_FMT_NONE) {
        sws_src_format = AV_PIX_FMT_NV12; 
    }


    m_swsCtx = sws_getContext(m_decCtx->width, m_decCtx->height, sws_src_format,
                            m_decCtx->width, m_decCtx->height, AV_PIX_FMT_BGR24,
                            SWS_BILINEAR, nullptr, nullptr, nullptr);

    if (m_swsCtx) {
        m_currentSwsSrcFmt = sws_src_format;
    }
    return true;
}

void VideoReader::decoderLoop() {
    AVFrame *sw_frame = av_frame_alloc();
    if (!sw_frame) {
        qFatal("Failed to allocate SW frame.");
    }
    
    while (1) {
        {
            QMutexLocker locker(&m_mutex);
            if (!m_running)
                break;

            // queue full, wait for pop
            while (m_queue.size() >= m_maxQueueSize && m_running) {
                m_notFull.wait(&m_mutex);
            }

            if (!m_running)
                break;
        }

        if (av_read_frame(m_fmtCtx, m_pkt) < 0) {
            av_seek_frame(m_fmtCtx, m_videoStreamIndex, 0, AVSEEK_FLAG_BACKWARD);
            continue;
        }

        if (m_pkt->stream_index == m_videoStreamIndex) {
            avcodec_send_packet(m_decCtx, m_pkt);

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
                        final_frame->width, final_frame->height, AV_PIX_FMT_BGR24,
                        SWS_BILINEAR, nullptr, nullptr, nullptr
                    );
                    
                    if (m_swsCtx) {
                        m_width = final_frame->width;
                        m_height = final_frame->height;
                        m_currentSwsSrcFmt = (AVPixelFormat)final_frame->format;
                    } else {
                        qWarning("Failed to create SWS context.");
                        av_frame_unref(m_frame);
                        if (m_frame->format == m_hwPixFmt) av_frame_unref(sw_frame);
                        continue;
                    }
                }

                cv::Mat mat(final_frame->height, final_frame->width, CV_8UC3);

                uint8_t* dest[4] = { mat.data, nullptr, nullptr, nullptr };
                int dest_linesize[4] = { (int) mat.step, 0, 0, 0 };

                // yuv to rgb
                sws_scale(
                    m_swsCtx,
                    final_frame->data, final_frame->linesize,
                    0, final_frame->height,
                    dest, dest_linesize
                );

                {
                    QMutexLocker locker(&m_mutex);
                    m_queue.enqueue(mat.clone());
                }

                av_frame_unref(m_frame);
                if (used_hw_frame) {
                    av_frame_unref(sw_frame); 
                }
            }
        }

        av_packet_unref(m_pkt);
    }

    // flush decoder
    avcodec_send_packet(m_decCtx, NULL);
    while (avcodec_receive_frame(m_decCtx, m_frame) == 0) {
        av_frame_unref(m_frame); 
    }

    while(!m_queue.empty()) {
        m_queue.dequeue();
    }

    sws_freeContext(m_swsCtx);
    av_frame_free(&m_frame);
    av_frame_free(&sw_frame);
    av_packet_free(&m_pkt);
    avcodec_free_context(&m_decCtx);
    if (m_hwDeviceCtx) av_buffer_unref(&m_hwDeviceCtx); 
    avformat_close_input(&m_fmtCtx);
}

enum AVPixelFormat VideoReader::getHwFormat(AVCodecContext *ctx, const enum AVPixelFormat *pix_fmts) {
    VideoReader *self = (VideoReader *)ctx->opaque;
    for (const enum AVPixelFormat *p = pix_fmts; *p != -1; ++p) {
        if (*p == self->m_hwPixFmt) {
            return *p;
        }
    }
    return avcodec_default_get_format(ctx, pix_fmts);
}

bool VideoReader::findVAAPIPixelFormat(const AVCodec *dec) {
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