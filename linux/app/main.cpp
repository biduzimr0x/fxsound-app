/*
FxSound for Linux
Copyright (C) 2026  FxSound Linux port contributors

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU Affero General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
*/
#include "FxController.h"
#include "MainWindow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMessageBox>
#include <QSocketNotifier>

#include <csignal>
#include <unistd.h>

#ifndef FXSOUND_VERSION
#define FXSOUND_VERSION "dev"
#endif

namespace {

int g_signal_pipe[2] = {-1, -1};

void on_signal(int)
{
    char c = 1;
    ssize_t r = write(g_signal_pipe[1], &c, 1);
    (void)r;
}

QString serverName() { return QStringLiteral("fxsound-%1").arg(getuid()); }

// Envia um comando a uma instância já em execução. Retorna true se havia uma.
bool sendToRunning(const QString &cmd)
{
    QLocalSocket s;
    s.connectToServer(serverName());
    if (!s.waitForConnected(500)) return false;
    s.write(cmd.toUtf8() + '\n');
    s.waitForBytesWritten(1000);
    s.disconnectFromServer();
    return true;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("fxsound");
    QApplication::setApplicationDisplayName("FxSound");
    QApplication::setApplicationVersion(FXSOUND_VERSION);
    QApplication::setDesktopFileName("fxsound");
    QApplication::setQuitOnLastWindowClosed(false);

    QCommandLineParser p;
    p.setApplicationDescription("FxSound para Linux (PipeWire)");
    p.addHelpOption();
    p.addVersionOption();
    QCommandLineOption minimized({"m", "minimized"}, "Inicia minimizado na bandeja.");
    QCommandLineOption toggle("toggle", "Liga/desliga o processamento.");
    QCommandLineOption on("on", "Liga o processamento.");
    QCommandLineOption off("off", "Desliga o processamento.");
    QCommandLineOption next("next", "Próximo preset.");
    QCommandLineOption prev("prev", "Preset anterior.");
    QCommandLineOption show("show", "Mostra a janela.");
    QCommandLineOption preset("preset", "Seleciona um preset pelo nome.", "nome");
    QCommandLineOption quit("quit", "Encerra a instância em execução.");
    p.addOptions({minimized, toggle, on, off, next, prev, show, preset, quit});
    p.process(app);

    QString cmd = "show";
    if (p.isSet(toggle)) cmd = "toggle";
    else if (p.isSet(on)) cmd = "on";
    else if (p.isSet(off)) cmd = "off";
    else if (p.isSet(next)) cmd = "next";
    else if (p.isSet(prev)) cmd = "prev";
    else if (p.isSet(preset)) cmd = "preset:" + p.value(preset);
    else if (p.isSet(quit)) cmd = "quit";
    else if (p.isSet(minimized)) cmd = QString();

    // instância única: repassa o comando e sai
    if (sendToRunning(cmd.isEmpty() ? QStringLiteral("noop") : cmd)) return 0;
    if (cmd == "quit") return 0;

    // SIGTERM/SIGINT (ex.: fim da sessão) encerram limpo, restaurando o sink padrão
    if (pipe(g_signal_pipe) == 0) {
        auto *sn = new QSocketNotifier(g_signal_pipe[0], QSocketNotifier::Read, &app);
        QObject::connect(sn, &QSocketNotifier::activated, &app, [] {
            char c;
            ssize_t r = read(g_signal_pipe[0], &c, 1);
            (void)r;
            QApplication::quit();
        });
        struct sigaction sa {};
        sa.sa_handler = on_signal;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGTERM, &sa, nullptr);
        sigaction(SIGINT, &sa, nullptr);
        sigaction(SIGHUP, &sa, nullptr);
    }

    FxController controller;
    QString error;
    if (!controller.init(error)) {
        QMessageBox::critical(nullptr, "FxSound", QObject::tr("Não foi possível iniciar o FxSound:\n%1").arg(error));
        return 1;
    }

    MainWindow window(&controller);

    QLocalServer::removeServer(serverName());
    QLocalServer server;
    server.listen(serverName());
    QObject::connect(&server, &QLocalServer::newConnection, &app, [&] {
        while (QLocalSocket *s = server.nextPendingConnection()) {
            QObject::connect(s, &QLocalSocket::readyRead, s, [s, &window] {
                while (s->canReadLine()) {
                    QString c = QString::fromUtf8(s->readLine()).trimmed();
                    if (c == "quit") QApplication::quit();
                    else if (c != "noop") window.handleCommand(c);
                }
            });
            QObject::connect(s, &QLocalSocket::disconnected, s, &QObject::deleteLater);
        }
    });

    QObject::connect(&app, &QApplication::aboutToQuit, &controller, &FxController::shutdown);

    if (cmd.isEmpty() && QSystemTrayIcon::isSystemTrayAvailable()) {
        // inicia só na bandeja
    } else {
        window.show();
        if (!cmd.isEmpty() && cmd != "show") window.handleCommand(cmd);
    }
    return app.exec();
}
