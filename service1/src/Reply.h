#pragma once
#include <QJsonDocument>
#include <QByteArray>
namespace msa {
struct Reply { int status = 200; QJsonDocument body; QByteArray instance; };
Reply error(int status, const char* code, const char* message);
}
