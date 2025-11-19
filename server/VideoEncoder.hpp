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

class VideoEncoder : public QThread {
    Q_OBJECT
public:
    explicit VideoEncoder(int width, int height, int fps, int bitrate, QObject *parent = nullptr);
    ~VideoEncoder();

public slots:
    void pushBitmap(const cv::Mat &mat);

signals:
    void frameReady(const QByteArray &out);

protected:
    void run() override;

private:
    void init();
    void encode(const cv::Mat &mat);
    void flush();

    bool m_keepRunning;
    QMutex m_mutex;
    QWaitCondition m_wait;
    QQueue<cv::Mat> m_queue;
    const int m_maxQueueSize = 50;

    int m_width, m_height, m_fps, m_bitrate;
    int64_t m_pts = 0;

    AVBufferRef *m_hwDeviceCtx = nullptr;
    AVBufferRef *m_hwFramesRef = nullptr;
    AVHWFramesContext *m_hwFramesCtx = nullptr;

    AVFrame *m_swFrame = nullptr;
    AVFrame *m_hwFrame = nullptr;

    AVCodecContext *m_ctx = nullptr;
    AVPacket *m_pkt = nullptr;
    SwsContext *m_swsCtx = nullptr;
};