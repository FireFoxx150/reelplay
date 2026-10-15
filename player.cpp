#include "player.h"
#include "mpris.h"
#include <QActionGroup>
#include <QApplication>
#include <QCollator>
#include <QContextMenuEvent>
#include <QDateTime>
#include <QRandomGenerator>
#include <QTextStream>
#include <QToolTip>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QProxyStyle>
#include <QRegularExpression>
#include <QUrl>
#include <optional>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QSlider>
#include <QStatusBar>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

// Left click jumps the slider straight to the clicked point, like MPC's seek bar.
struct JumpStyle : QProxyStyle {
    int styleHint(StyleHint h, const QStyleOption *o, const QWidget *w, QStyleHintReturn *r) const override
    {
        return h == SH_Slider_AbsoluteSetButtons ? Qt::LeftButton : QProxyStyle::styleHint(h, o, w, r);
    }
};

// VLC 3.0's video + audio extensions (include/vlc_interface.h), minus MIDI/SID/.bin,
// which libmpv can't play.
static const QStringList mediaFilters = {
    "*.3g2", "*.3gp", "*.3gp2", "*.3gpp", "*.amv", "*.asf", "*.avi", "*.bik", "*.crf", "*.dav",
    "*.divx", "*.drc", "*.dv", "*.dvr-ms", "*.evo", "*.f4v", "*.flv", "*.gvi", "*.gxf", "*.iso",
    "*.m1v", "*.m2v", "*.m2t", "*.m2ts", "*.m4v", "*.mkv", "*.mov", "*.mp2", "*.mp2v", "*.mp4",
    "*.mp4v", "*.mpe", "*.mpeg", "*.mpeg1", "*.mpeg2", "*.mpeg4", "*.mpg", "*.mpv2", "*.mts",
    "*.mtv", "*.mxf", "*.mxg", "*.nsv", "*.nuv", "*.ogg", "*.ogm", "*.ogv", "*.ogx", "*.ps",
    "*.rec", "*.rm", "*.rmvb", "*.rpl", "*.thp", "*.tod", "*.tp", "*.ts", "*.tts", "*.txd",
    "*.vob", "*.vro", "*.webm", "*.wm", "*.wmv", "*.wtv", "*.xesc",
    "*.3ga", "*.669", "*.a52", "*.aac", "*.ac3", "*.adt", "*.adts", "*.aif", "*.aifc", "*.aiff",
    "*.amb", "*.amr", "*.aob", "*.ape", "*.au", "*.awb", "*.caf", "*.dts", "*.flac", "*.it",
    "*.m4a", "*.m4b", "*.m4p", "*.m5p", "*.mka", "*.mlp", "*.mod", "*.mpa", "*.mp1", "*.mp3",
    "*.mpc", "*.mpga", "*.mus", "*.oga", "*.oma", "*.opus", "*.qcp", "*.ra", "*.s3m", "*.spx",
    "*.tak", "*.thd", "*.tta", "*.voc", "*.vqf", "*.w64", "*.wav", "*.wma", "*.wv", "*.xa", "*.xm"};
static const QStringList playlistFilters = {
    "*.m3u", "*.m3u8", "*.pls", "*.xspf", "*.asx", "*.wax", "*.wvx", "*.ram", "*.wpl", "*.cue", "*.sdp", "*.ifo"};
static const QString openFilter = "Media files (" + (mediaFilters + playlistFilters).join(' ')
    + ");;Playlists (" + playlistFilters.join(' ') + ");;All files (*)";

// Media files in a folder, in natural order ("ep2" before "ep10").
static QStringList mediaInFolder(const QDir &dir)
{
    QStringList names = dir.entryList(mediaFilters, QDir::Files);
    QCollator natural;
    natural.setNumericMode(true);
    std::sort(names.begin(), names.end(), natural);
    return names;
}

// Entries of a playlist file we expand ourselves, or nullopt if it isn't one.
static std::optional<QStringList> readPlaylist(const QFileInfo &fi)
{
    static const QStringList kinds = {"m3u", "m3u8", "pls", "xspf", "asx", "wax", "wvx", "wpl", "ram"};
    static const QStringList xmlKinds = {"xspf", "asx", "wax", "wvx", "wpl"};
    QString ext = fi.suffix().toLower();
    if (!kinds.contains(ext) || !fi.isFile())
        return std::nullopt;
    QFile in(fi.filePath());
    if (!in.open(QIODevice::ReadOnly))
        return QStringList();
    QString text = QString::fromUtf8(in.readAll());
    if (text.contains("#EXT-X-"))
        return std::nullopt; // HLS manifest: one stream, mpv plays it as is

    QRegularExpression re;
    if (ext == "pls")
        re.setPattern("^\\s*File\\d+\\s*=\\s*(.+?)\\s*$");
    else if (ext == "xspf")
        re.setPattern("<location>\\s*(.+?)\\s*</location>");
    else if (ext == "asx" || ext == "wax" || ext == "wvx")
        re.setPattern("<ref\\s+href\\s*=\\s*\"([^\"]+)\"");
    else if (ext == "wpl")
        re.setPattern("<media\\s+src\\s*=\\s*\"([^\"]+)\"");
    else
        re.setPattern("^\\s*([^#\\s].*?)\\s*$"); // m3u, ram: one entry per line
    re.setPatternOptions(QRegularExpression::CaseInsensitiveOption | QRegularExpression::MultilineOption);

    QStringList out;
    for (auto it = re.globalMatch(text); it.hasNext();) {
        QString e = it.next().captured(1);
        if (xmlKinds.contains(ext))
            e.replace("&lt;", "<").replace("&gt;", ">").replace("&quot;", "\"").replace("&apos;", "'").replace("&amp;", "&");
        if (e.startsWith("file://", Qt::CaseInsensitive))
            e = QUrl(e).toLocalFile();
        else if (!e.contains("://") && QDir::isRelativePath(e))
            e = fi.dir().filePath(e);
        out << e;
    }
    return out;
}

// "dvd://" or "bd://" if a disc image holds a DVD or Blu-ray, else empty.
static QString discImageKind(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QByteArray head = f.read(32 << 20); // the root directory sits near the start of the image
    if (head.contains("VIDEO_TS"))
        return "dvd://";
    if (head.contains("BDMV") || head.contains(QByteArray("\0B\0D\0M\0V", 8)))
        return "bd://";
    return {};
}

static QString fmtTime(double s)
{
    int t = s > 0 ? int(s) : 0;
    return QString::asprintf("%02d:%02d:%02d", t / 3600, t / 60 % 60, t % 60);
}

Player::Player()
{
    setWindowTitle("newmpc-be");
    setAcceptDrops(true);

    mpv = mpv_create();
    if (!mpv)
        qFatal("mpv_create failed");

    // Wayland can't embed another process's window, so draw through the render API there.
    // On X11 keep embedding: a QOpenGLWidget costs 0.5-1 s of GL setup before the window
    // even appears (measured on NVIDIA). NEWMPC_RENDER=gl forces the render API for testing.
    if (QGuiApplication::platformName() == "wayland" || qgetenv("NEWMPC_RENDER") == "gl") {
        video = glVideo = new VideoWidget(mpv);
    } else {
        video = new QWidget;
        video->setAttribute(Qt::WA_NativeWindow);
        video->setAttribute(Qt::WA_DontCreateNativeAncestors);
        video->setStyleSheet("background: black");
    }
    video->setMinimumSize(320, 180);
    video->installEventFilter(this);

    // Delay the click-to-pause so a double-click (fullscreen) doesn't also pause/unpause.
    clickTimer = new QTimer(this);
    clickTimer->setSingleShot(true);
    clickTimer->setInterval(QApplication::doubleClickInterval());
    connect(clickTimer, &QTimer::timeout, this, [this] { togglePause(); });

    // Fullscreen: hide cursor and controls after the mouse rests.
    video->setMouseTracking(true);
    idleTimer = new QTimer(this);
    idleTimer->setSingleShot(true);
    idleTimer->setInterval(2000);
    connect(idleTimer, &QTimer::timeout, this, [this] {
        if (!isFullScreen())
            return;
        video->setCursor(Qt::BlankCursor);
        if (!controls->underMouse())
            controls->hide();
    });

    // Control bar under the video, MPC-style: seek bar on top, buttons + volume below.
    seekBar = new QSlider(Qt::Horizontal);
    seekBar->setFocusPolicy(Qt::NoFocus);
    seekBar->setEnabled(false);
    auto *jump = new JumpStyle;
    jump->setParent(seekBar);
    seekBar->setStyle(jump);
    seekBar->setMouseTracking(true);
    seekBar->installEventFilter(this); // hover tooltip
    connect(seekBar, &QSlider::sliderMoved, this, [this](int v) {
        command({"seek", QByteArray::number(v / 10.0), "absolute"});
    });

    auto button = [this](QStyle::StandardPixmap icon, const QString &tip, std::function<void()> fn) {
        auto *b = new QToolButton;
        b->setIcon(style()->standardIcon(icon));
        b->setToolTip(tip);
        b->setFocusPolicy(Qt::NoFocus);
        b->setAutoRaise(true);
        connect(b, &QToolButton::clicked, this, fn);
        return b;
    };

    volBar = new QSlider(Qt::Horizontal);
    volBar->setRange(0, 100);
    volBar->setFixedWidth(90);
    volBar->setFocusPolicy(Qt::NoFocus);
    connect(volBar, &QSlider::valueChanged, this, [this](int v) {
        command({"set", "volume", QByteArray::number(v)});
    });
    timeLabel = new QLabel(fmtTime(0) + " / " + fmtTime(0));

    auto *row = new QHBoxLayout;
    row->setContentsMargins(4, 0, 4, 0);
    row->addWidget(button(QStyle::SP_MediaPlay, "Play", [this] { play(); }));
    row->addWidget(button(QStyle::SP_MediaPause, "Pause", [this] { command({"set", "pause", "yes"}); }));
    row->addWidget(button(QStyle::SP_MediaStop, "Stop", [this] { stop(); }));
    row->addWidget(button(QStyle::SP_MediaSkipBackward, "Previous", [this] { skipFile(-1); }));
    row->addWidget(button(QStyle::SP_MediaSkipForward, "Next", [this] { skipFile(1); }));
    row->addStretch();
    row->addWidget(button(QStyle::SP_MediaVolume, "Mute", [this] { command({"cycle", "mute"}); }));
    row->addWidget(volBar);

    controls = new QWidget;
    auto *col = new QVBoxLayout(controls);
    col->setContentsMargins(0, 2, 0, 2);
    col->setSpacing(0);
    col->addWidget(seekBar);
    col->addLayout(row);

    auto *central = new QWidget;
    auto *lay = new QVBoxLayout(central);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    lay->addWidget(video, 1);
    lay->addWidget(controls);
    setCentralWidget(central);

    statusLabel = new QLabel("Stopped");
    statusBar()->addWidget(statusLabel, 1);
    statusBar()->addPermanentWidget(timeLabel);

    playlist = new QListWidget;
    connect(playlist, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *it) {
        playIndex(playlist->row(it));
    });
    playlist->setDragDropMode(QAbstractItemView::InternalMove);
    playlist->setSelectionMode(QAbstractItemView::ExtendedSelection);
    playlist->setContextMenuPolicy(Qt::ActionsContextMenu);
    connect(playlist->model(), &QAbstractItemModel::rowsMoved, this, &Player::syncCurrent);
    connect(playlist->model(), &QAbstractItemModel::rowsRemoved, this, &Player::syncCurrent);
    connect(playlist->model(), &QAbstractItemModel::rowsInserted, this, &Player::syncCurrent);
    connect(playlist->model(), &QAbstractItemModel::modelReset, this, &Player::syncCurrent);
    auto plAct = [this](const QString &text, const QKeySequence &key, std::function<void()> fn) {
        auto *a = new QAction(text, playlist);
        a->setShortcut(key);
        a->setShortcutContext(Qt::WidgetShortcut);
        connect(a, &QAction::triggered, this, fn);
        playlist->addAction(a);
    };
    plAct("&Remove", QKeySequence::Delete, [this] { qDeleteAll(playlist->selectedItems()); });
    plAct("Rando&mize", QKeySequence(), [this] {
        QList<QListWidgetItem *> items;
        while (playlist->count())
            items << playlist->takeItem(0);
        std::shuffle(items.begin(), items.end(), *QRandomGenerator::global());
        for (auto *it : items)
            playlist->addItem(it);
    });
    plAct("&Save Playlist...", QKeySequence::Save, [this] {
        QString f = QFileDialog::getSaveFileName(this, "Save Playlist", "playlist.m3u", "Playlists (*.m3u *.m3u8)");
        if (!f.isEmpty()) savePlaylist(f);
    });
    plAct("&Clear", QKeySequence(), [this] { playlist->clear(); });
    dock = new QDockWidget("Playlist", this);
    dock->setObjectName("playlist"); // needed by saveState()
    dock->setWidget(playlist);
    addDockWidget(Qt::RightDockWidgetArea, dock);
    dock->hide();

    // Menus with MPC-BE default hotkeys.
    QMenu *file = menuBar()->addMenu("&File");
    act(file, "&Open File...", QKeySequence("Ctrl+O"), [this] {
        QStringList f = QFileDialog::getOpenFileNames(this, "Open", {}, openFilter);
        if (!f.isEmpty()) openFiles(f);
    });
    act(file, "Open &URL...", QKeySequence("Ctrl+U"), [this] {
        QString u = QInputDialog::getText(this, "Open URL", "URL:");
        if (!u.isEmpty()) openFiles({u});
    });
    act(file, "Open &Folder...", QKeySequence(), [this] {
        QString d = QFileDialog::getExistingDirectory(this, "Open Folder");
        if (!d.isEmpty()) openFiles({d});
    });
    act(file, "Open &DVD/BD...", QKeySequence("Ctrl+D"), [this] {
        QString d = QFileDialog::getExistingDirectory(this, "Open DVD/BD (disc or folder with VIDEO_TS/BDMV)");
        if (!d.isEmpty()) openFiles({d});
    });
    act(file, "Open Audio &CD", QKeySequence(), [this] { openFiles({"cdda://"}); });
    recentMenu = file->addMenu("&Recent Files");
    connect(recentMenu, &QMenu::aboutToShow, this, [this] {
        recentMenu->clear();
        const QStringList recent = QSettings().value("recent").toStringList();
        for (const QString &r : recent)
            connect(recentMenu->addAction(r), &QAction::triggered, this, [this, r] { openFiles({r}); });
        recentMenu->addSeparator();
        QAction *clear = recentMenu->addAction("&Clear list");
        clear->setEnabled(!recent.isEmpty());
        connect(clear, &QAction::triggered, this, [] { QSettings().remove("recent"); });
    });
    file->addSeparator();
    act(file, "&Add to Playlist...", QKeySequence(), [this] {
        QStringList f = QFileDialog::getOpenFileNames(this, "Add", {}, openFilter);
        if (!f.isEmpty()) openFiles(f, true);
    });
    act(file, "Load &Subtitle...", QKeySequence("Ctrl+L"), [this] {
        QString f = QFileDialog::getOpenFileName(this, "Load Subtitle", {}, "Subtitles (*.srt *.ass *.ssa *.sub *.idx *.vtt *.smi *.sami *.txt *.aqt *.jss *.psb *.rt *.stl *.usf *.pjs *.mpl2 *.mks *.tt *.ttml *.dfxp *.scc *.utf *.dks *.cdg);;All files (*)");
        if (!f.isEmpty()) command({"sub-add", f.toUtf8(), "select"});
    });
    act(file, "Save &Image...", QKeySequence("Alt+I"), [this] {
        if (stopped) return;
        QString f = QFileDialog::getSaveFileName(this, "Save Image", autoScreenshotPath(), "Images (*.png *.jpg)");
        if (!f.isEmpty()) screenshot(f);
    });
    act(file, "Save Image (&Auto)", QKeySequence("F5"), [this] { if (!stopped) screenshot(autoScreenshotPath()); });
    act(file, "&Properties", QKeySequence("Shift+F10"), [this] {
        if (!stopped) QMessageBox::information(this, "Properties", propertiesText());
    });
    file->addSeparator();
    act(file, "&Close", QKeySequence("Ctrl+C"), [this] { savePosition(); stop(); });
    file->addSeparator();
    act(file, "E&xit", QKeySequence("Alt+X"), [this] { close(); });

    QMenu *view = menuBar()->addMenu("&View");
    act(view, "&Playlist", QKeySequence("Ctrl+7"), [this] { dock->setVisible(!dock->isVisible()); });
    act(view, "&Fullscreen", QKeySequence("Alt+Return"), [this] { toggleFullscreen(); });
    act(view, "Exit Fullscreen", QKeySequence("Esc"), [this] { if (isFullScreen()) toggleFullscreen(); });
    act(view, "&Options...", QKeySequence("O"), [this] { openOptions(); });
    view->addSeparator();
    QMenu *zoomMenu = view->addMenu("&Zoom");
    act(zoomMenu, "50%", QKeySequence("Alt+1"), [this] { zoom(0.5); });
    act(zoomMenu, "100%", QKeySequence("Alt+2"), [this] { zoom(1); });
    act(zoomMenu, "200%", QKeySequence("Alt+3"), [this] { zoom(2); });
    QMenu *aspectMenu = view->addMenu("&Aspect Ratio");
    auto *aspectGroup = new QActionGroup(aspectMenu);
    for (auto [label, value] : {std::pair{"Default", "-1"}, {"4:3", "4:3"}, {"16:9", "16:9"}, {"1.85:1", "1.85"}, {"2.35:1", "2.35"}}) {
        QAction *a = aspectMenu->addAction(label);
        a->setCheckable(true);
        a->setChecked(QByteArray(value) == "-1");
        aspectGroup->addAction(a);
        connect(a, &QAction::triggered, this, [this, value] { command({"set", "video-aspect-override", value}); });
    }

    QMenu *play = menuBar()->addMenu("&Play");
    act(play, "&Play/Pause", QKeySequence("Space"), [this] { togglePause(); });
    act(play, "&Stop", QKeySequence("."), [this] { stop(); });
    play->addSeparator();
    act(play, "Frame Step", QKeySequence("Ctrl+Right"), [this] { command({"frame-step"}); });
    act(play, "Frame Step Back", QKeySequence("Ctrl+Left"), [this] { command({"frame-back-step"}); });
    act(play, "Jump Forward", QKeySequence("Right"), [this] { command({"seek", "5"}); });
    act(play, "Jump Backward", QKeySequence("Left"), [this] { command({"seek", "-5"}); });
    act(play, "Jump Forward (large)", QKeySequence("Shift+Right"), [this] { command({"seek", "30"}); });
    act(play, "Jump Backward (large)", QKeySequence("Shift+Left"), [this] { command({"seek", "-30"}); });
    play->addSeparator();
    QMenu *speed = play->addMenu("Playback &Rate");
    act(speed, "&Increase", QKeySequence("Ctrl+Up"), [this] { osd(QString::asprintf("Speed: %.2fx", step("speed", 0.1, 0.25, 4))); });
    act(speed, "&Decrease", QKeySequence("Ctrl+Down"), [this] { osd(QString::asprintf("Speed: %.2fx", step("speed", -0.1, 0.25, 4))); });
    act(speed, "&Reset", QKeySequence("Ctrl+R"), [this] { osd(QString::asprintf("Speed: %.2fx", step("speed", 0, 1, 1))); });
    QMenu *sync = play->addMenu("S&ync");
    act(sync, "Subtitle Delay -100 ms", QKeySequence("F1"), [this] { osd(QString::asprintf("Subtitle delay: %.0f ms", step("sub-delay", -0.1, -600, 600) * 1000)); });
    act(sync, "Subtitle Delay +100 ms", QKeySequence("F2"), [this] { osd(QString::asprintf("Subtitle delay: %.0f ms", step("sub-delay", 0.1, -600, 600) * 1000)); });
    act(sync, "Audio Delay -10 ms", QKeySequence(Qt::KeypadModifier | Qt::Key_Minus), [this] { osd(QString::asprintf("Audio delay: %.0f ms", step("audio-delay", -0.01, -600, 600) * 1000)); });
    act(sync, "Audio Delay +10 ms", QKeySequence(Qt::KeypadModifier | Qt::Key_Plus), [this] { osd(QString::asprintf("Audio delay: %.0f ms", step("audio-delay", 0.01, -600, 600) * 1000)); });
    QMenu *ab = play->addMenu("A-&B Repeat");
    act(ab, "Set Marker &A", QKeySequence("["), [this] { command({"set", "ab-loop-a", prop("time-pos").toUtf8()}); osd("A marker set"); });
    act(ab, "Set Marker &B", QKeySequence("]"), [this] { command({"set", "ab-loop-b", prop("time-pos").toUtf8()}); osd("B marker set"); });
    act(ab, "&Clear Markers", QKeySequence("\\"), [this] { command({"set", "ab-loop-a", "no"}); command({"set", "ab-loop-b", "no"}); osd("A-B repeat off"); });
    QMenu *repeatMenu = play->addMenu("R&epeat");
    auto *repeatGroup = new QActionGroup(repeatMenu);
    repeat = decltype(repeat)(QSettings().value("repeat", RepeatOff).toInt());
    for (auto [label, mode] : {std::pair{"&Off", RepeatOff}, {"Repeat &File", RepeatFile}, {"Repeat &Playlist", RepeatPlaylist}}) {
        QAction *a = repeatMenu->addAction(label);
        a->setCheckable(true);
        a->setChecked(mode == repeat);
        repeatGroup->addAction(a);
        connect(a, &QAction::triggered, this, [this, mode] {
            repeat = mode;
            QSettings().setValue("repeat", int(mode));
            command({"set", "loop-file", mode == RepeatFile ? "inf" : "no"});
        });
    }
    play->addSeparator();
    act(play, "Next File", QKeySequence("PgDown"), [this] { skipFile(1); });
    act(play, "Previous File", QKeySequence("PgUp"), [this] { skipFile(-1); });
    play->addSeparator();
    audioMenu = play->addMenu("&Audio Track");
    subMenu = play->addMenu("S&ubtitle Track");
    play->addSeparator();
    QMenu *volume = play->addMenu("&Volume");
    act(volume, "&Up", QKeySequence("Up"), [this] { command({"add", "volume", "5"}); });
    act(volume, "&Down", QKeySequence("Down"), [this] { command({"add", "volume", "-5"}); });
    muteAct = act(volume, "&Mute", QKeySequence("Ctrl+M"), [this] { command({"cycle", "mute"}); });
    muteAct->setCheckable(true);

    QMenu *help = menuBar()->addMenu("&Help");
    act(help, "&About...", QKeySequence(), [this] {
        QMessageBox::about(this, "About", QString("newmpc-be\nMPC-BE-style player on Qt %1 + libmpv").arg(qVersion()));
    });

    // Right-click on the video: the full menu, like MPC-BE.
    contextMenu = new QMenu(this);
    for (QAction *m : menuBar()->actions())
        contextMenu->addAction(m);

    if (glVideo) {
        mpv_set_option_string(mpv, "vo", "libmpv");
    } else {
        mpv_set_option_string(mpv, "wid", QByteArray::number(qulonglong(video->winId())).constData());
        mpv_set_option_string(mpv, "input-vo-keyboard", "no"); // let Qt get keys and clicks
        mpv_set_option_string(mpv, "input-cursor", "no");
    }
    mpv_set_option_string(mpv, "idle", "yes");
    mpv_set_option_string(mpv, "input-default-bindings", "no");
    mpv_set_option_string(mpv, "osc", "no");
    mpv_set_option_string(mpv, "ytdl", "yes"); // YouTube & co. via yt-dlp, like VLC's lua scripts
    // Resume where each file was left off; our own dir so we don't mix with the mpv CLI.
    QByteArray watchDir = (QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/watch_later").toUtf8();
    mpv_set_option_string(mpv, "watch-later-dir", watchDir.constData());
    mpv_set_option_string(mpv, "watch-later-options", "start,aid,sid,sub-delay,audio-delay");
    mpv_set_option_string(mpv, "loop-file", repeat == RepeatFile ? "inf" : "no");
    if (mpv_initialize(mpv) < 0)
        qFatal("mpv_initialize failed");

    applySettings();
    mpris = new Mpris(this);

    // Media keys reach the window only when the desktop doesn't route them to MPRIS
    // (Cinnamon/GNOME/KDE do); these cover the rest while the window has focus.
    for (auto [key, fn] : {std::pair<Qt::Key, std::function<void()>>{Qt::Key_MediaTogglePlayPause, [this] { togglePause(); }},
                           {Qt::Key_MediaPlay, [this] { this->play(); }}, // `play` is also the Play menu here
                           {Qt::Key_MediaPause, [this] { pause(); }},
                           {Qt::Key_MediaStop, [this] { stop(); }},
                           {Qt::Key_MediaNext, [this] { skipFile(1); }},
                           {Qt::Key_MediaPrevious, [this] { skipFile(-1); }}}) {
        auto *a = new QAction(this);
        a->setShortcut(key);
        connect(a, &QAction::triggered, this, fn);
        addAction(a);
    }

    mpv_observe_property(mpv, 0, "time-pos", MPV_FORMAT_DOUBLE);
    mpv_observe_property(mpv, 0, "duration", MPV_FORMAT_DOUBLE);
    mpv_observe_property(mpv, 0, "pause", MPV_FORMAT_FLAG);
    mpv_observe_property(mpv, 0, "volume", MPV_FORMAT_DOUBLE);
    mpv_observe_property(mpv, 0, "mute", MPV_FORMAT_FLAG);
    mpv_observe_property(mpv, 0, "track-list", MPV_FORMAT_NONE);

    // mpv wakes us from its own thread; hop to the GUI thread to drain events.
    mpv_set_wakeup_callback(mpv, [](void *ctx) {
        QMetaObject::invokeMethod(static_cast<Player *>(ctx), [ctx] { static_cast<Player *>(ctx)->handleEvents(); }, Qt::QueuedConnection);
    }, this);
    QSettings settings;
    volBar->setValue(settings.value("volume", 100).toInt());
    if (!restoreGeometry(settings.value("geometry").toByteArray()))
        resize(960, 600);
    restoreState(settings.value("windowState").toByteArray());
}

Player::~Player()
{
    shutdown();
}

// Must run before Qt destroys the video widget's native window / GL context (Qt 6 does that
// on close()): otherwise mpv's video thread hits a dead X window and Xlib aborts the
// process, or the render context is freed without its GL context.
void Player::shutdown()
{
    if (!mpv)
        return;
    if (!stopped) {
        const char *args[] = {"write-watch-later-config", nullptr};
        mpv_command(mpv, args); // synchronous: must land before mpv is destroyed
    }
    if (glVideo)
        glVideo->release();
    mpv_terminate_destroy(mpv);
    mpv = nullptr;
}

void Player::changeEvent(QEvent *ev)
{
    if (ev->type() == QEvent::ActivationChange && isActiveWindow() && mpris)
        mpris->grabMediaKeys(); // the player used last gets the media keys
    QMainWindow::changeEvent(ev);
}

void Player::closeEvent(QCloseEvent *ev)
{
    if (isFullScreen())
        toggleFullscreen(); // save the windowed layout, not the fullscreen one
    QSettings settings;
    settings.setValue("volume", volBar->value());
    settings.setValue("geometry", saveGeometry());
    settings.setValue("windowState", saveState());
    shutdown();
    QMainWindow::closeEvent(ev);
}

// Synchronous so fast key-repeat never loses a step.
double Player::step(const char *name, double delta, double lo, double hi)
{
    double v = 0;
    if (!mpv || mpv_get_property(mpv, name, MPV_FORMAT_DOUBLE, &v) < 0)
        return 0;
    v = qBound(lo, std::round((v + delta) * 1000) / 1000, hi);
    mpv_set_property(mpv, name, MPV_FORMAT_DOUBLE, &v);
    return v;
}

void Player::osd(const QString &text)
{
    command({"show-text", text.toUtf8()});
}

void Player::zoom(double factor)
{
    int w = prop("dwidth").toInt(), h = prop("dheight").toInt();
    if (!w || !h || isFullScreen())
        return;
    if (isMaximized())
        showNormal();
    QSize chrome = size() - video->size();
    resize(QSize(int(w * factor), int(h * factor)) + chrome);
}

// Re-find the playing item after the playlist was reordered or edited.
void Player::syncCurrent()
{
    current = -1;
    for (int r = 0; r < playlist->count(); ++r)
        if (playlist->item(r)->data(Qt::UserRole + 2).toBool())
            current = r;
}

void Player::savePlaylist(const QString &path)
{
    QFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Save Playlist", "Cannot write " + path);
        return;
    }
    QTextStream ts(&out);
    ts << "#EXTM3U\n";
    for (int r = 0; r < playlist->count(); ++r)
        ts << playlist->item(r)->data(Qt::UserRole + 1).toString() << "\n";
}

QString Player::propertiesText()
{
    auto v = [this](const char *name) { QString s = prop(name); return s.isEmpty() ? QString("n/a") : s; };
    auto kbps = [this](const char *name) {
        double b = prop(name).toDouble();
        return b > 0 ? QString::number(qRound(b / 1000)) + " kbps" : QString("n/a");
    };
    double mb = prop("file-size").toDouble() / (1024 * 1024);
    return QString("File: %1\nPath: %2\nSize: %3\nDuration: %4\nContainer: %5\n\n"
                   "Video: %6, %7x%8, %9 fps, %10\n"
                   "Audio: %11, %12 Hz, %13 ch, %14")
        .arg(v("filename"), v("path"), mb > 0 ? QString::number(mb, 'f', 1) + " MB" : QString("n/a"),
             fmtTime(duration), v("file-format"), v("video-codec"), v("width"), v("height"),
             QString::number(prop("container-fps").toDouble(), 'f', 3))
        .arg(kbps("video-bitrate"), v("audio-codec-name"), v("audio-params/samplerate"),
             v("audio-params/channel-count"), kbps("audio-bitrate"));
}

QString Player::autoScreenshotPath()
{
    QString dir = QSettings().value("screenshotDir",
        QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) + "/newmpc-be").toString();
    QString base = QFileInfo(prop("filename")).completeBaseName();
    QString at = fmtTime(prop("time-pos").toDouble()).replace(':', '.');
    return dir + "/" + base + "_" + at + "_" + QDateTime::currentDateTime().toString("hhmmsszzz") + ".png";
}

void Player::screenshot(const QString &path)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    command({"screenshot-to-file", path.toUtf8(), "video"});
    osd("Saved: " + QFileInfo(path).fileName());
}

void Player::addRecent(const QString &path)
{
    QSettings settings;
    QStringList recent = settings.value("recent").toStringList();
    recent.removeAll(path);
    recent.prepend(path);
    settings.setValue("recent", recent.mid(0, 10));
}

void Player::savePosition()
{
    if (!stopped)
        command({"write-watch-later-config"});
}

QAction *Player::act(QMenu *menu, const QString &text, const QKeySequence &key, std::function<void()> fn)
{
    QAction *a = menu->addAction(text);
    a->setShortcut(key);
    a->setProperty("defaultKey", key);
    // Stable id for saved hotkeys, e.g. "Volume/Mute".
    a->setObjectName(QString(menu->title()).remove('&') + "/" + QString(text).remove('&').replace('/', '-'));
    connect(a, &QAction::triggered, this, fn);
    addAction(a); // keep hotkeys working while the menu bar is hidden in fullscreen
    return a;
}

void Player::command(std::initializer_list<QByteArray> args)
{
    if (!mpv)
        return;
    std::vector<const char *> argv;
    for (const QByteArray &a : args)
        argv.push_back(a.constData());
    argv.push_back(nullptr);
    mpv_command_async(mpv, 0, argv.data());
}

QString Player::prop(const char *name)
{
    if (!mpv)
        return {};
    char *s = mpv_get_property_string(mpv, name);
    QString r = QString::fromUtf8(s ? s : "");
    mpv_free(s);
    return r;
}

void Player::openFiles(const QStringList &files, bool append)
{
    if (!append)
        playlist->clear();
    int first = playlist->count();
    auto add = [this](const QString &label, const QString &url, const QString &source) {
        auto *it = new QListWidgetItem(label);
        it->setData(Qt::UserRole, url);
        it->setData(Qt::UserRole + 1, source); // what Recent Files reopens
        it->setToolTip(source);
        playlist->addItem(it);
    };
    QStringList queue = files;
    QSet<QString> seenLists; // guard against playlists that include themselves
    while (!queue.isEmpty()) {
        QString f = queue.takeFirst();
        if (f.startsWith("file://", Qt::CaseInsensitive)) // desktop files pass URLs (%U)
            f = QUrl(f).toLocalFile();
        QFileInfo fi(f);
        QString ext = fi.suffix().toLower();
        if (auto entries = readPlaylist(fi)) {
            if (!seenLists.contains(fi.absoluteFilePath()))
                queue = *entries + queue;
            seenLists.insert(fi.absoluteFilePath());
            continue;
        }
        if (ext == "ifo" && fi.isFile()) { // VIDEO_TS.IFO: open the whole DVD
            queue.prepend(fi.absolutePath());
            continue;
        }
        if (ext == "iso" && fi.isFile()) {
            QString kind = discImageKind(f);
            if (!kind.isEmpty()) {
                add((kind == "dvd://" ? "DVD: " : "Blu-ray: ") + fi.fileName(), kind, f);
                continue;
            }
        }
        if (!fi.isDir()) {
            add(fi.fileName().isEmpty() ? f : fi.fileName(), f, f);
            continue;
        }
        QDir dir(f);
        if (dir.exists("VIDEO_TS") || dir.dirName().compare("VIDEO_TS", Qt::CaseInsensitive) == 0) {
            add("DVD: " + fi.fileName(), "dvd://", f);
        } else if (dir.exists("BDMV")) {
            add("Blu-ray: " + fi.fileName(), "bd://", f);
        } else {
            for (const QString &n : mediaInFolder(dir))
                add(n, dir.filePath(n), dir.filePath(n));
        }
    }
    if (!append || stopped)
        playIndex(first);
}

void Player::playIndex(int i)
{
    if (i < 0 || i >= playlist->count())
        return;
    current = i;
    playlist->setCurrentRow(i);
    for (int r = 0; r < playlist->count(); ++r) {
        QListWidgetItem *it = playlist->item(r);
        QFont f = it->font();
        f.setBold(r == i);
        it->setFont(f);
        it->setData(Qt::UserRole + 2, r == i);
    }
    savePosition();
    stopped = false;
    QString url = playlist->item(i)->data(Qt::UserRole).toString();
    QString source = playlist->item(i)->data(Qt::UserRole + 1).toString();
    if (url == "dvd://")
        command({"set", "dvd-device", source.toUtf8()});
    else if (url == "bd://")
        command({"set", "bluray-device", source.toUtf8()});
    command({"loadfile", url.toUtf8()});
    addRecent(source);
    folderNeighbors[0] = folderNeighbors[1] = QString();
    QFileInfo fi(source);
    if (playlist->count() == 1 && fi.isFile()) {
        QStringList names = mediaInFolder(fi.dir());
        int k = names.indexOf(fi.fileName());
        if (k > 0)
            folderNeighbors[0] = fi.dir().filePath(names[k - 1]);
        if (k >= 0 && k + 1 < names.size())
            folderNeighbors[1] = fi.dir().filePath(names[k + 1]);
    }
    command({"set", "pause", "no"});
    setWindowTitle(playlist->item(i)->text() + " - newmpc-be");
}

void Player::play()
{
    if (stopped)
        playIndex(current < 0 ? 0 : current);
    else
        command({"set", "pause", "no"});
}

void Player::pause()
{
    if (!stopped)
        command({"set", "pause", "yes"});
}

// Next/previous playlist entry. With a single file open (e.g. from the file manager),
// step through that file's folder instead, like MPC-BE.
void Player::skipFile(int dir)
{
    if (playlist->count() == 1 && !folderNeighbors[dir > 0].isEmpty())
        openFiles({folderNeighbors[dir > 0]});
    else
        playIndex(current + dir);
}

bool Player::canSkip(int dir)
{
    if (playlist->count() == 1 && !folderNeighbors[dir > 0].isEmpty())
        return true;
    return current + dir >= 0 && current + dir < playlist->count();
}

void Player::togglePause()
{
    if (stopped)
        playIndex(current < 0 ? 0 : current);
    else
        command({"cycle", "pause"});
}

void Player::stop()
{
    stopped = true;
    command({"stop"});
    statusLabel->setText("Stopped");
    seekBar->setValue(0);
    seekBar->setEnabled(false);
    duration = 0;
    updateTime(0);
}

void Player::toggleFullscreen()
{
    bool fs = !isFullScreen();
    menuBar()->setVisible(!fs);
    statusBar()->setVisible(!fs);
    controls->setVisible(!fs);
    video->unsetCursor();
    if (fs) idleTimer->start(); else idleTimer->stop();
    static bool dockWasVisible = false;
    if (fs) {
        dockWasVisible = dock->isVisible();
        dock->hide();
        showFullScreen();
    } else {
        dock->setVisible(dockWasVisible);
        showNormal();
    }
}

void Player::updateTime(double pos)
{
    timeLabel->setText(fmtTime(pos) + " / " + fmtTime(duration));
    if (!seekBar->isSliderDown())
        seekBar->setValue(int(pos * 10));
}

void Player::rebuildTrackMenus()
{
    audioMenu->clear();
    subMenu->clear();
    auto *subGroup = new QActionGroup(subMenu);
    auto *audioGroup = new QActionGroup(audioMenu);
    QAction *off = subMenu->addAction("Disabled");
    off->setCheckable(true);
    off->setChecked(true);
    subGroup->addAction(off);
    connect(off, &QAction::triggered, this, [this] { command({"set", "sid", "no"}); });

    int n = prop("track-list/count").toInt();
    for (int i = 0; i < n; ++i) {
        QByteArray p = "track-list/" + QByteArray::number(i) + "/";
        QString type = prop(p + "type");
        if (type != "audio" && type != "sub")
            continue;
        QString id = prop(p + "id"), title = prop(p + "title"), lang = prop(p + "lang");
        QString label = id + ": " + (title.isEmpty() ? (lang.isEmpty() ? "Track" : lang) : title);
        if (!lang.isEmpty() && !title.isEmpty())
            label += " [" + lang + "]";
        bool isAudio = type == "audio";
        QAction *a = (isAudio ? audioMenu : subMenu)->addAction(label);
        a->setCheckable(true);
        a->setChecked(prop(p + "selected") == "yes");
        (isAudio ? audioGroup : subGroup)->addAction(a);
        QByteArray key = isAudio ? "aid" : "sid";
        connect(a, &QAction::triggered, this, [this, key, id] { command({"set", key, id.toUtf8()}); });
    }
}

void Player::handleEvents()
{
    while (mpv) {
        mpv_event *ev = mpv_wait_event(mpv, 0);
        if (ev->event_id == MPV_EVENT_NONE)
            break;
        if (ev->event_id == MPV_EVENT_PROPERTY_CHANGE) {
            auto *p = static_cast<mpv_event_property *>(ev->data);
            QByteArray name = p->name;
            if (name == "track-list") {
                rebuildTrackMenus();
            } else if (p->format == MPV_FORMAT_NONE) {
                continue; // property unavailable (e.g. nothing loaded)
            } else if (name == "time-pos") {
                updateTime(*static_cast<double *>(p->data));
            } else if (name == "duration") {
                duration = *static_cast<double *>(p->data);
                seekBar->setRange(0, int(duration * 10));
                seekBar->setEnabled(duration > 0);
            } else if (name == "pause") {
                if (!stopped)
                    statusLabel->setText(*static_cast<int *>(p->data) ? "Paused" : "Playing");
            } else if (name == "volume") {
                QSignalBlocker b(volBar);
                volBar->setValue(int(*static_cast<double *>(p->data)));
            } else if (name == "mute") {
                muteAct->setChecked(*static_cast<int *>(p->data));
            }
        } else if (ev->event_id == MPV_EVENT_START_FILE) {
            statusLabel->setText("Opening...");
        } else if (ev->event_id == MPV_EVENT_FILE_LOADED) {
            statusLabel->setText(prop("pause") == "yes" ? "Paused" : "Playing");
        } else if (ev->event_id == MPV_EVENT_END_FILE) {
            auto *e = static_cast<mpv_event_end_file *>(ev->data);
            if (e->reason == MPV_END_FILE_REASON_ERROR)
                statusBar()->showMessage(QString("Cannot play: ") + mpv_error_string(e->error), 5000);
            if (e->reason == MPV_END_FILE_REASON_EOF || e->reason == MPV_END_FILE_REASON_ERROR) {
                if (current + 1 < playlist->count())
                    playIndex(current + 1);
                else if (repeat == RepeatPlaylist && e->reason == MPV_END_FILE_REASON_EOF)
                    playIndex(0);
                else
                    stop();
            }
        } else if (ev->event_id == MPV_EVENT_PLAYBACK_RESTART) {
            mpris->seeked(prop("time-pos").toDouble());
        } else if (ev->event_id == MPV_EVENT_SHUTDOWN) {
            close();
        }
    }
    mpris->update();
}

bool Player::eventFilter(QObject *obj, QEvent *ev)
{
    if (obj == video) {
        switch (ev->type()) {
        case QEvent::MouseMove:
            if (isFullScreen()) {
                video->unsetCursor();
                idleTimer->start();
                // Controls slide in when the mouse reaches the bottom edge.
                if (static_cast<QMouseEvent *>(ev)->position().y() >= video->height() - 40)
                    controls->show();
                else
                    controls->hide();
            }
            return false;
        case QEvent::MouseButtonPress:
            if (static_cast<QMouseEvent *>(ev)->button() == Qt::LeftButton)
                clickTimer->start();
            return true;
        case QEvent::MouseButtonDblClick:
            clickTimer->stop();
            toggleFullscreen();
            return true;
        case QEvent::Wheel:
            command({"add", "volume", static_cast<QWheelEvent *>(ev)->angleDelta().y() > 0 ? "5" : "-5"});
            return true;
        case QEvent::ContextMenu:
            contextMenu->popup(static_cast<QContextMenuEvent *>(ev)->globalPos());
            return true;
        default:
            break;
        }
    } else if (obj == seekBar && ev->type() == QEvent::MouseMove && duration > 0) {
        auto *me = static_cast<QMouseEvent *>(ev);
        int v = QStyle::sliderValueFromPosition(seekBar->minimum(), seekBar->maximum(), int(me->position().x()), seekBar->width());
        QToolTip::showText(me->globalPosition().toPoint(), fmtTime(v / 10.0), seekBar);
    }
    return QMainWindow::eventFilter(obj, ev);
}

void Player::dragEnterEvent(QDragEnterEvent *ev)
{
    if (ev->mimeData()->hasUrls())
        ev->acceptProposedAction();
}

void Player::dropEvent(QDropEvent *ev)
{
    QStringList files;
    for (const QUrl &u : ev->mimeData()->urls())
        files << (u.isLocalFile() ? u.toLocalFile() : u.toString());
    openFiles(files);
}
