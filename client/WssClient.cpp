#include "WssClient.hpp"
#include <QSslConfiguration>
#include <QSslCertificate>
#include <QSslKey>
#include <QTimer>

WssClient::WssClient(const QUrl &url, QObject* parent) : QObject(parent), m_url(url), m_reconnecting(false) {
    m_sock = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);

    qInfo() << "Connecting to" << url;
    // Allow self-signed certificate
    QSslConfiguration config = QSslConfiguration::defaultConfiguration();
    config.setPeerVerifyMode(QSslSocket::VerifyNone);
    m_sock->setSslConfiguration(config);

    connect(m_sock, &QWebSocket::connected, this, &WssClient::onConnected);
    connect(m_sock, &QWebSocket::disconnected, this, &WssClient::onDisconnected);

    m_sock->open(m_url);
}

WssClient::~WssClient() {
    m_sock->close();
    delete m_sock;
}

void WssClient::sendData(const QByteArray& data) {
    if (m_sock->state() == QAbstractSocket::ConnectedState) {
        m_sock->sendBinaryMessage(data);
    } else {
        qWarning() << "Client connected";
    }
}

void WssClient::onConnected() {
    qInfo() << "Connected to " << m_url.toString();
    connect(m_sock, &QWebSocket::binaryMessageReceived,
            this, &WssClient::onBinaryMessageReceived);
    emit connected();
}

void WssClient::onDisconnected() {
    if (!m_reconnecting) {
        m_sock->close();
        m_reconnecting = true;
        qWarning() << "Disconnected. Attempting reconnect in 10 seconds to" << m_url.toString();
        QTimer::singleShot(10000, [this]() {
            m_sock->open(m_url);
            m_reconnecting = false;
        });
    }
    emit disconnected();
}

void WssClient::onBinaryMessageReceived(const QByteArray &msg) {
    emit newData(msg);
}

void WssClient::onError(QAbstractSocket::SocketError error) {
    qWarning() << "WebSocket error:" << error
               << m_sock->errorString();
}

void WssClient::onSslErrors(const QList<QSslError> &errors) {
    for (const auto &err : errors)
        qWarning() << "SSL error:" << err.errorString();

    m_sock->ignoreSslErrors();
}