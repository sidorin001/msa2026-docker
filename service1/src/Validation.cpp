#include "Validation.h"
#include <QRegularExpression>
#include <stdexcept>
namespace msa {
int positive(const QString& value) {
    static const QRegularExpression digits("^[0-9]+$");
    bool ok = false;
    int id = value.toInt(&ok);
    if (!digits.match(value).hasMatch() || !ok || id <= 0) throw std::invalid_argument("id");
    return id;
}

}
