#pragma once
#include <QObject>
#include <opencv2/opencv.hpp>

class VideoProcessing : public QObject
{
    Q_OBJECT

public:
    explicit VideoProcessing(QObject *parent = nullptr)
        : QObject(parent) {}

public slots:
    void zoomChanged(int value) {
        if (value > 4) {
            m_zoomLevel = 4;
        } else if (value < 1) {
            m_zoomLevel = 1;
        } else {
            m_zoomLevel = value;
        }
    }

    void zoom(const cv::Mat &frame)
    {
        if (frame.empty() || m_zoomLevel <= 1)
            emit frameZoomed(frame);

        int newWidth = frame.cols / m_zoomLevel;
        int newHeight = frame.rows / m_zoomLevel;

        int offsetX = (frame.cols  - newWidth) / 2;
        int offsetY = (frame.rows - newHeight) / 2;

        cv::Rect rect(offsetX, offsetY, newWidth, newHeight);
        cv::Mat cropped = frame(rect);

        cv::Mat zoomed;
        cv::resize(cropped, zoomed, frame.size(), 0, 0, cv::INTER_LINEAR);

        emit frameZoomed(zoomed);
    }

signals:
    void frameZoomed(const cv::Mat &frame);

private:
    int m_zoomLevel = 1;
};
