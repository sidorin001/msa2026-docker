#pragma once
#include <QJsonArray>
#include <QSqlDatabase>
#include <QString>
namespace msa {
struct Config;
class Database {
    QString connectionName;
    QSqlDatabase db;
public:
    explicit Database(const Config& config);
    ~Database();
    QJsonArray query(const QString& sql, int id = 0);
};
}
