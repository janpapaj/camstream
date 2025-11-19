#include <QUrlQuery>
#include <QSslConfiguration>
#include <QSslCertificate>
#include <QSslKey>
#include <QFile>
#include <QtEndian>

#include "StreamServer.hpp"

StreamServer::StreamServer(const QString &certPath,
                           const QString &keyPath,
                           quint16 port,
                           QObject* parent)
    : QObject(parent), m_cmdClient(nullptr), m_heatVideoClient(nullptr), m_dayVideoClient(nullptr) {
    m_server = new QWebSocketServer("StreamServer",
                                    QWebSocketServer::SecureMode,
                                    this);

    QFile certFile(certPath);
    if (!certFile.open(QIODevice::ReadOnly)) {
        qCritical() << "Cannot open certificate file:" << certPath;
    }
    QSslCertificate cert(&certFile, QSsl::Pem);

    QFile keyFile(keyPath);
    if (!keyFile.open(QIODevice::ReadOnly)) {
        qCritical() << "Cannot open private key file:" << keyPath;
    }
    QSslKey key(&keyFile, QSsl::Rsa, QSsl::Pem);

    // TLS config - no peer verify for simplicity
    QSslConfiguration sslConfig;
    sslConfig.setLocalCertificate(cert);
    sslConfig.setPrivateKey(key);
    sslConfig.setProtocol(QSsl::TlsV1_2OrLater);
    sslConfig.setPeerVerifyMode(QSslSocket::VerifyNone);
    m_server->setSslConfiguration(sslConfig);

    qInfo() << "Listening on port " << port;
    if (!m_server->listen(QHostAddress::Any, port)) {
        qCritical() << "Listen failed: " << m_server->errorString();
    }

    connect(m_server, &QWebSocketServer::newConnection,
            this, &StreamServer::onNewConnection);
}

StreamServer::~StreamServer() {
    m_server->close();

    if (m_cmdClient) m_cmdClient->deleteLater();
    if (m_dayVideoClient) m_dayVideoClient->deleteLater();
    if (m_heatVideoClient) m_heatVideoClient->deleteLater();
}

void StreamServer::onNewConnection() {
    QWebSocket* sock = m_server->nextPendingConnection();
    if (!sock) return;

    const QUrl path = sock->requestUrl();
    QString route = path.path();

    qInfo() << "New client on route: " << route;

    if (!m_cmdClient && route == "/ws/ws_cmd") {
        m_cmdClient = sock;
    } else if (!m_heatVideoClient && route == "/ws/ws_rec_video_heat") {
        m_heatVideoClient = sock;
    } else if (!m_dayVideoClient && route == "/ws/ws_rec_video_day") {
        m_dayVideoClient = sock;
    } else {
        qWarning() << "Unknown WS path:" << route;
        sock->close();
        sock->deleteLater();
        return;
    }

    connect(sock, &QWebSocket::disconnected,
            this, &StreamServer::onClientDisconnected);

    connect(sock, &QWebSocket::binaryMessageReceived,
            this, &StreamServer::processBinaryMessage);
}

void StreamServer::onClientDisconnected() {
    QWebSocket* sock = qobject_cast<QWebSocket*>(sender());
    if (!sock) return;

    QString route = sock->requestUrl().path();

    if (route == "/ws/ws_cmd") {
        m_cmdClient = nullptr;
    } else if (route == "/ws/ws_rec_video_heat") {
        m_heatVideoClient = nullptr;
    } else if (route == "/ws/ws_rec_video_day") {
        m_dayVideoClient = nullptr;
    }

    sock->deleteLater();
    emit clientDisconnected(route);
}

void StreamServer::processBinaryMessage(QByteArray message) {
    QWebSocket* sock = qobject_cast<QWebSocket*>(sender());
    if (!sock) return;

    QString route = sock->requestUrl().path();

    if (route == "/ws/ws_cmd") {
        parseCommand(message);
    } else {
       qWarning() << "Unknown data to path " << route;
    }
}

void StreamServer::sendData(QWebSocket* sock, const QByteArray& data) {
    if (sock) {
        sock->sendBinaryMessage(data);
    }
}

void StreamServer::parseCommand(const QByteArray &data) {
    camera::CommandMessage cmd;
        
    quint32 msgLen = qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(data.constData()));
    if (data.size() < 4 + (int) msgLen) {
        qWarning() << "Incomplete message";
        return;
    }

    QByteArray payload = data.mid(4, msgLen);
    if (!cmd.ParseFromArray(payload.constData(), payload.size())) {
        qWarning() << "Failed to parse protobuf";
        return;
    }

    switch (cmd.type()) {
        case camera::CMD_ZOOM:
            if (cmd.has_zoom()) {
                int level = cmd.zoom().level();
                qInfo() << "Channel" << cmd.chan() << "zoom" << level;
                switch (cmd.chan()) {
                    case camera::CAMERA_DAY:
                        emit zoomChangedDayChan(level);
                        break;
                    case camera::CAMERA_HEAT:
                        emit zoomChangedHeatChan(level);
                        break;
                    case camera::CAMERA_UNKNOWN:
                    default:
                        qWarning() << "Unknown channel type";
                        break;

                }
            } else {
                qWarning() << "No zoom payload present";
            }
            break;
        case camera::CMD_UNKNOWN:
        default:
            qWarning() << "Unknown command type";
            break;
    }
}

void StreamServer::sendCmd(const QByteArray& protobuf) {
    sendData(m_cmdClient, protobuf);
}

void StreamServer::sendHeatNal(const QByteArray& nal) {
    sendData(m_heatVideoClient, nal);
}

void StreamServer::sendDayNal(const QByteArray& nal) {
    sendData(m_dayVideoClient, nal);
}
