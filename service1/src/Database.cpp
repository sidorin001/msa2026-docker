#include "Database.h"
#include "Config.h"
#include <QJsonObject>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QVariant>
#include <QUuid>
#include <stdexcept>
namespace msa {
Database::Database(const Config& c) : connectionName(QUuid::createUuid().toString()), db(QSqlDatabase::addDatabase("QPSQL", connectionName)) {
    db.setHostName(c.host); db.setPort(c.port); db.setDatabaseName(c.name);
    db.setUserName(c.user); db.setPassword(c.password);
    db.setConnectOptions(QString("connect_timeout=%1;options='-c statement_timeout=%2'").arg(c.dbConnect).arg(c.statement));
}
Database::~Database() { db.close(); db = QSqlDatabase(); QSqlDatabase::removeDatabase(connectionName); }
QJsonArray Database::query(const QString& sql, int id) {
    if (!db.isOpen() && !db.open()) throw std::runtime_error("database");
    QSqlQuery query(db);
    if (!query.prepare(sql)) throw std::runtime_error("database");
    if (id) query.addBindValue(id);
    if (!query.exec()) throw std::runtime_error("database");
    QJsonArray rows;
    while (query.next()) {
        QJsonObject row;
        const auto record = query.record();
        for (int i = 0; i < record.count(); ++i) {
            const auto field = record.fieldName(i);
            row.insert(field, field == "id" ? QJsonValue(query.value(i).toInt()) : QJsonValue(query.value(i).toString()));
        }
        rows.append(row);
    }
    return rows;
}
}
