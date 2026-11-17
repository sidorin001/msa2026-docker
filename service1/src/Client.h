#pragma once
#include <QPointer>
#include <memory>
class QHttpServerResponder;
class QTcpSocket;

namespace msa {
struct Reply;

struct Client {
    std::shared_ptr<QHttpServerResponder> responder;
    QPointer<QTcpSocket> socket;
    bool head;

    void send(const Reply& result) const;
};
} // namespace msa
