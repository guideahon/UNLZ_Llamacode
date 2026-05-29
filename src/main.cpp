#include "AppController.h"
#include "ThemeProvider.h"
#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QIcon>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QStandardPaths>
#include <QDir>
#include <QtMessageHandler>

static QFile s_logFile;
static QTextStream s_logStream;

static void messageHandler(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
    const char *level = "DEBUG";
    if      (type == QtWarningMsg)  level = "WARN ";
    else if (type == QtCriticalMsg) level = "ERROR";
    else if (type == QtFatalMsg)    level = "FATAL";

    QString line = QStringLiteral("[%1] [%2] %3")
        .arg(QDateTime::currentDateTime().toString("hh:mm:ss.zzz"))
        .arg(QLatin1String(level))
        .arg(msg);

    if (s_logFile.isOpen()) {
        s_logStream << line << "\n";
        s_logStream.flush();
    }

    // Also forward to default output (visible when run from cmd)
    fprintf(type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg ? stderr : stdout,
            "%s\n", qPrintable(line));

    if (type == QtFatalMsg)
        abort();
}

int main(int argc, char *argv[])
{
    // Log file: %APPDATA%\LlamaCode\llamacode.log
    QString logDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(logDir);
    s_logFile.setFileName(logDir + "/llamacode.log");
    if (s_logFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        s_logStream.setDevice(&s_logFile);

    qInstallMessageHandler(messageHandler);
    qDebug() << "=== LlamaCode starting ===" << QDateTime::currentDateTime().toString();

    QApplication app(argc, argv);
    app.setWindowIcon(QIcon(":/assets/app_icon.png"));
    app.setApplicationName("LlamaCode");
    app.setOrganizationName("LlamaCode");
    app.setApplicationVersion("0.1.0");

    qDebug() << "QApplication ready";

    AppController controller;
    ThemeProvider theme;

    qDebug() << "Controllers ready";

    QQmlApplicationEngine engine;

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                     &app, []() {
                         qCritical() << "QML object creation failed — aborting";
                         QCoreApplication::exit(-1);
                     }, Qt::QueuedConnection);

    engine.rootContext()->setContextProperty("App", &controller);
    engine.rootContext()->setContextProperty("Theme", &theme);
    engine.addImportPath(QStringLiteral("qrc:/"));

    qDebug() << "Loading Main.qml";
    engine.loadFromModule("LlamaCode", "Main");

    if (engine.rootObjects().isEmpty()) {
        qCritical() << "No root objects — QML load failed";
        return -1;
    }

    qDebug() << "QML loaded OK — entering event loop";
    int ret = app.exec();
    qDebug() << "Event loop exited with code" << ret;
    return ret;
}
