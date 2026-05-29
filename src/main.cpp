#include "AppController.h"
#include "ThemeProvider.h"
#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QIcon>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setWindowIcon(QIcon(":/assets/app_icon.png"));
    app.setApplicationName("LlamaCode");
    app.setOrganizationName("LlamaCode");
    app.setApplicationVersion("0.1.0");

    AppController controller;
    ThemeProvider theme;

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("App", &controller);
    engine.rootContext()->setContextProperty("Theme", &theme);
    engine.addImportPath(QStringLiteral("qrc:/"));
    engine.loadFromModule("LlamaCode", "Main");

    if (engine.rootObjects().isEmpty())
        return -1;

    return app.exec();
}
