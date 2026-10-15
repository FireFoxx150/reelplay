#pragma once
#include <QMainWindow>
#include <QKeySequence>
#include <functional>
#include <mpv/client.h>
#include "videowidget.h"

class QSlider;
class QLabel;
class QListWidget;
class QDockWidget;
class QMenu;
class QAction;
class QTimer;
class Mpris;

class Player : public QMainWindow
{
    Q_OBJECT
public:
    Player();
    ~Player() override;
    void openFiles(const QStringList &files, bool append = false);

protected:
    bool eventFilter(QObject *obj, QEvent *ev) override;
    void closeEvent(QCloseEvent *ev) override;
    void changeEvent(QEvent *ev) override;
    void dragEnterEvent(QDragEnterEvent *ev) override;
    void dropEvent(QDropEvent *ev) override;

private:
    QAction *act(QMenu *menu, const QString &text, const QKeySequence &key, std::function<void()> fn);
    void command(std::initializer_list<QByteArray> args);
    QString prop(const char *name);
    void handleEvents();
    void playIndex(int i);
    void togglePause();
    void skipFile(int dir);
    bool canSkip(int dir);
    void play();
    void pause();
    void stop();
    void savePosition();
    void shutdown();
    void toggleFullscreen();
    void rebuildTrackMenus();
    void updateTime(double pos);
    void zoom(double factor);
    double step(const char *name, double delta, double lo, double hi);
    void osd(const QString &text);
    void addRecent(const QString &path);
    void syncCurrent();
    void savePlaylist(const QString &path);
    QString propertiesText();
    void screenshot(const QString &path);
    QString autoScreenshotPath();
    void openOptions();
    void applySettings();

    mpv_handle *mpv = nullptr;
    QWidget *video, *controls;
    VideoWidget *glVideo = nullptr; // set when drawing via the render API
    QSlider *seekBar, *volBar;
    QLabel *timeLabel, *statusLabel;
    QListWidget *playlist;
    QDockWidget *dock;
    QMenu *audioMenu, *subMenu, *contextMenu, *recentMenu;
    QAction *muteAct;
    QTimer *clickTimer, *idleTimer;
    Mpris *mpris = nullptr;
    int current = -1;
    QString folderNeighbors[2]; // previous/next file in the folder of a lone playlist entry
    double duration = 0;
    bool stopped = true;
    enum { RepeatOff, RepeatFile, RepeatPlaylist } repeat = RepeatOff;
    friend class PlayerTest;
    friend class Mpris;
    friend class MprisRoot;
    friend class MprisPlayer;
};
