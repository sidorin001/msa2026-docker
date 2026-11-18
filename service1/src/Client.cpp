#include "Client.h"
#include "Reply.h"
#include <QBuffer>
#include <QHttpServerResponder>
#include <QTcpSocket>

namespace msa {
void Client::send(const Reply& result) const {
    if (!socket || socket->state() != QAbstractSocket::ConnectedState)
        return;
    auto response = result.toHttpResponse();
    if (head) {
        // Qt 6.4's response writer does not suppress HEAD bodies. An EOF-positioned
        // QBuffer preserves the GET representation size while Qt sends only headers.
        // The responder takes ownership of the buffer.
        auto* content = new QBuffer;
        content->setData(response.data());
        content->open(QIODevice::ReadOnly);
        content->seek(content->size());
        if (result.instance.isEmpty()) {
            responder->write(content, response.mimeType(), response.statusCode());
        } else {
            responder->write(
                content,
                {{"Content-Type", response.mimeType()}, {"X-Service2-Instance", result.instance}},
                response.statusCode());
        }
    } else {
        response.write(std::move(*responder));
    }
    socket->disconnectFromHost();
    qInfo("Service1 status=%d", result.status);
}
} // namespace msa
