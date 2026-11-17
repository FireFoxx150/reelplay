#include "player.h"
#include "mpris.h"
#include <QActionGroup>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QCheckBox>
#include <QGridLayout>
#include <QPushButton>
#include <QWindow>
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
#include <QPainter>
#include <cmath>
#include <QProcess>
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

// Seek bar with chapter tick marks above and below the groove, like MPC-BE's.
struct ChapterSlider : QSlider {
    using QSlider::QSlider;
    QList<int> marks; // in slider units
    void paintEvent(QPaintEvent *e) override
    {
        QSlider::paintEvent(e);
        if (marks.isEmpty() || maximum() <= minimum())
            return;
        QPainter p(this);
        p.setPen(palette().color(QPalette::WindowText));
        for (int m : marks) {
            int x = QStyle::sliderPositionFromValue(minimum(), maximum(), m, width());
            p.drawLine(x, 0, x, 3);
            p.drawLine(x, height() - 4, x, height() - 1);
        }
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
    // Explicit locale: the default (C on CI) collator sorted "ep10" before "ep2".
    QCollator natural(QLocale::English);
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

// The id a hotkey is saved under, e.g. "Volume/Mute". Options > Keys lists actions by it.
static QString hotkeyId(const QString &group, const QString &text)
{
    return QString(group).remove('&') + "/" + QString(text).remove('&').replace('/', '-');
}

Player::Player()
{
    setWindowTitle("Reelplay");
    setAcceptDrops(true);

    mpv = mpv_create();
    if (!mpv)
        qFatal("mpv_create failed");

    // Wayland can't embed another process's window, so draw through the render API there.
    // On X11 keep embedding: a QOpenGLWidget costs 0.5-1 s of GL setup before the window
    // even appears (measured on NVIDIA). REELPLAY_RENDER=gl forces the render API for testing.
    if (QGuiApplication::platformName() == "wayland" || qgetenv("REELPLAY_RENDER") == "gl") {
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

    statsTimer = new QTimer(this);
    statsTimer->setInterval(500);
    connect(statsTimer, &QTimer::timeout, this, &Player::updateStats);

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
    seekBar = new ChapterSlider(Qt::Horizontal);
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
        a->setProperty("defaultKey", key);
        if (!key.isEmpty())
            a->setObjectName(hotkeyId("Playlist", text));
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
    seriesAct = file->addAction("Screenshot Series...");
    seriesAct->setCheckable(true);
    connect(seriesAct, &QAction::toggled, this, [this](bool on) {
        if (!on) {
            stopScreenshotSeries();
            return;
        }
        bool ok = false;
        const int secs = QInputDialog::getInt(this, "Screenshot Series", "Seconds between screenshots:", 10, 1, 3600, 1, &ok);
        if (!ok) {
            QSignalBlocker block(seriesAct);
            seriesAct->setChecked(false);
            return;
        }
        startScreenshotSeries(secs);
    });
    addAction(seriesAct);
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

    // Checkable items keep their state in the menu; the toggled signal does the work.
    auto checkable = [this](QMenu *menu, const QString &text, const QKeySequence &key, bool checked, std::function<void(bool)> fn) {
        QAction *a = menu->addAction(text);
        a->setCheckable(true);
        a->setChecked(checked);
        a->setShortcut(key);
        a->setProperty("defaultKey", key);
        if (!key.isEmpty()) // only keyed items are listed in Options > Keys
            a->setObjectName(hotkeyId(menu->title(), text));
        connect(a, &QAction::toggled, this, fn);
        addAction(a); // keep hotkeys working while the menu bar is hidden
        return a;
    };
    view->addSeparator();
    checkable(view, "Always on &Top", QKeySequence("Ctrl+A"), false, [this](bool on) { setAlwaysOnTop(on); });
    checkable(view, "Hide &Menu Bar", QKeySequence("Ctrl+0"), false, [this](bool on) { hideMenu = on; applyChrome(isFullScreen()); });
    checkable(view, "Hide Con&trols", QKeySequence("Ctrl+1"), false, [this](bool on) { hideControls = on; applyChrome(isFullScreen()); });
    checkable(view, "Hide &Status Bar", QKeySequence("Ctrl+2"), false, [this](bool on) { hideStatus = on; applyChrome(isFullScreen()); });
    checkable(view, "&Statistics", QKeySequence("Ctrl+J"), false, [this](bool on) { toggleStats(on); });
    checkable(view, "&Visualizer (audio files)", QKeySequence(), QSettings().value("visualizer", false).toBool(), [this](bool on) {
        QSettings().setValue("visualizer", on);
        applyVisualizer();
    });
    QMenu *videoMenu = view->addMenu("&Video");
    act(videoMenu, "&Adjust Colors...", QKeySequence(), [this] { openVideoAdjust(); });
    act(videoMenu, "Rotate &Clockwise", QKeySequence(), [this] { rotateVideo(90); });
    act(videoMenu, "Rotate &Counter-clockwise", QKeySequence(), [this] { rotateVideo(-90); });
    checkable(videoMenu, "Flip &Horizontal", QKeySequence(), QSettings().value("video/flip", false).toBool(), [this](bool on) { setVideoFilter("flip", "hflip", on); });
    checkable(videoMenu, "&Deinterlace", QKeySequence(), QSettings().value("video/deinterlace", false).toBool(), [this](bool on) {
        command({"set", "deinterlace", on ? "yes" : "no"});
        QSettings().setValue("video/deinterlace", on);
    });
    QMenu *cropMenu = videoMenu->addMenu("&Crop");
    cropGroup = new QActionGroup(cropMenu);
    for (auto [label, aspect] : {std::pair{"No Crop", 0.0}, {"Crop 4:3", 4.0 / 3}, {"Crop 16:9", 16.0 / 9}, {"Crop 1.85:1", 1.85}, {"Crop 2.35:1", 2.35}}) {
        QAction *a = cropMenu->addAction(label);
        a->setCheckable(true);
        a->setChecked(aspect == 0);
        cropGroup->addAction(a);
        a->setData(aspect);
        connect(a, &QAction::triggered, this, [this, aspect] { setCrop(aspect); });
    }
    QMenu *scaleMenu = videoMenu->addMenu("&Scaling");
    auto *scaleGroup = new QActionGroup(scaleMenu);
    const QString scale = QSettings().value("scale", "bilinear").toString();
    for (auto [label, filter] : {std::pair{"&Default (Bilinear)", "bilinear"}, {"&Sharp (EWA Lanczos)", "ewa_lanczossharp"}, {"S&mooth (Spline36)", "spline36"}}) {
        QAction *a = scaleMenu->addAction(label);
        a->setCheckable(true);
        a->setChecked(scale == filter);
        scaleGroup->addAction(a);
        connect(a, &QAction::triggered, this, [this, filter] { setScale(filter); });
    }
    checkable(videoMenu, "&HDR Tone Mapping", QKeySequence(), QSettings().value("toneMap", true).toBool(), [this](bool on) { setToneMap(on); });
    checkable(videoMenu, "&Denoise", QKeySequence(), QSettings().value("video/denoise", false).toBool(), [this](bool on) { setVideoFilter("denoise", "hqdn3d", on); });
    checkable(videoMenu, "&Sharpen", QKeySequence(), QSettings().value("video/sharpen", false).toBool(), [this](bool on) { setVideoFilter("sharpen", "unsharp", on); });
    checkable(videoMenu, "&Smooth Motion", QKeySequence(), QSettings().value("video/smooth", false).toBool(), [this](bool on) { setSmoothMotion(on); });
    act(videoMenu, "Upscaling &Shader...", QKeySequence(), [this] { chooseShader(); });
    act(videoMenu, "Clear Upscaling Shader", QKeySequence(), [this] { setShader(QString()); });

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
    act(play, "&Go To...", QKeySequence("Ctrl+G"), [this] {
        if (stopped) return;
        bool ok;
        QString t = QInputDialog::getText(this, "Go To", "Time (hh:mm:ss):", QLineEdit::Normal,
                                          fmtTime(prop("time-pos").toDouble()), &ok);
        double secs = parseTime(t);
        if (ok && secs >= 0)
            command({"seek", QByteArray::number(secs), "absolute"});
        else if (ok)
            statusBar()->showMessage("Invalid time: " + t, 3000);
    });
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
    act(ab, "Save &Section...", QKeySequence(), [this] {
        if (stopped || current < 0 || prop("ab-loop-a") == "no" || prop("ab-loop-b") == "no") {
            osd("Set A and B markers on a file first");
            return;
        }
        const QString src = playlist->item(current)->data(Qt::UserRole + 1).toString();
        const QString out = QFileDialog::getSaveFileName(this, "Save Section", QFileInfo(src).completeBaseName() + "_section.mkv", "Media (*.mkv *.mp4)");
        if (!out.isEmpty())
            saveAbSection(out);
    });
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
    bookmarksMenu = play->addMenu("&Bookmarks");
    connect(bookmarksMenu, &QMenu::aboutToShow, this, &Player::rebuildBookmarksMenu);
    QAction *addBm = new QAction(this);
    addBm->setShortcut(QKeySequence("Ctrl+B"));
    addBm->setProperty("defaultKey", QKeySequence("Ctrl+B"));
    addBm->setObjectName("Bookmarks/Add Bookmark");
    connect(addBm, &QAction::triggered, this, &Player::addBookmark);
    addAction(addBm); // hotkey only; the menu builds its own entries

    recordAct = play->addAction("Record Stream (Experimental)...");
    recordAct->setCheckable(true);
    connect(recordAct, &QAction::toggled, this, [this](bool on) {
        if (!on) {
            stopRecording();
            return;
        }
        const QString path = QFileDialog::getSaveFileName(this, "Record Stream", {}, "Media (*.mkv *.mp4 *.ts *.webm);;All files (*)");
        if (path.isEmpty()) {
            QSignalBlocker block(recordAct);
            recordAct->setChecked(false);
            return;
        }
        startRecording(path);
    });
    addAction(recordAct);

    QMenu *afterMenu = play->addMenu("After Playback");
    auto *afterGroup = new QActionGroup(afterMenu);
    const QString after = QSettings().value("afterPlayback", "nothing").toString();
    for (auto [label, mode] : {std::pair{"&Do Nothing", "nothing"}, {"&Exit Player", "exit"}, {"&Shut Down", "poweroff"}, {"S&uspend", "suspend"}}) {
        QAction *a = afterMenu->addAction(label);
        a->setCheckable(true);
        a->setChecked(after == mode);
        afterGroup->addAction(a);
        connect(a, &QAction::triggered, this, [mode] { QSettings().setValue("afterPlayback", mode); });
    }
    play->addSeparator();
    act(play, "Next File", QKeySequence("PgDown"), [this] { skipFile(1); });
    act(play, "Previous File", QKeySequence("PgUp"), [this] { skipFile(-1); });
    chapterMenu = play->addMenu("C&hapters");
    connect(chapterMenu, &QMenu::aboutToShow, this, &Player::rebuildChapterMenu);
    act(play, "Next Chapter", QKeySequence("Ctrl+PgDown"), [this] { command({"add", "chapter", "1"}); });
    act(play, "Previous Chapter", QKeySequence("Ctrl+PgUp"), [this] { command({"add", "chapter", "-1"}); });
    act(play, "Skip Intro / Credits", QKeySequence("S"), [this] { skipIntro(); });
    play->addSeparator();
    audioMenu = play->addMenu("&Audio Track");
    subMenu = play->addMenu("S&ubtitle Track");
    play->addSeparator();
    QMenu *volume = play->addMenu("&Volume");
    act(volume, "&Up", QKeySequence("Up"), [this] { command({"add", "volume", "5"}); });
    act(volume, "&Down", QKeySequence("Down"), [this] { command({"add", "volume", "-5"}); });
    muteAct = act(volume, "&Mute", QKeySequence("Ctrl+M"), [this] { command({"cycle", "mute"}); });
    muteAct->setCheckable(true);

    QMenu *audioOut = play->addMenu("&Audio Output");
    audioDeviceMenu = audioOut->addMenu("&Device");
    connect(audioDeviceMenu, &QMenu::aboutToShow, this, &Player::rebuildAudioDeviceMenu);
    checkable(audioOut, "&Normalize Volume", QKeySequence(), QSettings().value("audioNormalize", false).toBool(), [this](bool on) {
        QSettings().setValue("audioNormalize", on);
        applyAudioFilters();
    });
    checkable(audioOut, "&S/PDIF Passthrough (AC3/DTS)", QKeySequence(), QSettings().value("passthrough", false).toBool(), [this](bool on) { setPassthrough(on); });
    QMenu *eqMenu = audioOut->addMenu("&Equalizer");
    QAction *openEq = eqMenu->addAction("&Open Equalizer...");
    connect(openEq, &QAction::triggered, this, &Player::openEqualizer);
    eqMenu->addSeparator();
    struct Preset { const char *name; QList<double> gains; };
    const QList<Preset> presets = {
        {"&Flat", {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}},
        {"&Bass Boost", {6, 5, 3, 0, 0, 0, 0, 0, 0, 0}},
        {"&Treble Boost", {0, 0, 0, 0, 0, 0, 0, 3, 4, 4}},
        {"&Voice", {-2, -2, 0, 0, 2, 3, 4, 2, 0, 0}},
    };
    for (const Preset &pr : presets) {
        QAction *a = eqMenu->addAction(pr.name);
        connect(a, &QAction::triggered, this, [this, gains = pr.gains] { setEqGains(gains); });
    }
    favoritesMenu = menuBar()->addMenu("F&avorites");
    connect(favoritesMenu, &QMenu::aboutToShow, this, &Player::rebuildFavoritesMenu);
    QAction *addFav = new QAction(this);
    addFav->setShortcut(QKeySequence("Ctrl+Shift+F"));
    addFav->setProperty("defaultKey", QKeySequence("Ctrl+Shift+F"));
    addFav->setObjectName("Favorites/Add Current File");
    connect(addFav, &QAction::triggered, this, &Player::addFavorite);
    addAction(addFav); // hotkey only; the menu builds its own entry

    QMenu *help = menuBar()->addMenu("&Help");
    act(help, "&About...", QKeySequence(), [this] {
        QMessageBox::about(this, "About", QString("Reelplay\nClassic-style media player on Qt %1 + libmpv").arg(qVersion()));
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
    // Load subtitles whose name contains the video's, also from Subs/ etc., like MPC-BE.
    mpv_set_option_string(mpv, "sub-auto", "fuzzy");
    mpv_set_option_string(mpv, "sub-file-paths", "Subs:subs:Subtitles:subtitles:Sub:sub");
    if (const QByteArray ao = qgetenv("REELPLAY_AO"); !ao.isEmpty()) // e.g. "null": no audio device
        mpv_set_option_string(mpv, "ao", ao.constData());
    if (const QByteArray vo = qgetenv("REELPLAY_VO"); !vo.isEmpty()) // e.g. "null": no video output (CI)
        mpv_set_option_string(mpv, "vo", vo.constData());
    mpv_set_option_string(mpv, "audio-display", "embedded-first"); // an audio file's cover art, if it has one
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
    mpv_observe_property(mpv, 0, "chapter-list", MPV_FORMAT_NONE);
    mpv_observe_property(mpv, 0, "chapter", MPV_FORMAT_INT64);

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
        QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) + "/reelplay").toString();
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
    a->setObjectName(hotkeyId(menu->title(), text));
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
    command({"set", "video-crop", ""}); // crop is per file
    if (cropGroup)
        cropGroup->actions().first()->setChecked(true);
    if (recordAct && recordAct->isChecked())
        stopRecording(); // a recording follows one file, never two
    previewCache.clear();
    hidePreview();
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
    setWindowTitle(playlist->item(i)->text() + " - Reelplay");
}

double parseTime(const QString &text)
{
    QStringList parts = text.trimmed().split(':');
    if (parts.size() > 3)
        return -1;
    double t = 0;
    for (const QString &part : parts) {
        bool ok;
        double v = part.toDouble(&ok);
        if (!ok || v < 0)
            return -1;
        t = t * 60 + v;
    }
    return t;
}

void Player::openFromOtherInstance(const QStringList &files)
{
    if (!files.isEmpty())
        openFiles(files);
    if (isMinimized())
        showNormal();
    raise();
    activateWindow();
}

void Player::readChapters()
{
    chapters.clear();
    int n = prop("chapter-list/count").toInt();
    for (int i = 0; i < n; ++i)
        chapters << prop(QByteArray("chapter-list/") + QByteArray::number(i) + "/time").toDouble();
    updateChapterMarks();
}

void Player::updateChapterMarks()
{
    QList<int> marks;
    if (duration > 0)
        for (double t : chapters)
            if (t > 0)
                marks << int(t * 10);
    static_cast<ChapterSlider *>(seekBar)->marks = marks;
    seekBar->update();
}

void Player::rebuildChapterMenu()
{
    chapterMenu->clear();
    if (chapters.isEmpty()) {
        chapterMenu->addAction("No chapters")->setEnabled(false);
        return;
    }
    int now = prop("chapter").toInt();
    for (int i = 0; i < chapters.size(); ++i) {
        QString title = prop(QByteArray("chapter-list/") + QByteArray::number(i) + "/title");
        QAction *a = chapterMenu->addAction(QString("%1. %2 (%3)").arg(i + 1)
            .arg(title.isEmpty() ? QString("Chapter %1").arg(i + 1) : title, fmtTime(chapters[i])));
        a->setCheckable(true);
        a->setChecked(i == now);
        connect(a, &QAction::triggered, this, [this, i] { command({"set", "chapter", QByteArray::number(i)}); });
    }
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

// View > Video > Scaling: mpv's upscaler, applied to every file.
void Player::setScale(const QString &filter)
{
    QSettings().setValue("scale", filter);
    command({"set", "scale", filter.toUtf8()});
}

void Player::setToneMap(bool on)
{
    QSettings().setValue("toneMap", on);
    command({"set", "tone-mapping", on ? "auto" : "clip"});
}

// View > Video > Smooth Motion: interpolates frames to the display rate. It costs GPU time.
void Player::setSmoothMotion(bool on)
{
    QSettings().setValue("video/smooth", on);
    command({"set", "interpolation", on ? "yes" : "no"});
    command({"set", "video-sync", on ? "display-resample" : "audio"});
}

// View > Video > Upscaling Shader: a GLSL file that mpv runs on each frame. An empty path clears it.
void Player::setShader(const QString &path)
{
    QSettings().setValue("video/shader", path);
    command({"set", "glsl-shaders", path.toUtf8()});
}

void Player::chooseShader()
{
    // Qt's own dialog: the GTK one that some desktops pick cannot be driven by the tests.
    const QString path = QFileDialog::getOpenFileName(this, "Upscaling Shader", {}, "GLSL shaders (*.glsl *.hook);;All files (*)", nullptr, QFileDialog::DontUseNativeDialog);
    if (!path.isEmpty())
        setShader(path);
}

// Passthrough sends AC3/DTS/TrueHD to the device undecoded. A device that can't take it
// gives silence or garbled sound, so it stays off unless the user turns it on.
void Player::setPassthrough(bool on)
{
    QSettings().setValue("passthrough", on);
    command({"set", "audio-spdif", on ? "ac3,eac3,dts,dts-hd,truehd" : ""});
}

// Skips the current chapter when its title marks it as an intro or credits. Automatic calls
// come on every chapter change, so they stay quiet when there is nothing to skip.
void Player::skipIntro(bool automatic)
{
    const int n = prop("chapter-list/count").toInt();
    const int c = prop("chapter").toInt();
    if (n == 0 || c < 0 || c >= n) {
        if (!automatic)
            osd("No chapters in this file");
        return;
    }
    const QString title = prop(QByteArray("chapter-list/") + QByteArray::number(c) + "/title");
    static const QRegularExpression skippable("intro|opening|recap|credit|ending", QRegularExpression::CaseInsensitiveOption);
    if (!skippable.match(title).hasMatch()) {
        if (!automatic)
            osd("Not an intro or credits chapter");
        return;
    }
    if (c + 1 < n)
        command({"set", "chapter", QByteArray::number(c + 1)});
    else
        command({"seek", "100", "absolute-percent"}); // credits run to the end
    osd("Skipped " + title);
}

// Bookmarks are times inside one file. One shared list, each entry tagged with its path.
void Player::addBookmark()
{
    if (stopped || current < 0)
        return;
    const QString path = playlist->item(current)->data(Qt::UserRole + 1).toString();
    QStringList list = {QString::number(prop("time-pos").toDouble(), 'f', 1) + "\t" + path};
    list += QSettings().value("bookmarks").toStringList();
    QSettings().setValue("bookmarks", list.mid(0, 500));
    statusBar()->showMessage("Bookmark added", 3000);
}

void Player::rebuildBookmarksMenu()
{
    bookmarksMenu->clear();
    const QString path = stopped || current < 0 ? QString() : playlist->item(current)->data(Qt::UserRole + 1).toString();
    QAction *add = bookmarksMenu->addAction("&Add Bookmark");
    add->setEnabled(!stopped);
    connect(add, &QAction::triggered, this, &Player::addBookmark);
    bookmarksMenu->addSeparator();
    int shown = 0;
    for (const QString &e : QSettings().value("bookmarks").toStringList()) {
        const int tab = e.indexOf('\t');
        if (e.mid(tab + 1) != path)
            continue;
        const double secs = e.left(tab).toDouble();
        QAction *a = bookmarksMenu->addAction(fmtTime(secs));
        connect(a, &QAction::triggered, this, [this, secs] { command({"seek", QByteArray::number(secs), "absolute"}); });
        ++shown;
    }
    if (!shown) {
        bookmarksMenu->addAction("No Bookmarks for This File")->setEnabled(false);
        return;
    }
    bookmarksMenu->addSeparator();
    QAction *clear = bookmarksMenu->addAction("&Clear Bookmarks for This File");
    connect(clear, &QAction::triggered, this, [path] {
        QStringList kept;
        for (const QString &e : QSettings().value("bookmarks").toStringList())
            if (e.mid(e.indexOf('\t') + 1) != path)
                kept << e;
        QSettings().setValue("bookmarks", kept);
    });
}

// mpv copies the stream without re-encoding. It is experimental: the file is written when
// recording stops, and a recording started mid-file can start with undecodable frames.
void Player::startRecording(const QString &path)
{
    command({"set", "stream-record", path.toUtf8()});
    QSignalBlocker block(recordAct);
    recordAct->setChecked(true);
    osd("Recording to " + QFileInfo(path).fileName() + ", saved when you stop");
}

void Player::stopRecording()
{
    command({"set", "stream-record", ""});
    QSignalBlocker block(recordAct);
    recordAct->setChecked(false);
    osd("Recording stopped");
}

// The visualiser draws a spectrum as the video of a file that has no video (audio files).
// The spectrum is the output of mpv's filter graph, so "vid" can read "no" while it runs.
// The file's own track list says whether there is real video.
void Player::applyVisualizer()
{
    bool video = false;
    const int n = prop("track-list/count").toInt();
    for (int i = 0; i < n && !video; ++i)
        video = prop(QByteArray("track-list/") + QByteArray::number(i) + "/type") == "video";
    const bool wanted = QSettings().value("visualizer", false).toBool() && !stopped && !video;
    const bool active = !prop("lavfi-complex").isEmpty();
    command({"set", "lavfi-complex", wanted ? "[aid1]asplit[ao][v];[v]showspectrum=s=640x360[vo]" : ""});
    // The graph takes the audio track (aid reads "no" while it runs), and clearing it does not give
    // the track back: playback stops, and a video's picture stays black. Select the first audio and
    // video track again, by id.
    auto reselect = [this, n](const char *type, const char *property) {
        for (int i = 0; i < n; ++i) {
            const QByteArray idx = QByteArray::number(i);
            if (prop("track-list/" + idx + "/type") == type) {
                command({"set", property, prop("track-list/" + idx + "/id").toUtf8()});
                return;
            }
        }
    };
    if (active && !wanted) {
        reselect("audio", "aid");
        reselect("video", "vid");
    }
}

// Cuts the A-B section out of the current local file with ffmpeg. Cuts start on the
// keyframe at or before A, so the section can begin a little early.
void Player::saveAbSection(const QString &out)
{
    const double a = prop("ab-loop-a").toDouble();
    const double b = prop("ab-loop-b").toDouble();
    const QString src = playlist->item(current)->data(Qt::UserRole + 1).toString();
    if (b <= a || !QFileInfo(src).isFile()) {
        osd("The section needs a local file and B after A");
        return;
    }
    auto *ff = new QProcess(this);
    connect(ff, &QProcess::finished, this, [this, ff, out](int code, QProcess::ExitStatus) {
        osd(code == 0 ? "Saved section " + QFileInfo(out).fileName() : "Saving the section failed");
        ff->deleteLater();
    });
    connect(ff, &QProcess::errorOccurred, this, [this, ff](QProcess::ProcessError) {
        osd("Could not run ffmpeg");
        ff->deleteLater();
    });
    ff->start("ffmpeg", {"-v", "error", "-y", "-ss", QString::number(a, 'f', 3), "-i", src,
                         "-t", QString::number(b - a, 'f', 3), "-c", "copy", "-avoid_negative_ts", "make_zero", out});
}

void Player::startScreenshotSeries(int seconds)
{
    if (!seriesTimer) {
        seriesTimer = new QTimer(this);
        connect(seriesTimer, &QTimer::timeout, this, [this] {
            if (!stopped)
                screenshot(autoScreenshotPath());
        });
    }
    seriesTimer->start(seconds * 1000);
    osd(QString("Screenshot every %1 s").arg(seconds));
}

void Player::stopScreenshotSeries()
{
    if (seriesTimer)
        seriesTimer->stop();
    QSignalBlocker block(seriesAct);
    seriesAct->setChecked(false);
    osd("Screenshot series stopped");
}

// Frame previews on the seek bar: one frame cut with ffmpeg, cached per second. Only local
// files get previews; a stream would have to be fetched again for every frame.
void Player::showPreview(double secs, const QPoint &globalPos)
{
    if (stopped || current < 0 || !QFileInfo(playlist->item(current)->data(Qt::UserRole + 1).toString()).isFile()) {
        hidePreview();
        return;
    }
    if (!seekPreview) {
        seekPreview = new QLabel(this, Qt::ToolTip | Qt::FramelessWindowHint);
        seekPreview->setAttribute(Qt::WA_ShowWithoutActivating);
        seekPreview->setStyleSheet("border: 1px solid #222; background: black;");
    }
    previewWanted = int(secs);
    previewAnchor = globalPos;
    if (previewCache.contains(previewWanted)) {
        showCachedPreview();
        return;
    }
    if (!previewDebounce) {
        previewDebounce = new QTimer(this);
        previewDebounce->setSingleShot(true);
        previewDebounce->setInterval(150); // wait for the mouse to settle
        connect(previewDebounce, &QTimer::timeout, this, &Player::startPreviewJob);
    }
    previewDebounce->start();
}

void Player::showCachedPreview()
{
    seekPreview->setPixmap(QPixmap::fromImage(previewCache.value(previewWanted)));
    seekPreview->adjustSize();
    const QPoint bar = seekBar->mapToGlobal(QPoint(0, 0));
    seekPreview->move(previewAnchor.x() - seekPreview->width() / 2, bar.y() - seekPreview->height() - 6);
    seekPreview->show();
}

void Player::startPreviewJob()
{
    if (previewCache.contains(previewWanted)) {
        showCachedPreview();
        return;
    }
    if (previewProc)
        previewProc->kill(); // superseded by this request; its result is dropped below
    const int at = previewWanted;
    const QString src = playlist->item(current)->data(Qt::UserRole + 1).toString();
    auto *proc = new QProcess(this);
    previewProc = proc;
    connect(proc, &QProcess::finished, this, [this, proc, at] {
        const QImage img = QImage::fromData(proc->readAllStandardOutput(), "PNG");
        if (proc == previewProc)
            previewProc = nullptr;
        proc->deleteLater();
        if (img.isNull())
            return;
        if (previewCache.size() > 300)
            previewCache.clear();
        previewCache.insert(at, img);
        if (at == previewWanted && seekPreview)
            showCachedPreview();
    });
    proc->start("ffmpeg", {"-v", "error", "-ss", QString::number(at), "-i", src, "-frames:v", "1",
                           "-vf", "scale=160:-2", "-f", "image2pipe", "-vcodec", "png", "-"});
}

void Player::hidePreview()
{
    previewWanted = -1;
    if (previewDebounce)
        previewDebounce->stop();
    if (seekPreview)
        seekPreview->hide();
}

void Player::applyChrome(bool fullscreen)
{
    menuBar()->setVisible(!fullscreen && !hideMenu);
    statusBar()->setVisible(!fullscreen && !hideStatus);
    controls->setVisible(!fullscreen && !hideControls);
}

// Always on top is a window-manager state. Setting it on the QWindow avoids QWidget's
// window re-creation, which would orphan the video window that mpv draws into.
void Player::setAlwaysOnTop(bool on)
{
    if (QWindow *w = windowHandle())
        w->setFlag(Qt::WindowStaysOnTopHint, on);
}

void Player::toggleStats(bool on)
{
    if (on) {
        updateStats();
        statsTimer->start();
    } else {
        statsTimer->stop();
        command({"show-text", "", "1"}); // clears the overlay
    }
}

// Refreshed twice a second. Values are read here: show-text doesn't expand ${property}.
void Player::updateStats()
{
    if (stopped)
        return;
    const QString text =
        "Video: " + prop("video-codec") + "  " + prop("width") + "x" + prop("height") + "  "
        + QString::number(prop("container-fps").toDouble(), 'f', 2) + " fps\n"
        "Audio: " + prop("audio-codec-name") + "  " + prop("audio-params/samplerate") + " Hz  "
        + prop("audio-params/channel-count") + " ch\n"
        "Decoder: " + prop("hwdec-current") + "  dropped " + prop("frame-drop-count")
        + " (decoder " + prop("decoder-frame-drop-count") + ")\n"
        "A/V sync: " + QString::number(prop("avsync").toDouble() * 1000, 'f', 1) + " ms  bitrate "
        + QString::number(prop("video-bitrate").toDouble() / 1000, 'f', 0) + " kb/s";
    command({"show-text", text.toUtf8(), "1000"});
}

void Player::setVideoAdjust(const char *name, int value)
{
    command({"set", name, QByteArray::number(value)});
    QSettings().setValue(QString("video/") + name, value);
}

void Player::openVideoAdjust()
{
    QDialog d(this);
    d.setWindowTitle("Adjust Colors");
    auto *form = new QFormLayout(&d);
    for (auto [name, label] : {std::pair{"brightness", "Brightness"}, {"contrast", "Contrast"}, {"saturation", "Saturation"}, {"gamma", "Gamma"}, {"hue", "Hue"}}) {
        auto *s = new QSlider(Qt::Horizontal);
        s->setObjectName(name);
        s->setRange(-100, 100);
        s->setValue(qRound(prop(name).toDouble()));
        connect(s, &QSlider::valueChanged, &d, [this, name](int v) { setVideoAdjust(name, v); });
        form->addRow(label, s);
    }
    auto *reset = new QPushButton("Reset");
    connect(reset, &QPushButton::clicked, &d, [&d] {
        for (QSlider *s : d.findChildren<QSlider *>())
            s->setValue(0);
    });
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    form->addRow(reset);
    form->addRow(buttons);
    d.exec();
}

void Player::rotateVideo(int by)
{
    const int r = (qRound(prop("video-rotate").toDouble()) + by + 360) % 360;
    command({"set", "video-rotate", QByteArray::number(r)});
    QSettings().setValue("video/rotate", r);
}

// Adds or removes a labelled video filter. Safe to call again with the same state, so
// reapplying saved settings never stacks a second copy of the filter.
void Player::setVideoFilter(const char *label, const char *filter, bool on)
{
    command({"vf", "remove", QByteArray("@") + label});
    if (on)
        command({"vf", "add", QByteArray("@") + label + ":" + filter});
    QSettings().setValue(QString("video/") + label, on);
}

// Crops the source to an aspect ratio, centred. 0 turns cropping off.
void Player::setCrop(double aspect)
{
    QSettings().setValue("video/crop", aspect);
    // The size captured at load can be missing if the video output came up late; ask again.
    QSize src = srcSize.isEmpty() ? QSize(prop("video-params/w").toInt(), prop("video-params/h").toInt()) : srcSize;
    if (aspect <= 0 || src.isEmpty()) {
        command({"set", "video-crop", ""});
        return;
    }
    int w = src.width(), h = src.height(), cw = w, ch = h;
    if (double(w) / h > aspect)
        cw = qRound(h * aspect);
    else
        ch = qRound(w / aspect);
    command({"set", "video-crop",
             QByteArray::number(cw) + "x" + QByteArray::number(ch) + "+" +
             QByteArray::number((w - cw) / 2) + "+" + QByteArray::number((h - ch) / 2)});
}

// Normalizer, 10-band equalizer, compressor and stereo widen, as one mpv audio filter chain.
// Bands above 45% of the file's sample rate are skipped, because ffmpeg rejects them.
static const QList<double> eqBandHz = {31, 62, 125, 250, 500, 1000, 2000, 4000, 8000, 16000};

void Player::applyAudioFilters()
{
    QSettings s;
    QStringList filters;
    if (s.value("audioNormalize", false).toBool())
        filters << "lavfi=[dynaudnorm]";
    if (s.value("eq/enabled", false).toBool()) {
        const QStringList gains = s.value("eq/gains").toStringList();
        const double rate = prop("audio-params/samplerate").toDouble();
        for (int i = 0; i < eqBandHz.size(); ++i) {
            const double g = gains.value(i).toDouble();
            if (g == 0 || (rate > 0 && eqBandHz[i] > rate * 0.45))
                continue;
            filters << QString("lavfi=[equalizer=f=%1:t=o:w=1:g=%2]").arg(eqBandHz[i]).arg(g);
        }
    }
    if (s.value("eq/compressor", false).toBool())
        filters << "lavfi=[acompressor]";
    if (s.value("eq/widen", false).toBool())
        filters << "lavfi=[extrastereo=m=2.5]";
    const int semitones = s.value("eq/pitch", 0).toInt();
    const double rate = prop("audio-params/samplerate").toDouble();
    if (semitones != 0 && rate > 0) {
        const double f = std::pow(2.0, semitones / 12.0);
        filters << QString("lavfi=[asetrate=%1,aresample=%2,atempo=%3]")
                       .arg(QString::number(rate * f, 'f', 0), QString::number(rate, 'f', 0), QString::number(1.0 / f, 'f', 6));
    }
    mpv_set_property_string(mpv, "af", filters.join(',').toUtf8().constData());
}

void Player::setEqGains(const QList<double> &gains)
{
    QStringList saved;
    for (double g : gains)
        saved << QString::number(g);
    QSettings().setValue("eq/gains", saved);
    QSettings().setValue("eq/enabled", true);
    applyAudioFilters();
}

void Player::openEqualizer()
{
    QDialog d(this);
    d.setWindowTitle("Equalizer");
    auto *grid = new QGridLayout(&d);
    QStringList gains = QSettings().value("eq/gains").toStringList();
    gains.resize(eqBandHz.size());
    for (int i = 0; i < eqBandHz.size(); ++i) {
        auto *s = new QSlider(Qt::Vertical);
        s->setObjectName(QString("band%1").arg(i));
        s->setRange(-12, 12);
        s->setValue(qRound(gains[i].toDouble()));
        connect(s, &QSlider::valueChanged, &d, [this, i](int v) {
            QStringList g = QSettings().value("eq/gains").toStringList();
            g.resize(eqBandHz.size());
            g[i] = QString::number(v);
            QSettings().setValue("eq/gains", g);
            applyAudioFilters();
        });
        const double hz = eqBandHz[i];
        grid->addWidget(s, 0, i, Qt::AlignHCenter);
        grid->addWidget(new QLabel(hz >= 1000 ? QString::number(hz / 1000) + "k" : QString::number(hz)), 1, i, Qt::AlignHCenter);
    }
    auto addCheck = [&](int row, const char *name, const char *text, const char *key) {
        auto *c = new QCheckBox(text);
        c->setObjectName(name);
        c->setChecked(QSettings().value(key).toBool());
        connect(c, &QCheckBox::toggled, &d, [this, key](bool on) {
            QSettings().setValue(key, on);
            applyAudioFilters();
        });
        grid->addWidget(c, row, 0, 1, eqBandHz.size());
    };
    addCheck(2, "eqEnabled", "Equalizer enabled", "eq/enabled");
    addCheck(3, "eqCompressor", "Compressor", "eq/compressor");
    addCheck(4, "eqWiden", "Stereo widen", "eq/widen");
    auto *reset = new QPushButton("Reset");
    connect(reset, &QPushButton::clicked, &d, [&d] {
        for (QSlider *s : d.findChildren<QSlider *>())
            s->setValue(0);
    });
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    auto *pitch = new QSlider(Qt::Horizontal);
    pitch->setObjectName("pitch");
    pitch->setRange(-12, 12);
    pitch->setValue(QSettings().value("eq/pitch", 0).toInt());
    connect(pitch, &QSlider::valueChanged, &d, [this](int v) {
        QSettings().setValue("eq/pitch", v);
        applyAudioFilters();
    });
    grid->addWidget(new QLabel("Pitch (semitones, speed unchanged)"), 5, 0, 1, 4);
    grid->addWidget(pitch, 5, 4, 1, eqBandHz.size() - 4);
    grid->addWidget(reset, 6, 0, 1, eqBandHz.size());
    grid->addWidget(buttons, 7, 0, 1, eqBandHz.size());
    d.exec();
}

void Player::rebuildAudioDeviceMenu()
{
    audioDeviceMenu->clear();
    QActionGroup *group = audioDeviceMenu->findChild<QActionGroup *>();
    if (!group)
        group = new QActionGroup(audioDeviceMenu);
    const QString now = prop("audio-device");
    const int n = prop("audio-device-list/count").toInt();
    for (int i = 0; i < n; ++i) {
        QByteArray p = "audio-device-list/" + QByteArray::number(i) + "/";
        const QString name = prop(p + "name");
        QString desc = prop(p + "description");
        QAction *a = audioDeviceMenu->addAction(desc.isEmpty() ? name : desc);
        a->setCheckable(true);
        a->setChecked(name == now);
        group->addAction(a);
        connect(a, &QAction::triggered, this, [this, name] {
            command({"set", "audio-device", name.toUtf8()});
            QSettings().setValue("audioDevice", name);
        });
    }
}

// What happens when the playlist runs out of files (not when the user presses Stop).
void Player::finishPlaylist()
{
    const QString action = QSettings().value("afterPlayback", "nothing").toString();
    if (action == "exit") {
        QTimer::singleShot(0, this, [this] { close(); }); // not from inside mpv's event handling
    } else if (action == "poweroff" || action == "suspend") {
        QDBusMessage call = QDBusMessage::createMethodCall("org.freedesktop.login1", "/org/freedesktop/login1",
                                                           "org.freedesktop.login1.Manager",
                                                           action == "suspend" ? "Suspend" : "PowerOff");
        call << true; // interactive: polkit may ask for a password
        QDBusConnection::systemBus().call(call, QDBus::NoBlock); // logind answers; nothing to wait for
    }
}

// Favorites are file + position; the position is applied once the file has loaded.
void Player::addFavorite()
{
    if (stopped || current < 0)
        return;
    const QString path = playlist->item(current)->data(Qt::UserRole + 1).toString();
    QStringList kept{QString::number(prop("time-pos").toDouble(), 'f', 1) + "\t" + path};
    for (const QString &e : QSettings().value("favorites").toStringList())
        if (e.mid(e.indexOf('\t') + 1) != path)
            kept << e;
    QSettings().setValue("favorites", kept.mid(0, 30));
    statusBar()->showMessage("Added to favorites", 3000);
}

void Player::rebuildFavoritesMenu()
{
    favoritesMenu->clear();
    QAction *add = favoritesMenu->addAction("&Add Current File");
    add->setEnabled(!stopped);
    connect(add, &QAction::triggered, this, &Player::addFavorite);
    favoritesMenu->addSeparator();
    const QStringList list = QSettings().value("favorites").toStringList();
    if (list.isEmpty())
        favoritesMenu->addAction("No Favorites")->setEnabled(false);
    for (const QString &e : list) {
        const int tab = e.indexOf('\t');
        const double secs = e.left(tab).toDouble();
        const QString path = e.mid(tab + 1);
        QAction *a = favoritesMenu->addAction(fmtTime(secs) + "  " + (path.contains("://") ? path : QFileInfo(path).fileName()));
        connect(a, &QAction::triggered, this, [this, path, secs] {
            pendingSeek = secs;
            openFiles({path});
        });
    }
    if (!list.isEmpty()) {
        favoritesMenu->addSeparator();
        QAction *clear = favoritesMenu->addAction("&Clear Favorites");
        connect(clear, &QAction::triggered, this, [] { QSettings().remove("favorites"); });
    }
}

void Player::toggleFullscreen()
{
    bool fs = !isFullScreen();
    applyChrome(fs);
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
            } else if (name == "chapter-list") {
                readChapters();
            } else if (p->format == MPV_FORMAT_NONE) {
                continue; // property unavailable (e.g. nothing loaded)
            } else if (name == "time-pos") {
                updateTime(*static_cast<double *>(p->data));
            } else if (name == "duration") {
                duration = *static_cast<double *>(p->data);
                seekBar->setRange(0, int(duration * 10));
                seekBar->setEnabled(duration > 0);
                updateChapterMarks();
            } else if (name == "pause") {
                if (!stopped)
                    statusLabel->setText(*static_cast<int *>(p->data) ? "Paused" : "Playing");
            } else if (name == "volume") {
                QSignalBlocker b(volBar);
                volBar->setValue(int(*static_cast<double *>(p->data)));
            } else if (name == "mute") {
                muteAct->setChecked(*static_cast<int *>(p->data));
            } else if (name == "chapter") {
                if (QSettings().value("autoSkip", false).toBool())
                    skipIntro(true);
            }
        } else if (ev->event_id == MPV_EVENT_START_FILE) {
            statusLabel->setText("Opening...");
        } else if (ev->event_id == MPV_EVENT_FILE_LOADED) {
            statusLabel->setText(prop("pause") == "yes" ? "Paused" : "Playing");
            srcSize = QSize(prop("video-params/w").toInt(), prop("video-params/h").toInt());
            applyAudioFilters(); // band limits depend on this file's sample rate
            applyVisualizer();
            if (QSettings().value("autoSkip", false).toBool())
                skipIntro(true);
            if (cropGroup) { // the remembered crop applies to every file
                const double saved = QSettings().value("video/crop", 0.0).toDouble();
                for (QAction *a : cropGroup->actions())
                    if (a->data().toDouble() == saved)
                        a->setChecked(true);
                setCrop(saved);
            }
            if (pendingSeek >= 0) {
                command({"seek", QByteArray::number(pendingSeek), "absolute"});
                pendingSeek = -1;
            }
        } else if (ev->event_id == MPV_EVENT_END_FILE) {
            auto *e = static_cast<mpv_event_end_file *>(ev->data);
            if (e->reason == MPV_END_FILE_REASON_ERROR)
                statusBar()->showMessage(QString("Cannot play: ") + mpv_error_string(e->error), 5000);
            if (e->reason == MPV_END_FILE_REASON_EOF || e->reason == MPV_END_FILE_REASON_ERROR) {
                if (current + 1 < playlist->count())
                    playIndex(current + 1);
                else if (repeat == RepeatPlaylist && e->reason == MPV_END_FILE_REASON_EOF)
                    playIndex(0);
                else if (e->reason == MPV_END_FILE_REASON_EOF && QSettings().value("autoNext", false).toBool() && canSkip(1))
                    skipFile(1); // lone file: carry on with the next one in its folder
                else {
                    stop();
                    if (e->reason == MPV_END_FILE_REASON_EOF) // a file that failed to load is not "finished"
                        finishPlaylist();
                }
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
                if (!hideControls && static_cast<QMouseEvent *>(ev)->position().y() >= video->height() - 40)
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
        showPreview(v / 10.0, me->globalPosition().toPoint());
    } else if (obj == seekBar && ev->type() == QEvent::Leave) {
        hidePreview();
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
