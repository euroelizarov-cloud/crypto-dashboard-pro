#include <QApplication>
#include "MainWindow.h"
#include "Profiler.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    Profiler::setEnabled(true);
    qApp->setStyleSheet("QWidget { background-color: #121212; color: white; font-family: -apple-system, 'SF Pro Text', 'Helvetica Neue', Arial, sans-serif; }");
    MainWindow w; w.show();
    return app.exec();
}
