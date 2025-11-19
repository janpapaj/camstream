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

class VideoDecoder : public QThread {
    Q_OBJECT

public:
    explicit VideoDecoder(QObject *parent = nullptr);
    ~VideoDecoder();

public slots:
    void pushNal(const QByteArray &nal);

signals:
    void frameReady(const QImage &img);

protected:
    void run() override;

private:
    void init();
    void decode(const QByteArray &nal);
    void flush();

    // hw accel
    bool findVAAPIPixelFormat(const AVCodec *dec);
    static enum AVPixelFormat getHwFormat(AVCodecContext *ctx, const enum AVPixelFormat *pix_fmts);
    AVBufferRef *m_hwDeviceCtx = nullptr;
    enum AVPixelFormat m_hwPixFmt = AV_PIX_FMT_NONE;
    AVPixelFormat m_currentSwsSrcFmt = AV_PIX_FMT_NONE;

    bool m_keepRunning;
    QMutex m_mutex;
    QWaitCondition m_wait;
    QQueue<QByteArray> m_queue;
    const int m_maxQueueSize = 50;
    
    int m_width, m_height;

    AVCodecContext *m_decCtx = nullptr;
    SwsContext *m_swsCtx = nullptr;

    int m_videoStreamIndex = -1;
    AVFrame *m_frame = nullptr;
};