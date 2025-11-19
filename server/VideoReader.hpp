#pragma once

#include <QObject>
#include <QImage>
#include <QThread>
#include <QTimer>
#include <QQueue>
#include <QMutex>
#include <QWaitCondition>
#include <QByteArray>
#include <QDebug>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

#include <opencv2/opencv.hpp>

class VideoReader : public QThread
{
    Q_OBJECT
public:
    explicit VideoReader(const QString &filePath, QObject *parent = nullptr);
    ~VideoReader();

    int getFrameWidth() { return m_width; }
    int getFrameHeight() { return m_height; }

    void stop();
    

protected:
    void run() override;

signals:
    void frameReady(const cv::Mat &img);

private:
    bool init();
    void decoderLoop();
    void publishFrame();

    // hw accel
    bool findVAAPIPixelFormat(const AVCodec *dec);
    static enum AVPixelFormat getHwFormat(AVCodecContext *ctx, const enum AVPixelFormat *pix_fmts);
    AVBufferRef *m_hwDeviceCtx = nullptr;
    enum AVPixelFormat m_hwPixFmt = AV_PIX_FMT_NONE;
    AVPixelFormat m_currentSwsSrcFmt = AV_PIX_FMT_NONE;

    bool m_initialized;
    QString m_path;
    bool m_running = false;
    QTimer m_timer;

    int m_width, m_height;

    const int m_maxQueueSize = 8;
    QQueue<cv::Mat> m_queue;
    QMutex m_mutex;
    QWaitCondition m_notFull;

    
    AVFormatContext *m_fmtCtx = nullptr;
    AVCodecContext *m_decCtx = nullptr;
    SwsContext *m_swsCtx = nullptr;

    int m_videoStreamIndex = -1;
    AVFrame *m_frame = nullptr;
    AVPacket *m_pkt = nullptr;
};
