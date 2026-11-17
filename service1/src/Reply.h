#pragma once
#include <QByteArray>
#include <QJsonDocument>

namespace msa {
struct Reply {
    int status = 200;
    QJsonDocument body;
    QByteArray instance;
};

Reply error(int status, const char* code, const char* message);
} // namespace msa
