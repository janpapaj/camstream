#include <QPixmap>
#include <QDebug>

#include "VideoWidget.hpp"

VideoWidget::VideoWidget(enum camera::CameraChan chan, QWidget *parent)
    : QWidget(parent), m_chan(chan) {
    m_nameLabel = new QLabel(QString("Channel: ") + chanToName(chan));
    m_nameLabel->setAlignment(Qt::AlignLeft);

    m_videoLabel = new QLabel();
    m_videoLabel->setAlignment(Qt::AlignCenter);
    m_videoLabel->setMinimumSize(320, 240);
    m_videoLabel->setStyleSheet("background:black;");

    m_zoomSlider = new QSlider(Qt::Horizontal);
    m_zoomSlider->setRange(1, 4);
    m_zoomSlider->setValue(1);

    m_statusLabel = new QLabel("Status: Disconnected");
    m_statusLabel->setAlignment(Qt::AlignLeft);

    m_zoomLabel = new QLabel("1x");
    m_zoomLabel->setAlignment(Qt::AlignLeft);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(m_nameLabel);
    layout->addWidget(m_statusLabel);
    layout->addWidget(m_videoLabel, 1);
    layout->addWidget(m_zoomSlider);
    layout->addWidget(m_zoomLabel);

    connect(m_zoomSlider, &QSlider::valueChanged,
            this, [this](int value) {
                m_zoomLabel->setText(QString::number(value) + "x"); 
                emit zoomChanged(value);
            });
}

void VideoWidget::updateFrame(const QImage &frame) {
    m_videoLabel->setPixmap(
        QPixmap::fromImage(frame).scaled(
            m_videoLabel->size(),
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation
        )
    );
}

void VideoWidget::updateStatus(bool connected) {
    m_statusLabel->setText(connected ? "Status: Connected" : "Status: Disconnected");
}

QString VideoWidget::chanToName(enum camera::CameraChan chan) {
    switch (chan) {
        case camera::CAMERA_HEAT:
            return "Heat";
        case camera::CAMERA_DAY:
            return "Day";
        case camera::CAMERA_UNKNOWN:
        default:
            return "Unknown";
    }
}
