#include <QApplication>
#include <QObject>
#include <QWidget>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QFrame>
#include <QPushButton>
#include <QSlider>
#include <QCommandLineParser>

#include "WssClient.hpp"
#include "VideoDecoder.hpp"
#include "VideoWidget.hpp"

#include "cmd.pb.h"

class MainWindow : public QWidget
{
    Q_OBJECT
public:
    MainWindow(QString host, int port, QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setWindowTitle("CamStream Client");

        videoWidgetDay = new VideoWidget(camera::CAMERA_DAY, this);
        videoWidgetDay->setMinimumSize(640, 360);

        videoWidgetHeat = new VideoWidget(camera::CAMERA_HEAT, this);
        videoWidgetHeat->setMinimumSize(640, 360);

        auto *layout = new QHBoxLayout();
        layout->addWidget(videoWidgetDay);
        layout->addWidget(videoWidgetHeat);
        setLayout(layout);

        wssClientCmd = new WssClient(QUrl(QString("wss://%1:%2/ws/ws_cmd").arg(host).arg(port)));

        // create streaming pipeline like this:
        // WssClient->VideoDecoder->VideoWidget
        wssClientVideoDay = new WssClient(QUrl(QString("wss://%1:%2/ws/ws_rec_video_day").arg(host).arg(port)));
        videoDecoderDay = new VideoDecoder();
        connect(wssClientVideoDay, &WssClient::newData, videoDecoderDay, &VideoDecoder::pushNal);
        connect(videoDecoderDay, &VideoDecoder::frameReady, videoWidgetDay, &VideoWidget::updateFrame);
        connect(wssClientVideoDay, &WssClient::disconnected, this, [this]() {videoWidgetDay->updateStatus(false);});
        connect(wssClientVideoDay, &WssClient::connected, this, [this]() {videoWidgetDay->updateStatus(true);});
        connect(videoWidgetDay, &VideoWidget::zoomChanged,
                this, [this](int value) {
                    QByteArray pkt = buildCmdPacket(camera::CAMERA_DAY, camera::CMD_ZOOM, value);
                    wssClientCmd->sendData(pkt);
                });
        videoDecoderDay->start();

        wssClientVideoHeat = new WssClient(QUrl(QString("wss://%1:%2/ws/ws_rec_video_heat").arg(host).arg(port)));
        videoDecoderHeat = new VideoDecoder();
        connect(wssClientVideoHeat, &WssClient::newData, videoDecoderHeat, &VideoDecoder::pushNal);
        connect(videoDecoderHeat, &VideoDecoder::frameReady, videoWidgetHeat, &VideoWidget::updateFrame);
        connect(wssClientVideoHeat, &WssClient::disconnected, this, [this]() {videoWidgetHeat->updateStatus(false);});
        connect(wssClientVideoHeat, &WssClient::connected, this, [this]() {videoWidgetHeat->updateStatus(true);});
        connect(videoWidgetHeat, &VideoWidget::zoomChanged,
                this, [this](int value) {
                    QByteArray pkt = buildCmdPacket(camera::CAMERA_HEAT, camera::CMD_ZOOM, value);
                    wssClientCmd->sendData(pkt);
                });
        videoDecoderHeat->start();
    }

    ~MainWindow() {
        delete wssClientVideoDay;
        delete videoDecoderDay;
        delete wssClientVideoHeat;
        delete videoDecoderHeat;
        delete wssClientCmd;
    }


private:
    // TODO make val to be generic using std::variant
    QByteArray buildCmdPacket(enum camera::CameraChan chan,
                                          enum camera::CommandType type,
                                          int val) {
        camera::CommandMessage cmd;
        cmd.set_chan(chan);
        cmd.set_type(type);

        auto* zoom = new camera::ZoomCommand();
        zoom->set_level(val);
        cmd.set_allocated_zoom(zoom);
    
        std::string bin;
        cmd.SerializeToString(&bin);

        // packet wraps command with 4B length prefix
        QByteArray packet;
        QDataStream out(&packet, QIODevice::WriteOnly);
        out.setByteOrder(QDataStream::BigEndian);
        out << static_cast<quint32>(bin.size());
        packet.append(bin.data(), bin.size());

        return packet;
    }

    WssClient *wssClientCmd;
    WssClient *wssClientVideoDay;
    WssClient *wssClientVideoHeat;

    VideoWidget *videoWidgetDay;
    VideoWidget *videoWidgetHeat;

    VideoDecoder *videoDecoderDay;
    VideoDecoder *videoDecoderHeat;
};

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCommandLineParser parser;

    parser.setApplicationDescription("Camstream client");
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption hostOption(
        {"s", "server"},
        "Server host address.",
        "host",
        "127.0.0.1"
    );

    QCommandLineOption portOption(
        {"p", "port"},
        "Server port.",
        "port",
        "8443"
    );

    parser.addOption(hostOption);
    parser.addOption(portOption);

    parser.process(app);

    QString host = parser.value(hostOption);
    int port = parser.value(portOption).toInt();

    MainWindow w(host, port);
    w.resize(1000, 500);
    w.show();

    return app.exec();
}

#include "Client.moc"