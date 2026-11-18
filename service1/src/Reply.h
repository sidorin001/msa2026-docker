#pragma once
#include <QByteArray>
#include <QHttpServerResponse>
#include <QJsonDocument>

namespace msa {
struct Reply {
    int status = 200;
    QJsonDocument body;
    QByteArray instance;

    QHttpServerResponse toHttpResponse() const;
};

Reply error(int status, const char* code, const char* message);
} // namespace msa
