#include <QApplication>
#include "MainWindow.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("ChessEngine"));
    QApplication::setApplicationName(QStringLiteral("ChessEngineGui"));

    MainWindow w;
    w.show();
    return app.exec();
}
