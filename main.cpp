#include <QApplication>
#include <QIcon>
#include <clocale>
#include "player.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("newmpc-be");
    app.setOrganizationName("newmpc-be");
    app.setDesktopFileName("newmpc-be"); // Wayland matches the window to newmpc-be.desktop
    app.setWindowIcon(QIcon::fromTheme("newmpc-be"));
    std::setlocale(LC_NUMERIC, "C"); // libmpv requires the C numeric locale

    Player w;
    w.show();
    QStringList files = app.arguments().mid(1);
    if (!files.isEmpty())
        w.openFiles(files);
    return app.exec();
}
