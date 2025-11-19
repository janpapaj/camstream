#pragma once

#include <QObject>
#include <QWebSocket>

class WssClient : public QObject
{
    Q_OBJECT

public:
    explicit WssClient(const QUrl &url, QObject* parent = nullptr);
    ~WssClient();

    void sendData(const QByteArray& data);

signals:
    void newData(const QByteArray& data);
    void connected();
    void disconnected();

private slots:
    void onConnected();
    void onDisconnected();
    void onBinaryMessageReceived(const QByteArray &msg);
    void onError(QAbstractSocket::SocketError error);
    void onSslErrors(const QList<QSslError> &errors);

private:
    QUrl m_url;
    QWebSocket* m_sock;
    bool m_reconnecting;
};
