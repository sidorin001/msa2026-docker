#pragma once
#include <QString>
#include <QUrl>

namespace msa {
struct Config {
    QString host, name, user, password;
    int port, httpPort;
    int dbConnect, statement;
    int connect, read;
    QUrl upstream;

    Config();
};
} // namespace msa
