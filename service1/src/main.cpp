#include "Application.h"
#include <QCoreApplication>
#include <csignal>
#include <exception>
#include <thread>
#include <pthread.h>

int main(int argc, char** argv) {
    sigset_t terminationSet;
    sigemptyset(&terminationSet); sigaddset(&terminationSet, SIGINT); sigaddset(&terminationSet, SIGTERM);
    if (pthread_sigmask(SIG_BLOCK, &terminationSet, nullptr) != 0) return 1;
    QCoreApplication app(argc, argv);
    try {
        msa::Application service;
        std::thread terminator([&] {
            int signal = 0; sigwait(&terminationSet, &signal);
            QMetaObject::invokeMethod(&app, &QCoreApplication::quit, Qt::QueuedConnection);
        });
        int result = app.exec();
        pthread_kill(terminator.native_handle(), SIGTERM); terminator.join();
        return result;
    } catch (const std::exception&) {
        qCritical("Service1 startup failed; check configuration and Qt plugins"); return 1;
    }
}
