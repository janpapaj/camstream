#include <QApplication>
#include <QCommandLineParser>

#include "VideoReader.hpp"
#include "VideoEncoder.hpp"
#include "StreamServer.hpp"
#include "VideoProcessing.hpp"

class ServerApp : public QObject 
{
    Q_OBJECT
public:
    ServerApp(QString certPath, QString keyPath, QString videoDayPath, QString videoHeatPath, int port, QObject *parent = nullptr)
        : QObject(parent)
    {
        streamServer = new StreamServer(certPath, keyPath, port);

        // create pipeline for each camera channel:
        // VideoReader->VideoProcessing->VideoEncoder->StreamServer
        videoReaderDayChan = new VideoReader(videoDayPath, this);
        videoProcDayChan = new VideoProcessing();
        videoEncDayChan = new VideoEncoder(videoReaderDayChan->getFrameWidth(),
                                           videoReaderDayChan->getFrameHeight(),
                                           25, 2000000); // 25 fps, 2Mbps
        connect(streamServer, &StreamServer::zoomChangedDayChan, videoProcDayChan, &VideoProcessing::zoomChanged);
        connect(videoReaderDayChan, &VideoReader::frameReady, videoProcDayChan, &VideoProcessing::zoom);
        connect(videoProcDayChan, &VideoProcessing::frameZoomed, videoEncDayChan, &VideoEncoder::pushBitmap);
        connect(videoEncDayChan, &VideoEncoder::frameReady, streamServer, &StreamServer::sendDayNal);
        videoReaderDayChan->start();
        videoEncDayChan->start();

        videoReaderHeatChan = new VideoReader(videoHeatPath, this);
        videoProcHeatChan = new VideoProcessing();
        videoEncHeatChan = new VideoEncoder(videoReaderHeatChan->getFrameWidth(),
                                            videoReaderHeatChan->getFrameHeight(),
                                            25, 2000000); // 25 fps, 2Mbps
        connect(streamServer, &StreamServer::zoomChangedHeatChan, videoProcHeatChan, &VideoProcessing::zoomChanged);
        connect(videoReaderHeatChan, &VideoReader::frameReady, videoProcHeatChan, &VideoProcessing::zoom);
        connect(videoProcHeatChan, &VideoProcessing::frameZoomed, videoEncHeatChan, &VideoEncoder::pushBitmap);
        connect(videoEncHeatChan, &VideoEncoder::frameReady, streamServer, &StreamServer::sendHeatNal);
        videoReaderHeatChan->start();
        videoEncHeatChan->start();
    }

    ~ServerApp() {
        delete videoReaderDayChan;
        delete videoEncDayChan;
        delete videoReaderHeatChan;
        delete videoEncHeatChan;
        delete videoProcDayChan;
        delete videoProcHeatChan;
        delete streamServer;
    }

private:
    VideoReader *videoReaderDayChan;
    VideoReader *videoReaderHeatChan;
    VideoEncoder *videoEncDayChan;
    VideoEncoder *videoEncHeatChan;
    StreamServer *streamServer;
    VideoProcessing *videoProcDayChan;
    VideoProcessing *videoProcHeatChan;
};

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCommandLineParser parser;

    parser.setApplicationDescription("Camstream server");
    parser.addHelpOption();
    parser.addVersionOption();

QCommandLineOption certOption(
        {"c", "cert"},
        "Path to SSL certificate file.",
        "certPath"
    );

    QCommandLineOption keyOption(
        {"k", "key"},
        "Path to SSL key file.",
        "keyPath"
    );

    QCommandLineOption videoDayOption(
        {"d", "video-day"},
        "Path to day video file/folder.",
        "videoDayPath"
    );

    QCommandLineOption videoHeatOption(
        {"e", "video-heat"},
        "Path to heat video file/folder.",
        "videoHeatPath"
    );

    QCommandLineOption portOption(
        {"p", "port"},
        "Port to listen on.",
        "port"
    );

    parser.addOption(certOption);
    parser.addOption(keyOption);
    parser.addOption(videoDayOption);
    parser.addOption(videoHeatOption);
    parser.addOption(portOption);

    parser.process(app);

    QString certPath = parser.value(certOption);
    QString keyPath = parser.value(keyOption);
    QString videoDayPath = parser.value(videoDayOption);
    QString videoHeatPath = parser.value(videoHeatOption);
    bool ok = false;
    int port = parser.value(portOption).toInt(&ok);

    if (certPath.isEmpty() ||
        keyPath.isEmpty() ||
        videoDayPath.isEmpty() ||
        videoHeatPath.isEmpty() ||
        !ok || port < 1 || port > 65535)
    {
        qCritical() << "Invalid or missing arguments.";
        parser.showHelp(1);
    }

    ServerApp serverApp(certPath, keyPath, videoDayPath, videoHeatPath, port);

    return app.exec();
}

#include "Server.moc"
