#pragma once

#include <QObject>
#include <QWebSocketServer>
#include <QWebSocket>

#include "cmd.pb.h"

class StreamServer : public QObject
{
    Q_OBJECT

public:
    explicit StreamServer(const QString &certPath,
                           const QString &keyPath,
                           quint16 port = 443,
                           QObject *parent = nullptr);
    ~StreamServer();

public slots:
    void sendCmd(const QByteArray &protobuf);
    void sendHeatNal(const QByteArray &nal);
    void sendDayNal(const QByteArray &nal);

signals:
    void clientConnected(const QString &path);
    void clientDisconnected(const QString &path);
    void zoomChangedDayChan(int zoom);
    void zoomChangedHeatChan(int zoom);

private slots:
    void onNewConnection();
    void onClientDisconnected();
    void processBinaryMessage(QByteArray message);

private:
    void sendData(QWebSocket *sock, const QByteArray &data);
    void parseCommand(const QByteArray &data);

    QWebSocketServer* m_server;
    QWebSocket* m_cmdClient;
    QWebSocket* m_heatVideoClient;
    QWebSocket* m_dayVideoClient;
};
