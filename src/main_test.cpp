#include <cstdio>
#include <QGuiApplication>
#include <QQmlApplicationEngine>

int main(int argc, char *argv[])
{
    printf("Step 1: before QGuiApplication\n");
    fflush(stdout);
    
    QGuiApplication app(argc, argv);
    printf("Step 2: QGuiApplication created\n");
    fflush(stdout);
    
    return 0;
}
