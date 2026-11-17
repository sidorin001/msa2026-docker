#include "Reply.h"
#include <QJsonObject>

namespace msa {
Reply error(int status, const char* code, const char* message) {
    QJsonObject detail{{"code", code}, {"message", message}};
    return {status, QJsonDocument(QJsonObject{{"error", detail}}), {}};
}

} // namespace msa
