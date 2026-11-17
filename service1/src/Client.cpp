#include "Client.h"
#include "Reply.h"
#include <QHttpServerResponder>
#include <QTcpSocket>
namespace msa {
void Client::send(const Reply& result) const {
    if (!socket || socket->state() != QAbstractSocket::ConnectedState) return;
    const QByteArray body = result.body.toJson(QJsonDocument::Compact);
    responder->writeStatusLine(static_cast<QHttpServerResponder::StatusCode>(result.status));
    responder->writeHeader("Content-Type", "application/json");
    responder->writeHeader("Content-Length", QByteArray::number(body.size()));
    responder->writeHeader("Connection", "close");
    if (result.status == 405) responder->writeHeader("Allow", "GET, HEAD");
    if (!result.instance.isEmpty()) responder->writeHeader("X-Service2-Instance", result.instance);
    responder->writeBody(head ? QByteArray() : body);
    socket->disconnectFromHost();
    qInfo("Service1 status=%d", result.status);
}
}
