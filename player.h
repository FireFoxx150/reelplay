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
class QActionGroup;
class Mpris;

double parseTime(const QString &text); // "1:23:45", "83:45", "45.5" -> seconds; -1 if invalid

class Player : public QMainWindow
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.reelplay.Player")
public:
    Player();
    ~Player() override;
    void openFiles(const QStringList &files, bool append = false);

public slots:
    // Called over D-Bus by a second launch (single window mode).
    Q_SCRIPTABLE void openFromOtherInstance(const QStringList &files);

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
    void readChapters();
    void updateChapterMarks();
    void rebuildChapterMenu();
    void applyChrome(bool fullscreen);
    void setAlwaysOnTop(bool on);
    void toggleStats(bool on);
    void updateStats();
    void setVideoAdjust(const char *name, int value);
    void openVideoAdjust();
    void rotateVideo(int by);
    void setCrop(double aspect);
    void applyAudioFilters();
    void applyVisualizer();
    void saveAbSection(const QString &out);
    void startScreenshotSeries(int seconds);
    void stopScreenshotSeries();
    void setEqGains(const QList<double> &gains);
    void openEqualizer();
    void rebuildAudioDeviceMenu();
    void finishPlaylist();
    void addFavorite();
    void rebuildFavoritesMenu();
    void setScale(const QString &filter);
    void setToneMap(bool on);
    void setPassthrough(bool on);
    void setVideoFilter(const char *label, const char *filter, bool on);
    void skipIntro();
    void addBookmark();
    void rebuildBookmarksMenu();
    void startRecording(const QString &path);
    void stopRecording();
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
    QMenu *audioMenu, *subMenu, *contextMenu, *recentMenu, *chapterMenu;
    QAction *muteAct;
    QTimer *clickTimer, *idleTimer;
    Mpris *mpris = nullptr;
    QTimer *statsTimer = nullptr;
    QMenu *favoritesMenu = nullptr, *audioDeviceMenu = nullptr;
    QActionGroup *cropGroup = nullptr; // radio items of View > Video > Crop
    QSize srcSize;                     // the file's size before any crop
    double pendingSeek = -1;           // favorites: seek here once the file has loaded
    bool hideMenu = false, hideControls = false, hideStatus = false;
    QAction *recordAct = nullptr;    // Play > Record Stream
    QAction *seriesAct = nullptr;    // File > Screenshot Series
    QTimer *seriesTimer = nullptr;
    QMenu *bookmarksMenu = nullptr;  // Play > Bookmarks
    int current = -1;
    QList<double> chapters; // start times in seconds
    QString folderNeighbors[2]; // previous/next file in the folder of a lone playlist entry
    double duration = 0;
    bool stopped = true;
    enum { RepeatOff, RepeatFile, RepeatPlaylist } repeat = RepeatOff;
    friend class PlayerTest;
    friend class Mpris;
    friend class MprisRoot;
    friend class MprisPlayer;
};
