#include "mainwindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    QCoreApplication::setOrganizationName("CodeLab");
    QCoreApplication::setApplicationName("CodeLab");
    MainWindow w;
    w.show();
    return QApplication::exec();
}
