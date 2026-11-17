#pragma once
#include "Config.h"
#include "Client.h"
#include <QObject>
#include <QHttpServer>
#include <QNetworkAccessManager>
#include <QThreadPool>
class QHttpServerRequest;
class QHttpServerResponder;
namespace msa {
class Application : public QObject {
    Config config;
    QHttpServer server;
    QNetworkAccessManager network;
    QThreadPool workers;
    void aggregate(Client client, int id);
    void dispatch(const QHttpServerRequest& request, QHttpServerResponder&& responder);
public:
    Application();
};
}
