#include "mainwindow.h"
#include <QApplication>

int main(int argc, char *argv[])
{
    qRegisterMetaType<QList<int>>("QList<int>>");

    QApplication a(argc, argv);
    MainWindow w;
    w.show();

    return a.exec();
}
