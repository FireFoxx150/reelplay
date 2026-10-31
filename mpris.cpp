#include "mpris.h"
#include "player.h"
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QListWidget>
#include <QUrl>

static const char *objectPath = "/org/mpris/MediaPlayer2";

// Settings daemons offering GrabMediaPlayerKeys: Cinnamon (under the GNOME name), GNOME, MATE.
struct KeyService { const char *service, *path, *iface; };
static const KeyService keyServices[] = {
    {"org.gnome.SettingsDaemon", "/org/gnome/SettingsDaemon/MediaKeys", "org.gnome.SettingsDaemon.MediaKeys"},
    {"org.gnome.SettingsDaemon.MediaKeys", "/org/gnome/SettingsDaemon/MediaKeys", "org.gnome.SettingsDaemon.MediaKeys"},
    {"org.mate.SettingsDaemon", "/org/mate/SettingsDaemon/MediaKeys", "org.mate.SettingsDaemon.MediaKeys"},
};

static QString trackId(int index)
{
    return index < 0 ? "/org/mpris/MediaPlayer2/TrackList/NoTrack"
                     : "/org/reelplay/track/" + QString::number(index);
}

Mpris::Mpris(Player *player) : QObject(player), player(player)
{
    new MprisRoot(this);
    new MprisPlayer(this);
    QDBusConnection bus = QDBusConnection::sessionBus();
    service = "org.mpris.MediaPlayer2.reelplay";
    if (!bus.registerService(service)) { // another instance already owns the plain name
        service += ".instance" + QString::number(QCoreApplication::applicationPid());
        bus.registerService(service);
    }
    bus.registerObject(objectPath, this);
    for (const KeyService &k : keyServices)
        bus.connect(k.service, k.path, k.iface, "MediaPlayerKeyPressed", this, SLOT(mediaPlayerKeyPressed(QString, QString)));
    grabMediaKeys();
}

void Mpris::grabMediaKeys()
{
    for (const KeyService &k : keyServices) {
        QDBusMessage msg = QDBusMessage::createMethodCall(k.service, k.path, k.iface, "GrabMediaPlayerKeys");
        msg << QString("reelplay") << 0u; // time 0 = now: become the newest grabber
        msg.setAutoStartService(false);
        QDBusConnection::sessionBus().call(msg, QDBus::NoBlock); // absent daemons just error out
    }
}

void Mpris::mediaPlayerKeyPressed(const QString &application, const QString &key)
{
    if (application != "reelplay")
        return; // the signal is broadcast; it's for whichever app grabbed last
    Player *p = player;
    if (key == "Play")
        p->togglePause(); // the Play key is play/pause
    else if (key == "Pause")
        p->pause();
    else if (key == "Stop")
        p->stop();
    else if (key == "Next")
        p->skipFile(1);
    else if (key == "Previous")
        p->skipFile(-1);
    else if (key == "FastForward")
        p->command({"seek", "10"});
    else if (key == "Rewind")
        p->command({"seek", "-10"});
}

Mpris::~Mpris()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    for (const KeyService &k : keyServices) {
        QDBusMessage msg = QDBusMessage::createMethodCall(k.service, k.path, k.iface, "ReleaseMediaPlayerKeys");
        msg << QString("reelplay");
        msg.setAutoStartService(false);
        bus.call(msg, QDBus::NoBlock);
    }
    bus.unregisterObject(objectPath);
    bus.unregisterService(service);
}

QVariantMap Mpris::playerProperties()
{
    Player *p = player;
    bool loaded = !p->stopped && p->current >= 0;
    QVariantMap meta{{"mpris:trackid", QVariant::fromValue(QDBusObjectPath(trackId(loaded ? p->current : -1)))}};
    if (loaded) {
        QString source = p->playlist->item(p->current)->data(Qt::UserRole + 1).toString();
        meta["xesam:url"] = source.contains("://") ? source : QUrl::fromLocalFile(source).toString();
        QString title = p->prop("media-title");
        meta["xesam:title"] = title.isEmpty() ? p->playlist->item(p->current)->text() : title;
        if (p->duration > 0)
            meta["mpris:length"] = qlonglong(p->duration * 1e6);
        QString artist = p->prop("metadata/by-key/Artist"), album = p->prop("metadata/by-key/Album");
        if (!artist.isEmpty())
            meta["xesam:artist"] = QStringList{artist};
        if (!album.isEmpty())
            meta["xesam:album"] = album;
    }
    int count = p->playlist->count();
    return {
        {"PlaybackStatus", p->stopped ? "Stopped" : p->prop("pause") == "yes" ? "Paused" : "Playing"},
        {"Metadata", meta},
        {"Volume", p->prop("volume").toDouble() / 100},
        {"Rate", p->stopped ? 1.0 : p->prop("speed").toDouble()},
        {"CanGoNext", p->canSkip(1)},
        {"CanGoPrevious", p->canSkip(-1)},
        {"CanPlay", count > 0},
        {"CanSeek", loaded && p->duration > 0},
    };
}

void Mpris::update()
{
    if (!player->mpv)
        return; // shutting down
    QVariantMap now = playerProperties(), changed;
    for (auto it = now.cbegin(); it != now.cend(); ++it)
        if (last.value(it.key()) != it.value())
            changed[it.key()] = it.value();
    last = now;
    if (changed.isEmpty())
        return;
    QDBusMessage msg = QDBusMessage::createSignal(objectPath, "org.freedesktop.DBus.Properties", "PropertiesChanged");
    msg << QString("org.mpris.MediaPlayer2.Player") << changed << QStringList();
    QDBusConnection::sessionBus().send(msg);
}

void Mpris::seeked(double pos)
{
    emit findChild<MprisPlayer *>()->Seeked(qlonglong(pos * 1e6));
}

QStringList MprisRoot::schemes() const
{
    return {"file", "http", "https", "rtsp", "rtmp", "mms", "mmsh", "srt", "udp", "ftp", "smb", "dvd", "bd", "cdda"};
}

QStringList MprisRoot::mimeTypes() const
{
    return {"video/x-matroska", "video/mp4", "video/webm", "video/x-msvideo", "video/quicktime", "video/mpeg",
            "video/mp2t", "video/x-ms-wmv", "video/x-flv", "video/ogg", "audio/mpeg", "audio/flac", "audio/ogg",
            "audio/mp4", "audio/x-wav", "audio/x-ms-wma", "audio/opus", "audio/x-mpegurl", "application/xspf+xml"};
}

void MprisRoot::Raise()
{
    m->player->showNormal();
    m->player->raise();
    m->player->activateWindow();
}

void MprisRoot::Quit()
{
    m->player->close();
}

qlonglong MprisPlayer::position() const
{
    return m->player->stopped ? 0 : qlonglong(m->player->prop("time-pos").toDouble() * 1e6);
}

void MprisPlayer::setRate(double r)
{
    if (r > 0) // spec: 0 would mean pause; ignore it
        m->player->command({"set", "speed", QByteArray::number(qBound(0.25, r, 4.0))});
}

void MprisPlayer::setVolume(double v)
{
    m->player->command({"set", "volume", QByteArray::number(qBound(0.0, v, 1.0) * 100)});
}

void MprisPlayer::Next() { m->player->skipFile(1); }
void MprisPlayer::Previous() { m->player->skipFile(-1); }
void MprisPlayer::Pause() { m->player->pause(); }
void MprisPlayer::Play() { m->player->play(); }
void MprisPlayer::PlayPause() { m->player->togglePause(); }
void MprisPlayer::Stop() { m->player->stop(); }
void MprisPlayer::OpenUri(const QString &Uri) { m->player->openFiles({Uri}); }

void MprisPlayer::Seek(qlonglong Offset)
{
    if (!m->player->stopped)
        m->player->command({"seek", QByteArray::number(Offset / 1e6)});
}

void MprisPlayer::SetPosition(const QDBusObjectPath &TrackId, qlonglong Position)
{
    // Spec: ignore stale requests for a track that is no longer current.
    if (!m->player->stopped && TrackId.path() == trackId(m->player->current))
        m->player->command({"seek", QByteArray::number(Position / 1e6), "absolute"});
}
