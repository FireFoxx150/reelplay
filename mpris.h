#pragma once
#include <QDBusAbstractAdaptor>
#include <QDBusObjectPath>
#include <QStringList>
#include <QVariantMap>

class Player;

// MPRIS2 on the session bus. Desktops route the keyboard's media keys (Play/Pause, Stop,
// Next, Previous) to MPRIS players instead of the focused window; it also puts the player
// in the sound applet and makes it controllable by tools like playerctl.
// Cinnamon/GNOME/MATE only fall back to MPRIS (and then pick the first player that ever
// registered, e.g. a browser) unless an app grabs the keys with GrabMediaPlayerKeys, so
// we grab them too; the most recent grabber wins.
class Mpris : public QObject
{
    Q_OBJECT
public:
    explicit Mpris(Player *player);
    ~Mpris() override;
    void update();           // emit PropertiesChanged for whatever changed since last time
    void seeked(double pos); // emit Player.Seeked after a jump
    QString serviceName() const { return service; }
    QVariantMap playerProperties();
    void grabMediaKeys(); // call again whenever the window is activated

    Player *player;

public slots:
    void mediaPlayerKeyPressed(const QString &application, const QString &key);

private:
    QString service;
    QVariantMap last;
};

class MprisRoot : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2")
    Q_PROPERTY(bool CanQuit READ yes)
    Q_PROPERTY(bool CanRaise READ yes)
    Q_PROPERTY(bool HasTrackList READ no)
    Q_PROPERTY(QString Identity READ identity)
    Q_PROPERTY(QString DesktopEntry READ identity)
    Q_PROPERTY(QStringList SupportedUriSchemes READ schemes)
    Q_PROPERTY(QStringList SupportedMimeTypes READ mimeTypes)
public:
    explicit MprisRoot(Mpris *m) : QDBusAbstractAdaptor(m), m(m) {}
    bool yes() const { return true; }
    bool no() const { return false; }
    QString identity() const { return "newmpc-be"; }
    QStringList schemes() const;
    QStringList mimeTypes() const;
public slots:
    void Raise();
    void Quit();
private:
    Mpris *m;
};

class MprisPlayer : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
    Q_PROPERTY(QString PlaybackStatus READ playbackStatus)
    Q_PROPERTY(double Rate READ rate WRITE setRate)
    Q_PROPERTY(QVariantMap Metadata READ metadata)
    Q_PROPERTY(double Volume READ volume WRITE setVolume)
    Q_PROPERTY(qlonglong Position READ position)
    Q_PROPERTY(double MinimumRate READ minimumRate)
    Q_PROPERTY(double MaximumRate READ maximumRate)
    Q_PROPERTY(bool CanGoNext READ canGoNext)
    Q_PROPERTY(bool CanGoPrevious READ canGoPrevious)
    Q_PROPERTY(bool CanPlay READ canPlay)
    Q_PROPERTY(bool CanPause READ canPlay)
    Q_PROPERTY(bool CanSeek READ canSeek)
    Q_PROPERTY(bool CanControl READ canControl)
public:
    explicit MprisPlayer(Mpris *m) : QDBusAbstractAdaptor(m), m(m) {}
    QString playbackStatus() const { return m->playerProperties().value("PlaybackStatus").toString(); }
    QVariantMap metadata() const { return m->playerProperties().value("Metadata").toMap(); }
    double rate() const { return m->playerProperties().value("Rate").toDouble(); }
    double volume() const { return m->playerProperties().value("Volume").toDouble(); }
    bool canGoNext() const { return m->playerProperties().value("CanGoNext").toBool(); }
    bool canGoPrevious() const { return m->playerProperties().value("CanGoPrevious").toBool(); }
    bool canPlay() const { return m->playerProperties().value("CanPlay").toBool(); }
    bool canSeek() const { return m->playerProperties().value("CanSeek").toBool(); }
    bool canControl() const { return true; }
    double minimumRate() const { return 0.25; }
    double maximumRate() const { return 4; }
    qlonglong position() const;
    void setRate(double r);
    void setVolume(double v);
signals:
    void Seeked(qlonglong Position);
public slots:
    void Next();
    void Previous();
    void Pause();
    void PlayPause();
    void Stop();
    void Play();
    void Seek(qlonglong Offset);
    void SetPosition(const QDBusObjectPath &TrackId, qlonglong Position);
    void OpenUri(const QString &Uri);
private:
    Mpris *m;
};
