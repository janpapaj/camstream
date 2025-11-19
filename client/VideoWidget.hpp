#pragma once

#include <QWidget>
#include <QLabel>
#include <QSlider>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QImage>

#include "cmd.pb.h"

class VideoWidget : public QWidget
{
    Q_OBJECT
public:
    explicit VideoWidget(enum camera::CameraChan chan, QWidget *parent = nullptr);

signals:
    void zoomChanged(int zoomLevel);

public slots:
    void updateFrame(const QImage &frame);
    void updateStatus(bool connected);

private:
    QString chanToName(enum camera::CameraChan chan);
    
    QLabel *m_nameLabel;
    QLabel *m_videoLabel;
    QLabel *m_statusLabel;
    QSlider *m_zoomSlider;
    QLabel *m_zoomLabel;

    enum camera::CameraChan m_chan;
};
