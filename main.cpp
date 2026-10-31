#include <QApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QSettings>
#include <QStandardPaths>
#include <clocale>
#include "player.h"

// Reelplay was called newmpc-be up to 0.3.1: move its settings, recent files and resume
// positions over once, so nothing is lost. Never overwrites existing Reelplay data.
static void migrateOldName()
{
    QString config = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    QString data = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    QDir dir;
    if (!dir.exists(config + "/reelplay") && dir.rename(config + "/newmpc-be", config + "/reelplay"))
        dir.rename(config + "/reelplay/newmpc-be.conf", config + "/reelplay/reelplay.conf");
    if (!dir.exists(data + "/reelplay"))
        dir.rename(data + "/newmpc-be", data + "/reelplay");
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("reelplay");
    app.setOrganizationName("reelplay");
    app.setDesktopFileName("reelplay"); // Wayland matches the window to reelplay.desktop
    app.setWindowIcon(QIcon::fromTheme("reelplay"));
    std::setlocale(LC_NUMERIC, "C"); // libmpv requires the C numeric locale
    migrateOldName();

    QStringList files = app.arguments().mid(1);
    for (QString &f : files) // the running player may have another working directory
        if (!f.contains("://") && QFileInfo::exists(f))
            f = QFileInfo(f).absoluteFilePath();

    // Single window, like MPC-BE: whoever owns the name is the window; later launches
    // hand their files over and quit. registerService() is atomic, so no race.
    const QString service = "org.reelplay.Instance";
    QDBusConnection bus = QDBusConnection::sessionBus();
    bool single = QSettings().value("singleInstance", true).toBool() && bus.isConnected();
    if (single && !bus.registerService(service)) {
        QDBusMessage call = QDBusMessage::createMethodCall(service, "/org/reelplay", "org.reelplay.Player", "openFromOtherInstance");
        call << files;
        if (bus.call(call, QDBus::Block, 5000).type() == QDBusMessage::ReplyMessage)
            return 0;
        single = false; // the other one didn't answer: open our own window
    }

    Player w;
    if (single)
        bus.registerObject("/org/reelplay", &w, QDBusConnection::ExportScriptableSlots);
    w.show();
    if (!files.isEmpty())
        w.openFiles(files);
    return app.exec();
}
