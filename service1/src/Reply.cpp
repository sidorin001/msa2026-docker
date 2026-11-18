#include "Reply.h"
#include <QJsonArray>
#include <QJsonObject>

namespace msa {
QHttpServerResponse Reply::toHttpResponse() const {
    const auto code = static_cast<QHttpServerResponse::StatusCode>(status);
    auto response = body.isArray() ? QHttpServerResponse(body.array(), code)
                                   : QHttpServerResponse(body.object(), code);
    response.setHeader("Connection", "close");
    if (status == 405)
        response.setHeader("Allow", "GET, HEAD");
    if (!instance.isEmpty())
        response.setHeader("X-Service2-Instance", instance);
    return response;
}

Reply error(int status, const char* code, const char* message) {
    QJsonObject detail{{"code", code}, {"message", message}};
    return {status, QJsonDocument(QJsonObject{{"error", detail}}), {}};
}

} // namespace msa
