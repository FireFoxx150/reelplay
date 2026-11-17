// GUI tests: drive the real Player with key/mouse events. Needs an X11 display.
#include <QtTest>
#include <QDockWidget>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QSettings>
#include <QSlider>
#include <QToolTip>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QKeySequenceEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QContextMenuEvent>
#include <QTemporaryDir>
#include <QProcess>
#include <QWindow>
#include <QStatusBar>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QFontComboBox>
#include <QSpinBox>
#include <QInputDialog>
#include <clocale>
#include "../player.h"
#include "../mpris.h"

class PlayerTest : public QObject
{
    Q_OBJECT
    QTemporaryDir tmp;
    QString clipA, clipB;
    Player *p = nullptr;

    QString status() { return p->statusLabel->text(); }
    double pos() { return p->prop("time-pos").toDouble(); }
    void key(const char *seq) { QTest::keySequence(p, QKeySequence(seq)); }
    double num(const char *name) { return p->prop(name).toDouble(); }
    void trigger(const QString &text)
    {
        for (QAction *a : p->findChildren<QAction *>())
            if (a->text() == text) { a->trigger(); return; }
        QFAIL(qPrintable("no action " + text));
    }

    // Encode a 60 s clip with one video and one audio track using libmpv itself.
    void makeClip(const QString &path)
    {
        mpv_handle *enc = mpv_create();
        mpv_set_option_string(enc, "o", path.toUtf8().constData());
        mpv_set_option_string(enc, "vo", "lavc");
        mpv_set_option_string(enc, "ao", "lavc");
        mpv_initialize(enc);
        const char *args[] = {"loadfile", "av://lavfi:testsrc=duration=60:size=320x240:rate=10[out0];sine=duration=60[out1]", nullptr};
        mpv_command(enc, args);
        for (;;) {
            mpv_event *ev = mpv_wait_event(enc, 30);
            if (ev->event_id == MPV_EVENT_END_FILE || ev->event_id == MPV_EVENT_NONE)
                break;
        }
        mpv_terminate_destroy(enc);
        QVERIFY2(QFileInfo(path).size() > 0, "libmpv failed to encode test clip");
    }

    // Encode an av://lavfi source to `path` with mpv encoding options ("k=v|k=v"); true on success.
    static bool encode(const QString &path, const QString &src, const QString &opts)
    {
        mpv_handle *enc = mpv_create();
        mpv_set_option_string(enc, "o", path.toUtf8().constData());
        mpv_set_option_string(enc, "vo", "lavc");
        mpv_set_option_string(enc, "ao", "lavc");
        for (const QString &kv : opts.split('|', Qt::SkipEmptyParts)) {
            QStringList p = kv.split('=');
            mpv_set_option_string(enc, p[0].toUtf8().constData(), p.mid(1).join('=').toUtf8().constData());
        }
        mpv_initialize(enc);
        QByteArray url = ("av://lavfi:" + src).toUtf8();
        const char *args[] = {"loadfile", url.constData(), nullptr};
        mpv_command(enc, args);
        bool ok = false;
        for (;;) {
            mpv_event *ev = mpv_wait_event(enc, 60);
            if (ev->event_id == MPV_EVENT_NONE)
                break;
            if (ev->event_id == MPV_EVENT_END_FILE) {
                ok = static_cast<mpv_event_end_file *>(ev->data)->reason == MPV_END_FILE_REASON_EOF;
                break;
            }
        }
        mpv_terminate_destroy(enc);
        return ok && QFileInfo(path).size() > 0;
    }

    QString writeFile(const QString &name, const QByteArray &content)
    {
        QString path = tmp.filePath(name);
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        f.open(QIODevice::WriteOnly);
        f.write(content);
        return path;
    }

    // Talk to the player from another process, as the desktop's media-key daemon does.
    QString gdbus(const QStringList &args)
    {
        QProcess proc;
        proc.start("gdbus", QStringList{"call", "--session", "--dest", p->mpris->serviceName(),
                                        "--object-path", "/org/mpris/MediaPlayer2"} + args);
        QElapsedTimer t;
        t.start();
        while (proc.state() != QProcess::NotRunning && t.elapsed() < 5000)
            QTest::qWait(20); // keep our event loop running so the player can answer
        return QString(proc.readAllStandardOutput() + proc.readAllStandardError()).trimmed();
    }
    QString mpris(const char *method, const QStringList &args = {})
    {
        return gdbus(QStringList{"--method", QString("org.mpris.MediaPlayer2.Player.") + method} + args);
    }
    QString mprisGet(const char *prop, const char *iface = "org.mpris.MediaPlayer2.Player")
    {
        return gdbus({"--method", "org.freedesktop.DBus.Properties.Get", iface, prop});
    }

    void mouseMoveVideo(int y)
    {
        QPointF at(50, y);
        QMouseEvent move(QEvent::MouseMove, at, p->video->mapToGlobal(at), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(p->video, &move);
    }

    QStringList sources()
    {
        QStringList l;
        for (int r = 0; r < p->playlist->count(); ++r)
            l << p->playlist->item(r)->data(Qt::UserRole + 1).toString();
        return l;
    }

    // Run fn on the modal dialog once it is up (the dialog's exec() blocks the test).
    void onModal(std::function<void(QDialog *)> fn)
    {
        auto *t = new QTimer(this);
        connect(t, &QTimer::timeout, this, [t, fn] {
            if (auto *d = qobject_cast<QDialog *>(QApplication::activeModalWidget())) {
                t->stop();
                t->deleteLater();
                fn(d);
            }
        });
        t->start(50);
    }

    void newPlayer()
    {
        delete p;
        p = new Player;
        p->show();
        QVERIFY(QTest::qWaitForWindowExposed(p));
        p->activateWindow(); // window shortcuts only fire in the active window
        QVERIFY(QTest::qWaitForWindowActive(p));
    }

    void startClips()
    {
        p->openFiles({clipA, clipB});
        QTRY_COMPARE_WITH_TIMEOUT(status(), QString("Playing"), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(pos() > 0, 5000);
    }

private slots:
    void initTestCase()
    {
        std::setlocale(LC_NUMERIC, "C");
        QStandardPaths::setTestModeEnabled(true); // keep settings + watch_later out of the real profile
        clipA = tmp.filePath("a.mkv");
        clipB = tmp.filePath("b.mkv");
        makeClip(clipA);
        QFile::copy(clipA, clipB);
    }

    void init()
    {
        QSettings().clear();
        QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).removeRecursively();
        newPlayer();
    }
    void cleanup() { delete p; p = nullptr; }

    // mpv applies the crop inside the video output: with the null output (CI) it is
    // dropped as soon as the rotation or the filters change, so those checks need a real one.
    static bool realVideoOutput() { return qgetenv("REELPLAY_VO") != "null"; }

    void playPauseHotkey()
    {
        startClips();
        key("Space");
        QTRY_COMPARE(status(), QString("Paused"));
        key("Space");
        QTRY_COMPARE(status(), QString("Playing"));
    }

    void stopHotkey()
    {
        startClips();
        key(".");
        QTRY_COMPARE(status(), QString("Stopped"));
        QVERIFY(p->stopped);
        key("Space"); // Play after Stop restarts the current file
        QTRY_COMPARE_WITH_TIMEOUT(status(), QString("Playing"), 5000);
    }

    void seekHotkeys()
    {
        startClips();
        key("Space");
        QTRY_COMPARE(status(), QString("Paused"));
        double t = pos();
        key("Shift+Right");
        QTRY_VERIFY(pos() >= t + 25);
        t = pos();
        key("Left");
        QTRY_VERIFY(pos() <= t - 4);
    }

    void playlistHotkeys()
    {
        startClips();
        QCOMPARE(p->playlist->count(), 2);
        key("PgDown");
        QCOMPARE(p->current, 1);
        key("PgUp");
        QCOMPARE(p->current, 0);
        key("Ctrl+7");
        QVERIFY(p->dock->isVisible());
        key("Ctrl+7");
        QVERIFY(!p->dock->isVisible());
    }

    void volumeAndMute()
    {
        startClips();
        int v = p->volBar->value();
        key("Down");
        QTRY_COMPARE(p->volBar->value(), v - 5);
        key("Ctrl+M");
        QTRY_VERIFY(p->muteAct->isChecked());
        key("Ctrl+M");
        QTRY_VERIFY(!p->muteAct->isChecked());
    }

    void fullscreenHotkeys()
    {
        startClips();
        key("Alt+Return");
        QTRY_VERIFY(p->isFullScreen());
        QVERIFY(!p->controls->isVisible());
        key("Esc");
        QTRY_VERIFY(!p->isFullScreen());
        QVERIFY(p->controls->isVisible());
    }

    void trackMenus()
    {
        startClips();
        // One audio track; subtitle menu has just "Disabled".
        QTRY_COMPARE(p->audioMenu->actions().size(), 1);
        QVERIFY(p->audioMenu->actions().first()->isChecked());
        QCOMPARE(p->subMenu->actions().size(), 1);
    }

    void singleClickPauses()
    {
        startClips();
        QTest::mouseClick(p->video, Qt::LeftButton);
        QCOMPARE(status(), QString("Playing")); // not yet: waits for double-click interval
        QTRY_COMPARE(status(), QString("Paused"));
    }

    void doubleClickOnlyFullscreens()
    {
        startClips();
        QTest::mouseDClick(p->video, Qt::LeftButton);
        QTRY_VERIFY(p->isFullScreen());
        QTest::qWait(QApplication::doubleClickInterval() + 200);
        QCOMPARE(status(), QString("Playing"));
    }

    void settingsPersist()
    {
        startClips();
        key("Down");
        key("Down");
        QTRY_COMPARE(p->volBar->value(), 90);
        p->dock->show();
        p->resize(700, 450);
        p->close();
        newPlayer();
        QCOMPARE(p->volBar->value(), 90);
        QVERIFY(p->dock->isVisible());
        QCOMPARE(p->size(), QSize(700, 450));
    }

    void resumePosition()
    {
        startClips();
        key("Shift+Right");
        QTRY_VERIFY(pos() >= 30);
        p->close(); // closing the window (how users quit) saves the position
        newPlayer();
        p->openFiles({clipA});
        QTRY_VERIFY_WITH_TIMEOUT(pos() >= 29, 5000);
    }

    void contextMenu()
    {
        startClips();
        QContextMenuEvent ev(QContextMenuEvent::Mouse, QPoint(10, 10), p->video->mapToGlobal(QPoint(10, 10)));
        QApplication::sendEvent(p->video, &ev);
        QTRY_VERIFY(p->contextMenu->isVisible());
        QCOMPARE(p->contextMenu->actions().size(), p->menuBar()->actions().size());
        p->contextMenu->hide();
        QCOMPARE(status(), QString("Playing")); // right-click must not pause
    }

    void seekTooltip()
    {
        startClips();
        QTRY_VERIFY(p->seekBar->isEnabled());
        // Synthesized move: QTest::mouseMove drives the real cursor, which races the user's mouse.
        QPointF mid(p->seekBar->width() / 2, p->seekBar->height() / 2);
        QMouseEvent move(QEvent::MouseMove, mid, p->seekBar->mapToGlobal(mid), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(p->seekBar, &move);
        QTRY_VERIFY(QToolTip::isVisible());
        QVERIFY2(QToolTip::text().contains(QRegularExpression("^00:00:[23]\\d$")), qPrintable(QToolTip::text()));
    }

    void zoomHotkeys()
    {
        startClips();
        key("Alt+2");
        QTRY_COMPARE(p->video->size(), QSize(320, 240));
        key("Alt+3");
        QTRY_COMPARE(p->video->size(), QSize(640, 480));
    }

    void aspectRatio()
    {
        startClips();
        trigger("16:9");
        QTRY_VERIFY(qAbs(num("video-aspect-override") - 16.0 / 9) < 0.01);
        trigger("Default");
        QTRY_COMPARE(num("video-aspect-override"), -1.0);
    }

    void speedHotkeys()
    {
        startClips();
        key("Ctrl+Up");
        key("Ctrl+Up");
        QCOMPARE(num("speed"), 1.2);
        key("Ctrl+Down");
        QCOMPARE(num("speed"), 1.1);
        key("Ctrl+R");
        QCOMPARE(num("speed"), 1.0);
    }

    void syncHotkeys()
    {
        startClips();
        key("F2");
        key("F2");
        QCOMPARE(num("sub-delay"), 0.2);
        key("F1");
        QCOMPARE(num("sub-delay"), 0.1);
        QTest::keyClick(p, Qt::Key_Plus, Qt::KeypadModifier);
        QCOMPARE(num("audio-delay"), 0.01);
        QTest::keyClick(p, Qt::Key_Minus, Qt::KeypadModifier);
        QTest::keyClick(p, Qt::Key_Minus, Qt::KeypadModifier);
        QCOMPARE(num("audio-delay"), -0.01);
    }

    void abRepeat()
    {
        startClips();
        key("Space");
        QTRY_COMPARE(status(), QString("Paused"));
        key("[");
        QTRY_VERIFY(p->prop("ab-loop-a") != "no");
        key("Shift+Right");
        QTRY_VERIFY(pos() > num("ab-loop-a") + 20);
        key("]");
        QTRY_VERIFY(p->prop("ab-loop-b") != "no");
        QVERIFY(num("ab-loop-b") > num("ab-loop-a"));
        key("\\");
        QTRY_COMPARE(p->prop("ab-loop-a"), QString("no"));
        QTRY_COMPARE(p->prop("ab-loop-b"), QString("no"));
    }

    void repeatModes()
    {
        startClips();
        trigger("Repeat &File");
        QTRY_COMPARE(p->prop("loop-file"), QString("inf"));
        trigger("Repeat &Playlist");
        QTRY_COMPARE(p->prop("loop-file"), QString("no"));
        QCOMPARE(QSettings().value("repeat").toInt(), int(Player::RepeatPlaylist));
        key("PgDown"); // last file: at its end the playlist wraps to the first
        QTRY_COMPARE(p->current, 1);
        QTRY_VERIFY_WITH_TIMEOUT(pos() > 0, 5000);
        p->command({"seek", "58", "absolute"});
        QTRY_COMPARE_WITH_TIMEOUT(p->current, 0, 8000);
        QCOMPARE(p->stopped, false);
    }

    void recentFiles()
    {
        startClips();
        QStringList recent = QSettings().value("recent").toStringList();
        QCOMPARE(recent.value(0), clipA);
        emit p->recentMenu->aboutToShow();
        QCOMPARE(p->recentMenu->actions().first()->text(), clipA);
    }

    void openFolderNaturalOrder()
    {
        QDir d(tmp.filePath("folder"));
        d.mkpath(".");
        QFile::copy(clipA, d.filePath("ep10.mkv"));
        QFile::copy(clipA, d.filePath("ep2.mkv"));
        QFile(d.filePath("notes.txt")).open(QIODevice::WriteOnly);
        p->openFiles({d.path()});
        QCOMPARE(p->playlist->count(), 2);
        QCOMPARE(p->playlist->item(0)->text(), QString("ep2.mkv"));
        QCOMPARE(p->playlist->item(1)->text(), QString("ep10.mkv"));
        QTRY_COMPARE_WITH_TIMEOUT(status(), QString("Playing"), 5000);
    }

    void discFolderDetected()
    {
        QDir d(tmp.filePath("movie"));
        d.mkpath("VIDEO_TS");
        p->openFiles({d.path()});
        QCOMPARE(p->playlist->count(), 1);
        QCOMPARE(p->playlist->item(0)->data(Qt::UserRole).toString(), QString("dvd://"));
        QCOMPARE(p->prop("dvd-device"), d.path());
    }

    void fullscreenAutoControls()
    {
        startClips();
        p->idleTimer->setInterval(200);
        key("Alt+Return");
        QTRY_VERIFY(p->isFullScreen());
        QVERIFY(!p->controls->isVisible());
        mouseMoveVideo(p->video->height() - 5);
        QVERIFY(p->controls->isVisible());
        mouseMoveVideo(10);
        QVERIFY(!p->controls->isVisible());
        QTRY_COMPARE(p->video->cursor().shape(), Qt::BlankCursor);
        mouseMoveVideo(20);
        QCOMPARE(p->video->cursor().shape(), Qt::ArrowCursor);
        key("Esc");
        QTRY_VERIFY(!p->isFullScreen());
        QVERIFY(p->controls->isVisible());
    }

    void playlistEditing()
    {
        p->openFiles({clipA, clipB, clipA});
        QTRY_COMPARE_WITH_TIMEOUT(status(), QString("Playing"), 5000);
        QCOMPARE(p->current, 0);
        // Reorder (what drag-and-drop does): playing item moves to the end.
        QVERIFY(p->playlist->model()->moveRow(QModelIndex(), 0, QModelIndex(), 3));
        QCOMPARE(p->current, 2);
        // Delete key removes the selection.
        p->dock->show();
        p->playlist->setCurrentRow(0);
        p->playlist->setFocus();
        QTest::keyClick(p->playlist, Qt::Key_Delete);
        QCOMPARE(p->playlist->count(), 2);
        QCOMPARE(p->current, 1);
        trigger("Rando&mize");
        QCOMPARE(p->playlist->count(), 2);
        QVERIFY(p->playlist->item(p->current)->data(Qt::UserRole + 2).toBool());
        QCOMPARE(status(), QString("Playing")); // editing never interrupts playback
    }

    void m3uRoundTrip()
    {
        p->openFiles({clipA, clipB});
        QString m3u = tmp.filePath("list.m3u");
        p->savePlaylist(m3u);
        QFile f(m3u);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(QString(f.readAll()), "#EXTM3U\n" + clipA + "\n" + clipB + "\n");
        p->openFiles({m3u});
        QCOMPARE(sources(), QStringList({clipA, clipB}));
    }

    void m3uRelativeAndSelfInclude()
    {
        QString m3u = tmp.filePath("rel.m3u");
        QFile f(m3u);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("#EXTM3U\n# comment\n\nb.mkv\nrel.m3u\n"); // includes itself
        f.close();
        p->openFiles({m3u});
        QCOMPARE(sources(), QStringList({clipB}));
    }

    void propertiesDialogText()
    {
        startClips();
        QString t = p->propertiesText();
        QVERIFY2(t.contains("File: a.mkv"), qPrintable(t));
        QVERIFY2(t.contains("320x240"), qPrintable(t));
        QVERIFY2(t.contains("Container: mkv") || t.contains("Container: matroska"), qPrintable(t));
        QVERIFY2(t.contains(QRegularExpression("Audio: \\w+, 44100 Hz, 1 ch")), qPrintable(t));
    }

    void autoScreenshot()
    {
        QString dir = tmp.filePath("shots");
        QSettings().setValue("screenshotDir", dir);
        startClips();
        key("F5");
        QTRY_COMPARE_WITH_TIMEOUT(QDir(dir).entryList({"a_*.png"}, QDir::Files).size(), 1, 5000);
        QString png = QDir(dir).filePath(QDir(dir).entryList({"a_*.png"}).first());
        QTRY_COMPARE(QImage(png).size(), QSize(320, 240)); // mpv may still be writing it
    }

    void optionsApplyAndPersist()
    {
        startClips();
        onModal([](QDialog *d) {
            d->findChild<QComboBox *>("hwdec")->setCurrentText("auto-safe");
            d->findChild<QLineEdit *>("alang")->setText("eng");
            d->findChild<QCheckBox *>("darkTheme")->setChecked(false);
            d->findChild<QKeySequenceEdit *>("Play/Play-Pause")->setKeySequence(QKeySequence("P"));
            d->accept();
        });
        trigger("&Options...");
        QVERIFY(QTest::qWaitForWindowActive(p)); // focus returns from the dialog asynchronously
        QCOMPARE(p->prop("hwdec"), QString("auto-safe"));
        QCOMPARE(p->prop("alang"), QString("eng"));
        QVERIFY(qApp->palette().color(QPalette::Window) != QColor(32, 32, 32));
        key("P");
        QTRY_COMPARE(status(), QString("Paused"));
        key("Space"); // old key no longer bound
        QTest::qWait(300);
        QCOMPARE(status(), QString("Paused"));

        p->close();
        newPlayer();
        startClips();
        key("P");
        QTRY_COMPARE(status(), QString("Paused"));

        onModal([](QDialog *d) {
            d->findChild<QCheckBox *>("darkTheme")->setChecked(true);
            for (auto *b : d->findChildren<QPushButton *>())
                if (b->text() == "Reset All to Defaults") b->click();
            d->accept();
        });
        trigger("&Options...");
        QVERIFY(QTest::qWaitForWindowActive(p)); // focus returns from the dialog asynchronously
        QCOMPARE(qApp->palette().color(QPalette::Window), QColor(32, 32, 32));
        QVERIFY(!QSettings().contains("keys/Play/Play-Pause"));
        key("Space");
        QTRY_COMPARE(status(), QString("Playing"));
    }

    // Real files in the containers/codecs VLC plays, encoded on the fly by libmpv.
    void formats_data()
    {
        QTest::addColumn<QString>("ext");
        QTest::addColumn<QString>("opts");
        QTest::addColumn<QString>("size");
        QTest::addColumn<int>("rate");
        QTest::addColumn<bool>("video");
        QTest::addColumn<bool>("audio");
        auto row = [](const char *tag, const char *ext, const char *opts, bool video, bool audio,
                      const char *size = "160x120", int rate = 44100) {
            QTest::newRow(tag) << ext << opts << size << rate << video << audio;
        };
        row("mkv-h264-aac", "mkv", "of=matroska|ovc=libx264|oac=aac", true, true);
        row("mp4-h264-aac", "mp4", "of=mp4|ovc=libx264|oac=aac", true, true);
        row("mp4-hevc-aac", "mp4", "of=mp4|ovc=libx265|ovcopts=preset=ultrafast,x265-params=log-level=error|oac=aac", true, true);
        row("mov-prores-pcm", "mov", "of=mov|ovc=prores|oac=pcm_s16be", true, true);
        row("webm-vp8-vorbis", "webm", "of=webm|ovc=libvpx|ovcopts=deadline=realtime|oac=libvorbis", true, true);
        row("webm-vp9-opus", "webm", "of=webm|ovc=libvpx-vp9|ovcopts=deadline=realtime,cpu-used=8|oac=libopus", true, true, "160x120", 48000);
        row("mkv-av1-opus", "mkv", "of=matroska|ovc=libsvtav1|ovcopts=preset=12|oac=libopus", true, true, "160x120", 48000);
        row("avi-mpeg4-mp3", "avi", "of=avi|ovc=mpeg4|oac=libmp3lame", true, true);
        row("avi-mjpeg-pcm", "avi", "of=avi|ovc=mjpeg|oac=pcm_s16le", true, true);
        row("avi-cinepak-pcm8", "avi", "of=avi|ovc=cinepak|oac=pcm_u8", true, true);
        row("mpg-mpeg1-mp2", "mpg", "of=mpeg|ovc=mpeg1video|oac=mp2", true, true);
        row("mpg-mpeg2-mp2", "mpg", "of=mpeg|ovc=mpeg2video|oac=mp2", true, true);
        row("vob-mpeg2-ac3", "vob", "of=vob|ovc=mpeg2video|oac=ac3", true, true, "160x120", 48000);
        row("ts-h264-ac3", "ts", "of=mpegts|ovc=libx264|oac=ac3", true, true, "160x120", 48000);
        row("flv-sorenson-mp3", "flv", "of=flv|ovc=flv|oac=libmp3lame", true, true);
        row("wmv-wmv2-wma", "wmv", "of=asf|ovc=wmv2|oac=wmav2", true, true);
        row("3gp-h263-amr", "3gp", "of=3gp|ovc=h263|oac=libopencore_amrnb", true, true, "176x144", 8000);
        row("ogv-theora-vorbis", "ogv", "of=ogg|ovc=libtheora|oac=libvorbis", true, true);
        // No RealMedia row: mpv's encoder writes .rm files with a broken header (0.004 s
        // duration), so a generated sample can't show whether real .rm files play.
        row("nut-ffv1-flac", "nut", "of=nut|ovc=ffv1|oac=flac", true, true);
        row("dv-dvvideo", "dv", "of=dv|ovc=dvvideo", true, false, "720x576");
        row("mp3", "mp3", "of=mp3|oac=libmp3lame", false, true);
        row("flac", "flac", "of=flac|oac=flac", false, true);
        row("wav", "wav", "of=wav|oac=pcm_s16le", false, true);
        row("ogg-vorbis", "ogg", "of=ogg|oac=libvorbis", false, true);
        row("opus", "opus", "of=opus|oac=libopus", false, true, "", 48000);
        row("m4a-aac", "m4a", "of=ipod|oac=aac", false, true);
        row("aac-adts", "aac", "of=adts|oac=aac", false, true);
        row("wma", "wma", "of=asf|oac=wmav2", false, true);
        row("aiff", "aiff", "of=aiff|oac=pcm_s16be", false, true);
        row("ac3", "ac3", "of=ac3|oac=ac3", false, true, "", 48000);
        row("dts", "dts", "of=dts|oac=dca|oacopts=strict=-2", false, true, "", 48000);
        row("truehd", "thd", "of=truehd|oac=truehd|oacopts=strict=-2", false, true, "", 48000);
        row("wavpack", "wv", "of=wv|oac=wavpack", false, true);
        row("tta", "tta", "of=tta|oac=tta", false, true);
        row("mka-flac", "mka", "of=matroska|oac=flac", false, true);
        row("amr", "amr", "of=amr|oac=libopencore_amrnb", false, true, "", 8000);
        row("au", "au", "of=au|oac=pcm_s16be", false, true);
        row("caf-alac", "caf", "of=caf|oac=alac", false, true);
        row("spx-speex", "spx", "of=ogg|oac=libspeex", false, true, "", 16000);
        row("mp2", "mp2", "of=mp2|oac=libtwolame", false, true);
        row("w64", "w64", "of=w64|oac=pcm_s16le", false, true);
        row("voc", "voc", "of=voc|oac=pcm_u8", false, true);
    }

    void formats()
    {
        QFETCH(QString, ext);
        QFETCH(QString, opts);
        QFETCH(QString, size);
        QFETCH(int, rate);
        QFETCH(bool, video);
        QFETCH(bool, audio);
        QString path = tmp.filePath("fmt/" + QString(QTest::currentDataTag()) + "." + ext);
        QDir().mkpath(tmp.filePath("fmt"));
        QStringList graph;
        if (video)
            graph << QString("testsrc=duration=4:size=%1:rate=25[out0]").arg(size);
        if (audio)
            graph << QString("sine=duration=4:sample_rate=%1[out%2]").arg(rate).arg(video ? 1 : 0);
        if (!encode(path, graph.join(';'), opts))
            QSKIP("this libmpv build can't write the sample file");
        p->openFiles({path});
        QTRY_COMPARE_WITH_TIMEOUT(status(), QString("Playing"), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(pos() > 0, 5000);
        p->command({"set", "pause", "yes"}); // keep it loaded while we look
        QTRY_COMPARE(status(), QString("Paused"));
        QCOMPARE(p->prop("track-list/count").toInt(), int(video) + int(audio));
        if (video)
            QVERIFY2(!p->prop("video-codec").isEmpty(), "no video decoder");
        if (audio)
            QVERIFY2(!p->prop("audio-codec-name").isEmpty(), "no audio decoder");
    }

    void vlcPlaylistFormats()
    {
        QString spaced = tmp.filePath("my clip & co.mkv");
        QFile::copy(clipA, spaced);
        QString spacedUrl = QUrl::fromLocalFile(spaced).toString(QUrl::FullyEncoded).replace("&", "&amp;");
        QString bUrl = QUrl::fromLocalFile(clipB).toString(QUrl::FullyEncoded);
        struct Case { QString name; QByteArray content; QStringList expected; };
        const QList<Case> cases = {
            {"list.pls", "[playlist]\nFile1=a.mkv\nTitle1=A\nFile2=" + clipB.toUtf8() + "\nNumberOfEntries=2\n", {clipA, clipB}},
            {"list.xspf", "<?xml version=\"1.0\"?><playlist version=\"1\"><trackList>\n<track><location>"
                + spacedUrl.toUtf8() + "</location></track>\n<track><location>b.mkv</location></track>\n</trackList></playlist>", {spaced, clipB}},
            {"list.asx", "<asx version=\"3.0\"><entry><REF HREF=\"b.mkv\"/></entry><entry><ref href=\""
                + bUrl.toUtf8() + "\"/></entry></asx>", {clipB, clipB}},
            {"list.wpl", "<?wpl version=\"1.0\"?><smil><body><seq><media src=\"a.mkv\"/><media src=\"b.mkv\"/></seq></body></smil>", {clipA, clipB}},
            {"list.ram", "a.mkv\r\n" + clipB.toUtf8() + "\r\n", {clipA, clipB}},
        };
        for (const Case &c : cases) {
            p->openFiles({writeFile(c.name, c.content)});
            QVERIFY2(sources() == c.expected, qPrintable(c.name + ": " + sources().join(" | ")));
        }
        QTRY_COMPARE_WITH_TIMEOUT(status(), QString("Playing"), 5000);
    }

    void hlsManifestIsOneStream()
    {
        QString m3u8 = tmp.filePath("hls/index.m3u8");
        QDir().mkpath(tmp.filePath("hls"));
        if (!encode(m3u8, "testsrc=duration=6:size=160x120:rate=25[out0];sine=duration=6[out1]", "of=hls|ovc=libx264|oac=aac"))
            QSKIP("this libmpv build can't write HLS");
        p->openFiles({m3u8});
        QCOMPARE(sources(), QStringList({m3u8}));
        QTRY_COMPARE_WITH_TIMEOUT(status(), QString("Playing"), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(pos() > 0, 5000);
    }

    void discImagesAndIfo()
    {
        QByteArray pad(70000, '\0');
        p->openFiles({writeFile("dvd.iso", pad + "VIDEO_TS" + pad)}, true);
        p->openFiles({writeFile("bd.iso", pad + QByteArray("\0B\0D\0M\0V", 8) + pad)}, true);
        p->openFiles({writeFile("data.iso", pad + "README" + pad)}, true);
        QString ifo = writeFile("dvdfolder/VIDEO_TS/VIDEO_TS.IFO", "DVDVIDEO-VMG");
        p->openFiles({ifo}, true);
        auto url = [this](int r) { return p->playlist->item(r)->data(Qt::UserRole).toString(); };
        QCOMPARE(url(0), QString("dvd://"));
        QCOMPARE(url(1), QString("bd://"));
        QCOMPARE(url(2), tmp.filePath("data.iso"));
        QCOMPARE(url(3), QString("dvd://"));
        QCOMPARE(sources().value(3), tmp.filePath("dvdfolder/VIDEO_TS"));
    }

    void fileUrlArguments()
    {
        p->openFiles({QUrl::fromLocalFile(clipA).toString()});
        QCOMPARE(sources(), QStringList({clipA}));
        QTRY_COMPARE_WITH_TIMEOUT(status(), QString("Playing"), 5000);
    }

    void youtubeSupportOn()
    {
        QCOMPARE(p->prop("ytdl"), QString("yes"));
    }

    void httpStream()
    {
        QProcess server;
        server.setWorkingDirectory(tmp.path());
        server.start("python3", {"-u", "-m", "http.server", "0", "--bind", "127.0.0.1"});
        QVERIFY(server.waitForStarted());
        QByteArray out; // "Serving HTTP on 127.0.0.1 port N ..." may arrive in pieces
        QRegularExpressionMatch m;
        QTRY_VERIFY_WITH_TIMEOUT((out += server.readAllStandardOutput(), m = QRegularExpression("port (\\d+)").match(out)).hasMatch(), 5000);
        p->openFiles({"http://127.0.0.1:" + m.captured(1) + "/a.mkv"});
        QTRY_COMPARE_WITH_TIMEOUT(status(), QString("Playing"), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(pos() > 0, 5000);
        p->close();
        server.kill();
        server.waitForFinished();
    }

    void mprisRegistered()
    {
        QVERIFY2(p->mpris->serviceName().startsWith("org.mpris.MediaPlayer2.reelplay"), qPrintable(p->mpris->serviceName()));
        QCOMPARE(mprisGet("Identity", "org.mpris.MediaPlayer2"), QString("(<'Reelplay'>,)"));
        QCOMPARE(mprisGet("PlaybackStatus"), QString("(<'Stopped'>,)"));
        QCOMPARE(mprisGet("CanControl"), QString("(<true>,)"));
    }

    void mprisControls()
    {
        startClips();
        QCOMPARE(mprisGet("PlaybackStatus"), QString("(<'Playing'>,)"));
        mpris("PlayPause");
        QTRY_COMPARE(status(), QString("Paused"));
        QCOMPARE(mprisGet("PlaybackStatus"), QString("(<'Paused'>,)"));
        mpris("Play");
        QTRY_COMPARE(status(), QString("Playing"));
        mpris("Pause");
        QTRY_COMPARE(status(), QString("Paused"));
        mpris("Next");
        QTRY_COMPARE(p->current, 1);
        QCOMPARE(mprisGet("CanGoNext"), QString("(<false>,)"));
        mpris("Previous");
        QTRY_COMPARE(p->current, 0);
        QTRY_VERIFY_WITH_TIMEOUT(pos() >= 0 && !p->prop("time-pos").isEmpty(), 5000);
        QString r = mpris("SetPosition", {"'/org/reelplay/track/0'", "30000000"});
        QVERIFY2(r == "()", qPrintable(r));
        QTRY_VERIFY(qAbs(pos() - 30) < 1);
        mpris("Seek", {"int64 -10000000"}); // a bare "-1..." would be read as a gdbus option
        QTRY_VERIFY(pos() <= 21); // relative seeks snap to a keyframe (this clip: every 25 s)
        gdbus({"--method", "org.freedesktop.DBus.Properties.Set", "org.mpris.MediaPlayer2.Player", "Volume", "<0.5>"});
        QTRY_COMPARE(p->volBar->value(), 50);
        mpris("Stop");
        QTRY_COMPARE(status(), QString("Stopped"));
        QCOMPARE(mprisGet("PlaybackStatus"), QString("(<'Stopped'>,)"));
    }

    void mprisMetadata()
    {
        startClips();
        QTRY_VERIFY(p->duration > 0);
        QString meta = mprisGet("Metadata");
        QVERIFY2(meta.contains("'xesam:title': <'a.mkv'>"), qPrintable(meta));
        QVERIFY2(meta.contains("'mpris:trackid': <objectpath '/org/reelplay/track/0'>"), qPrintable(meta));
        QVERIFY2(meta.contains("'mpris:length': <int64 60"), qPrintable(meta));
        QVERIFY2(meta.contains("'xesam:url': <'file://"), qPrintable(meta));
    }

    void mprisSignals()
    {
        startClips();
        QProcess monitor;
        monitor.start("gdbus", {"monitor", "--session", "--dest", p->mpris->serviceName()});
        QVERIFY(monitor.waitForStarted());
        QTest::qWait(300); // let the monitor subscribe
        QByteArray out;
        key("Space");
        QTRY_VERIFY((out += monitor.readAllStandardOutput()).contains("'PlaybackStatus': <'Paused'>"));
        key("Shift+Right");
        QTRY_VERIFY((out += monitor.readAllStandardOutput()).contains("org.mpris.MediaPlayer2.Player.Seeked"));
        monitor.kill();
        monitor.waitForFinished();
    }

    // keyClick() only knows keys with ASCII text; media keys have none.
    void mediaKey(Qt::Key k) { QTest::sendKeyEvent(QTest::Click, p, k, QString(), Qt::NoModifier); }

    void mediaKeysWhenFocused()
    {
        startClips();
        mediaKey(Qt::Key_MediaTogglePlayPause);
        QTRY_COMPARE(status(), QString("Paused"));
        mediaKey(Qt::Key_MediaPlay);
        QTRY_COMPARE(status(), QString("Playing"));
        mediaKey(Qt::Key_MediaPause);
        QTRY_COMPARE(status(), QString("Paused"));
        mediaKey(Qt::Key_MediaNext);
        QCOMPARE(p->current, 1);
        mediaKey(Qt::Key_MediaPrevious);
        QCOMPARE(p->current, 0);
        mediaKey(Qt::Key_MediaStop);
        QTRY_COMPARE(status(), QString("Stopped"));
    }

    // What Cinnamon/GNOME/MATE send after GrabMediaPlayerKeys.
    void settingsDaemonMediaKeys()
    {
        startClips();
        p->mpris->mediaPlayerKeyPressed("some-other-app", "Play"); // broadcast for another grabber
        QTest::qWait(300);
        QCOMPARE(status(), QString("Playing"));
        p->mpris->mediaPlayerKeyPressed("reelplay", "Play");
        QTRY_COMPARE(status(), QString("Paused"));
        p->mpris->mediaPlayerKeyPressed("reelplay", "Play");
        QTRY_COMPARE(status(), QString("Playing"));
        p->mpris->mediaPlayerKeyPressed("reelplay", "Pause");
        QTRY_COMPARE(status(), QString("Paused"));
        p->mpris->mediaPlayerKeyPressed("reelplay", "Next");
        QCOMPARE(p->current, 1);
        p->mpris->mediaPlayerKeyPressed("reelplay", "Previous");
        QCOMPARE(p->current, 0);
        p->mpris->mediaPlayerKeyPressed("reelplay", "Stop");
        QTRY_COMPARE(status(), QString("Stopped"));
    }

    // A file opened on its own (e.g. from the file manager): Next/Previous walk its folder.
    void nextPreviousWalkFolderOfLoneFile()
    {
        QDir d(tmp.filePath("season"));
        d.mkpath(".");
        for (const char *n : {"ep1.mkv", "ep2.mkv", "ep10.mkv"})
            QFile::copy(clipA, d.filePath(n));
        QFile(d.filePath("notes.txt")).open(QIODevice::WriteOnly);
        auto playing = [this] { return p->playlist->count() == 1 ? p->playlist->item(0)->text() : QString("count=%1").arg(p->playlist->count()); };

        p->openFiles({d.filePath("ep2.mkv")});
        QTRY_COMPARE_WITH_TIMEOUT(status(), QString("Playing"), 5000);
        QCOMPARE(mprisGet("CanGoNext"), QString("(<true>,)"));
        key("PgDown");
        QCOMPARE(playing(), QString("ep10.mkv")); // natural order, skips notes.txt
        QTRY_COMPARE_WITH_TIMEOUT(status(), QString("Playing"), 5000);
        QTRY_COMPARE(mprisGet("CanGoNext"), QString("(<false>,)"));
        key("PgDown"); // last file: stays
        QCOMPARE(playing(), QString("ep10.mkv"));
        p->mpris->mediaPlayerKeyPressed("reelplay", "Previous"); // keyboard media key
        QCOMPARE(playing(), QString("ep2.mkv"));
        mpris("Previous");
        QTRY_COMPARE(playing(), QString("ep1.mkv"));
        QTRY_COMPARE(mprisGet("CanGoPrevious"), QString("(<false>,)"));
        key("PgUp"); // first file: stays
        QCOMPARE(playing(), QString("ep1.mkv"));
        QTRY_COMPARE_WITH_TIMEOUT(status(), QString("Playing"), 5000);
    }

    void parseTimeFormats()
    {
        QCOMPARE(parseTime("1:23:45"), 5025.0);
        QCOMPARE(parseTime(" 83:45 "), 5025.0);
        QCOMPARE(parseTime("45"), 45.0);
        QCOMPARE(parseTime("1:30.5"), 90.5);
        QCOMPARE(parseTime(""), -1.0);
        QCOMPARE(parseTime("1:2:3:4"), -1.0);
        QCOMPARE(parseTime("abc"), -1.0);
        QCOMPARE(parseTime("-5"), -1.0);
    }

    void goToTime()
    {
        startClips();
        key("Space");
        QTRY_COMPARE(status(), QString("Paused"));
        onModal([](QDialog *d) {
            QCOMPARE(d->findChild<QLineEdit *>()->text().left(6), QString("00:00:"));
            d->findChild<QLineEdit *>()->setText("0:00:42");
            d->accept();
        });
        key("Ctrl+G");
        QTRY_VERIFY(qAbs(pos() - 42) < 0.5);
        QVERIFY(QTest::qWaitForWindowActive(p));
        onModal([](QDialog *d) { d->findChild<QLineEdit *>()->setText("nonsense"); d->accept(); });
        key("Ctrl+G");
        QTest::qWait(200);
        QVERIFY(qAbs(pos() - 42) < 0.5); // invalid input doesn't seek
    }

    void chapters()
    {
        // An mpv EDL of two segments: each segment is a chapter (at 0 s and 60 s).
        QString edl = writeFile("chapters.edl", "# mpv EDL v0\n" + clipA.toUtf8() + "\n" + clipB.toUtf8() + "\n");
        p->openFiles({edl});
        QTRY_COMPARE_WITH_TIMEOUT(status(), QString("Playing"), 5000);
        QTRY_COMPARE(p->chapters.size(), 2);
        QCOMPARE(p->chapters[0], 0.0);
        QVERIFY(qAbs(p->chapters[1] - 60) < 0.5);
        emit p->chapterMenu->aboutToShow();
        QCOMPARE(p->chapterMenu->actions().size(), 2);
        QVERIFY(p->chapterMenu->actions()[0]->isChecked());
        QVERIFY2(p->chapterMenu->actions()[1]->text().endsWith("(00:01:00)"), qPrintable(p->chapterMenu->actions()[1]->text()));
        key("Ctrl+PgDown");
        QTRY_VERIFY(pos() >= 59.5);
        key("Ctrl+PgUp"); // back to the start of chapter 2's... previous = chapter 1
        QTRY_VERIFY(pos() < 30);
        p->chapterMenu->actions()[1]->trigger(); // stale action list still works
        QTRY_VERIFY(pos() >= 59.5);
        p->openFiles({clipA}); // a file without chapters
        QTRY_COMPARE(p->chapters.size(), 0);
        emit p->chapterMenu->aboutToShow();
        QCOMPARE(p->chapterMenu->actions().first()->text(), QString("No chapters"));
    }

    void autoNextInFolder()
    {
        QDir d(tmp.filePath("autonext"));
        d.mkpath(".");
        QFile::copy(clipA, d.filePath("ep1.mkv"));
        QFile::copy(clipA, d.filePath("ep2.mkv"));
        auto playing = [this] { return p->playlist->count() ? p->playlist->item(0)->text() : QString(); };

        p->openFiles({d.filePath("ep1.mkv")}); // off by default: stops at the end
        QTRY_COMPARE_WITH_TIMEOUT(status(), QString("Playing"), 5000);
        p->command({"seek", "59", "absolute"});
        QTRY_COMPARE_WITH_TIMEOUT(status(), QString("Stopped"), 8000);
        QCOMPARE(playing(), QString("ep1.mkv"));

        QSettings().setValue("autoNext", true);
        p->openFiles({d.filePath("ep1.mkv")});
        QTRY_COMPARE_WITH_TIMEOUT(status(), QString("Playing"), 5000);
        p->command({"seek", "59", "absolute"});
        QTRY_COMPARE_WITH_TIMEOUT(playing(), QString("ep2.mkv"), 8000);
        QTRY_VERIFY_WITH_TIMEOUT(pos() > 0 && pos() < 30, 5000); // ep2 really started
        QCOMPARE(status(), QString("Playing"));
        p->command({"seek", "59", "absolute"}); // last in folder: stops
        QTRY_COMPARE_WITH_TIMEOUT(status(), QString("Stopped"), 8000);
    }

    void subtitleOptions()
    {
        startClips();
        QString font;
        onModal([&font](QDialog *d) {
            auto *combo = d->findChild<QFontComboBox *>("subFont");
            combo->setCurrentIndex(combo->count() > 1 ? 1 : 0);
            font = combo->currentFont().family();
            d->findChild<QSpinBox *>("subSize")->setValue(70);
            d->findChild<QSpinBox *>("subPos")->setValue(90);
            d->findChild<QPushButton *>("subColor")->setProperty("color", "#ffff00");
            d->accept();
        });
        trigger("&Options...");
        QCOMPARE(p->prop("sub-font"), font);
        QCOMPARE(p->prop("sub-font-size").toDouble(), 70.0);
        QCOMPARE(p->prop("sub-pos").toDouble(), 90.0);
        QVERIFY2(p->prop("sub-color").contains("FFFF00", Qt::CaseInsensitive), qPrintable(p->prop("sub-color")));
        p->close();
        newPlayer(); // remembered
        QCOMPARE(p->prop("sub-font-size").toDouble(), 70.0);
        QCOMPARE(p->prop("sub-font"), font);
    }

    void subtitlesFromSubsFolder()
    {
        QDir d(tmp.filePath("movie"));
        d.mkpath("Subs");
        QFile::copy(clipA, d.filePath("film.mkv"));
        writeFile("movie/Subs/film.en.srt", "1\n00:00:00,000 --> 00:00:50,000\nHello\n\n");
        p->openFiles({d.filePath("film.mkv")});
        QTRY_COMPARE_WITH_TIMEOUT(status(), QString("Playing"), 5000);
        QTRY_COMPARE(p->subMenu->actions().size(), 2); // "Disabled" + film.en.srt
    }

    // Launch the real program twice: the second hands its file to the first and exits.
    void singleInstance()
    {
        QDBusConnectionInterface *bus = QDBusConnection::sessionBus().interface();
        if (bus->isServiceRegistered("org.reelplay.Instance"))
            QSKIP("reelplay is already running on this desktop");
        QTemporaryDir home;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert("XDG_CONFIG_HOME", home.filePath("config"));
        env.insert("XDG_DATA_HOME", home.filePath("data"));
        QProcess first;
        first.setProcessEnvironment(env);
        first.start(REELPLAY_BIN, {clipA});
        QVERIFY(first.waitForStarted());
        QTRY_VERIFY_WITH_TIMEOUT(bus->isServiceRegistered("org.reelplay.Instance"), 10000);

        QProcess second;
        second.setProcessEnvironment(env);
        second.setWorkingDirectory(tmp.path());
        second.start(REELPLAY_BIN, {"b.mkv"}); // relative: must be resolved by the sender
        QVERIFY(second.waitForFinished(8000));
        QCOMPARE(second.exitCode(), 0);

        // The first window now plays b.mkv: ask its MPRIS interface.
        QString name;
        for (const QString &n : bus->registeredServiceNames().value())
            if (n.startsWith("org.mpris.MediaPlayer2.reelplay") && bus->servicePid(n).value() == uint(first.processId()))
                name = n;
        QVERIFY(!name.isEmpty());
        QString meta;
        QTRY_VERIFY_WITH_TIMEOUT((meta = runGdbus(name)).contains("'xesam:title': <'b.mkv'>"), 5000);
        first.terminate();
        if (!first.waitForFinished(5000))
            first.kill();
    }

    QString runGdbus(const QString &dest)
    {
        QProcess proc;
        proc.start("gdbus", {"call", "--session", "--dest", dest, "--object-path", "/org/mpris/MediaPlayer2", "--method",
                             "org.freedesktop.DBus.Properties.Get", "org.mpris.MediaPlayer2.Player", "Metadata"});
        proc.waitForFinished(3000);
        return proc.readAllStandardOutput();
    }

    void chromeToggles()
    {
        startClips();
        key("Ctrl+1");
        QTRY_VERIFY(!p->controls->isVisible());
        key("Ctrl+1");
        QTRY_VERIFY(p->controls->isVisible());
        key("Ctrl+0");
        QTRY_VERIFY(!p->menuBar()->isVisible());
        key("Ctrl+2");
        QTRY_VERIFY(!p->statusBar()->isVisible());
        key("Ctrl+2");
        key("Ctrl+0");
        QTRY_VERIFY(p->menuBar()->isVisible() && p->statusBar()->isVisible());
    }

    void hiddenPartsSurviveFullscreen()
    {
        startClips();
        key("Ctrl+0");
        QTRY_VERIFY(!p->menuBar()->isVisible());
        key("Alt+Return");
        QTRY_VERIFY(p->isFullScreen());
        key("Esc");
        QTRY_VERIFY(!p->isFullScreen());
        QVERIFY(!p->menuBar()->isVisible()); // still hidden after leaving fullscreen
        key("Ctrl+0");
        QTRY_VERIFY(p->menuBar()->isVisible());
    }

    bool wmState(const QString &atom)
    {
        QProcess xprop;
        xprop.start("xprop", {"-id", QString::number(p->winId()), "_NET_WM_STATE"});
        xprop.waitForFinished(3000);
        return QString::fromUtf8(xprop.readAllStandardOutput()).contains(atom);
    }

    void alwaysOnTop()
    {
        if (QStandardPaths::findExecutable("xprop").isEmpty())
            QSKIP("xprop is needed to check the window manager state");
        startClips();
        key("Ctrl+A");
        QTRY_VERIFY(p->windowHandle()->flags().testFlag(Qt::WindowStaysOnTopHint));
        QTRY_VERIFY(wmState("_NET_WM_STATE_ABOVE"));
        key("Ctrl+A");
        QTRY_VERIFY(!p->windowHandle()->flags().testFlag(Qt::WindowStaysOnTopHint));
        QTRY_VERIFY(!wmState("_NET_WM_STATE_ABOVE"));
        QTRY_VERIFY(pos() > 0); // playback carries on
    }

    void statsOverlay()
    {
        startClips();
        key("Ctrl+J");
        QTRY_VERIFY(p->statsTimer->isActive());
        QTest::qWait(1200); // let the overlay refresh a few times
        key("Ctrl+J");
        QVERIFY(!p->statsTimer->isActive());
    }

    void videoAdjustments()
    {
        startClips();
        onModal([](QDialog *d) {
            d->findChild<QSlider *>("brightness")->setValue(40);
            d->accept();
        });
        trigger("&Adjust Colors...");
        QTRY_COMPARE(p->prop("brightness").toDouble(), 40.0);
        QCOMPARE(QSettings().value("video/brightness").toInt(), 40);
        p->close();
        newPlayer();
        QTRY_COMPARE(p->prop("brightness").toDouble(), 40.0); // restored at startup
    }

    void videoTransforms()
    {
        startClips();
        trigger("Rotate &Clockwise");
        QTRY_COMPARE(p->prop("video-rotate").toInt(), 90);
        trigger("Rotate &Counter-clockwise");
        trigger("Rotate &Counter-clockwise");
        QTRY_COMPARE(p->prop("video-rotate").toInt(), 270);
        trigger("Flip &Horizontal");
        QTest::qWait(500);
        QTRY_VERIFY(p->prop("vf").contains("hflip"));
        trigger("Flip &Horizontal");
        QTRY_VERIFY(!p->prop("vf").contains("hflip"));
        trigger("&Deinterlace");
        QTRY_COMPARE(p->prop("deinterlace"), QString("yes"));
        if (realVideoOutput()) {
            trigger("Crop 16:9"); // 320x240 source: centred 320x180 strip
            QTRY_COMPARE(p->prop("video-crop"), QString("320x180+0+30"));
            trigger("No Crop");
            QTRY_COMPARE(p->prop("video-crop"), QString(""));
        }
    }

    void audioOutput()
    {
        startClips();
        trigger("&Normalize Volume");
        QTRY_VERIFY(p->prop("af").contains("dynaudnorm"));
        trigger("&Bass Boost");
        QTRY_VERIFY(p->prop("af").contains("equalizer=f=31"));
        QVERIFY(p->prop("af").contains("dynaudnorm")); // the normalizer stays on
        trigger("&Flat");
        QTRY_VERIFY(!p->prop("af").contains("equalizer"));
        trigger("&Normalize Volume");
        QTRY_VERIFY(p->prop("af").isEmpty());
    }

    void equalizerDialog()
    {
        startClips();
        onModal([](QDialog *d) {
            d->findChild<QCheckBox *>("eqEnabled")->setChecked(true);
            d->findChild<QSlider *>("band8")->setValue(-4); // 8 kHz
            d->findChild<QCheckBox *>("eqCompressor")->setChecked(true);
            d->findChild<QCheckBox *>("eqWiden")->setChecked(true);
            d->accept();
        });
        trigger("&Open Equalizer...");
        QTRY_VERIFY(p->prop("af").contains("equalizer=f=8000"));
        QVERIFY(p->prop("af").contains("acompressor"));
        QVERIFY(p->prop("af").contains("extrastereo"));
        QCOMPARE(QSettings().value("eq/gains").toStringList().value(8), QString("-4"));
    }

    void audioDevice()
    {
        startClips();
        emit p->audioDeviceMenu->aboutToShow();
        QList<QAction *> devices = p->audioDeviceMenu->actions();
        if (devices.isEmpty())
            QSKIP("mpv lists no audio devices here");
        QString name = p->prop("audio-device-list/0/name");
        devices.first()->trigger();
        QTRY_COMPARE(p->prop("audio-device"), name);
        QCOMPARE(QSettings().value("audioDevice").toString(), name);
    }

    void afterPlaybackExit()
    {
        trigger("&Exit Player");
        QCOMPARE(QSettings().value("afterPlayback").toString(), QString("exit"));
        p->openFiles({clipA});
        QTRY_COMPARE_WITH_TIMEOUT(status(), QString("Playing"), 5000);
        p->command({"seek", "59", "absolute"}); // near the end of the 60 s clip
        QTRY_VERIFY_WITH_TIMEOUT(!p->isVisible(), 10000);
    }

    // A file that fails to load on the last slot stops playback but must not run the
    // After Playback action: with "Exit Player" the window has to stay open.
    void errorDoesNotFinishPlaylist()
    {
        trigger("&Exit Player");
        p->openFiles({tmp.filePath("missing.mkv")});
        QTest::qWait(3000);
        QVERIFY(p->stopped);
        QVERIFY(p->isVisible());
    }

    void favorites()
    {
        p->openFiles({clipA});
        QTRY_COMPARE_WITH_TIMEOUT(status(), QString("Playing"), 5000);
        p->command({"seek", "30", "absolute"});
        QTRY_VERIFY(qAbs(pos() - 30) < 2);
        key("Ctrl+Shift+F");
        QCOMPARE(QSettings().value("favorites").toStringList().size(), 1);
        p->openFiles({clipB});
        QTRY_COMPARE_WITH_TIMEOUT(status(), QString("Playing"), 5000);
        emit p->favoritesMenu->aboutToShow();
        QAction *entry = nullptr;
        for (QAction *a : p->favoritesMenu->actions())
            if (a->text().contains("a.mkv"))
                entry = a;
        QVERIFY(entry);
        entry->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(p->playlist->item(0)->text() == "a.mkv" && qAbs(pos() - 30) < 3, 8000);
    }

    bool hasAction(QMenu *m, const QString &text)
    {
        for (QAction *a : m->actions())
            if (a->text() == text)
                return true;
        return false;
    }

    void videoFilters()
    {
        startClips();
        trigger("S&mooth (Spline36)");
        QTRY_COMPARE(p->prop("scale"), QString("spline36"));
        QCOMPARE(QSettings().value("scale").toString(), QString("spline36"));
        trigger("&HDR Tone Mapping");
        QTRY_COMPARE(p->prop("tone-mapping"), QString("clip"));
        trigger("&HDR Tone Mapping");
        QTRY_COMPARE(p->prop("tone-mapping"), QString("auto"));
        trigger("&Denoise");
        QTRY_VERIFY(p->prop("vf").contains("hqdn3d"));
        trigger("&Sharpen");
        QTRY_VERIFY(p->prop("vf").contains("unsharp"));
        trigger("&Denoise");
        QTRY_VERIFY(!p->prop("vf").contains("hqdn3d"));
        QVERIFY(p->prop("vf").contains("unsharp")); // sharpen stays on
    }

    // Undecoded passthrough can silence a stereo device, so it must start off.
    void passthroughIsOptIn()
    {
        startClips();
        QCOMPARE(p->prop("audio-spdif"), QString(""));
        trigger("&S/PDIF Passthrough (AC3/DTS)");
        QTRY_VERIFY(p->prop("audio-spdif").contains("ac3"));
        QCOMPARE(QSettings().value("passthrough").toBool(), true);
        trigger("&S/PDIF Passthrough (AC3/DTS)");
        QTRY_COMPARE(p->prop("audio-spdif"), QString(""));
    }

    void skipIntroChapter()
    {
        if (QStandardPaths::findExecutable("ffmpeg").isEmpty())
            QSKIP("ffmpeg is needed to write chapter titles");
        QString meta = writeFile("chapters.txt",
            ";FFMETADATA1\n[CHAPTER]\nTIMEBASE=1/1000\nSTART=0\nEND=20000\ntitle=Intro\n"
            "[CHAPTER]\nTIMEBASE=1/1000\nSTART=20000\nEND=60000\ntitle=Main story\n");
        QString out = tmp.filePath("chaptered.mkv");
        QProcess ff;
        ff.start("ffmpeg", {"-v", "error", "-y", "-i", clipA, "-i", meta, "-map_metadata", "1", "-map_chapters", "1", "-c", "copy", out});
        QVERIFY(ff.waitForFinished(60000));
        QCOMPARE(ff.exitCode(), 0);
        p->openFiles({out});
        QTRY_COMPARE_WITH_TIMEOUT(status(), QString("Playing"), 5000);
        QTRY_COMPARE(p->chapters.size(), 2);
        key("S");
        QTRY_VERIFY_WITH_TIMEOUT(qAbs(pos() - 20) < 1.5, 5000); // the intro is skipped
        QTest::qWait(500);
        key("S"); // "Main story" is not skippable: nothing moves
        QTest::qWait(300);
        QVERIFY(pos() > 19 && pos() < 26);
    }

    void bookmarksPerFile()
    {
        p->openFiles({clipA});
        QTRY_COMPARE_WITH_TIMEOUT(status(), QString("Playing"), 5000);
        p->command({"seek", "20", "absolute"});
        QTRY_VERIFY(qAbs(pos() - 20) < 2);
        key("Ctrl+B");
        QCOMPARE(QSettings().value("bookmarks").toStringList().size(), 1);
        p->openFiles({clipB});
        QTRY_COMPARE_WITH_TIMEOUT(status(), QString("Playing"), 5000);
        emit p->bookmarksMenu->aboutToShow();
        QVERIFY(!hasAction(p->bookmarksMenu, "00:00:20")); // the bookmark belongs to clipA
        p->openFiles({clipA});
        QTRY_COMPARE_WITH_TIMEOUT(status(), QString("Playing"), 5000);
        emit p->bookmarksMenu->aboutToShow();
        QVERIFY(hasAction(p->bookmarksMenu, "00:00:20"));
        p->command({"seek", "2", "absolute"});
        QTRY_VERIFY(pos() < 5);
        for (QAction *a : p->bookmarksMenu->actions())
            if (a->text() == "00:00:20")
                a->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(qAbs(pos() - 20) < 2, 5000);
    }

    void recordStream()
    {
        p->openFiles({clipA});
        QTRY_COMPARE_WITH_TIMEOUT(status(), QString("Playing"), 5000);
        const QString path = tmp.filePath("recorded.mkv");
        p->startRecording(path);
        QTRY_COMPARE(p->prop("stream-record"), path); // the set is async; mpv may not have applied it yet
        QVERIFY(p->recordAct->isChecked());
        QTest::qWait(2000);
        p->stopRecording();
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo(path).size() > 0, 5000); // mpv writes the file on stop
        QCOMPARE(p->prop("stream-record"), QString(""));
        QVERIFY(!p->recordAct->isChecked());
        // Changing file ends a recording: one recording never spans two files.
        p->startRecording(tmp.filePath("second.mkv"));
        p->openFiles({clipB});
        QTRY_COMPARE(p->prop("stream-record"), QString(""));
        QVERIFY(!p->recordAct->isChecked());
    }

    bool cropChecked(const QString &text)
    {
        for (QAction *a : p->findChildren<QAction *>())
            if (a->text() == text && a->isChecked())
                return true;
        return false;
    }

    void videoChoicesPersist()
    {
        startClips();
        trigger("Rotate &Clockwise");
        trigger("Flip &Horizontal");
        trigger("&Denoise");
        trigger("&Deinterlace");
        trigger("Crop 16:9");
        QTRY_VERIFY(p->prop("vf").contains("hflip"));
        p->close();
        newPlayer();
        p->openFiles({clipA});
        QTRY_COMPARE_WITH_TIMEOUT(status(), QString("Playing"), 5000);
        QCOMPARE(p->prop("video-rotate").toInt(), 90);
        QVERIFY(p->prop("vf").contains("hflip"));
        QVERIFY(p->prop("vf").contains("hqdn3d"));
        QCOMPARE(p->prop("deinterlace"), QString("yes"));
        if (realVideoOutput())
            QTRY_COMPARE_WITH_TIMEOUT(p->prop("video-crop"), QString("320x180+0+30"), 5000);
        QVERIFY(cropChecked("Crop 16:9"));
        // Reapplying the saved settings must not stack a second copy of a filter.
        p->applySettings();
        QCOMPARE(p->prop("vf").count("hflip"), 1);
    }

    void visualizerForAudioOnly()
    {
        if (QStandardPaths::findExecutable("ffmpeg").isEmpty())
            QSKIP("ffmpeg is needed to make an audio-only file");
        const QString mp3 = tmp.filePath("tone.mp3");
        QProcess ff;
        ff.start("ffmpeg", {"-v", "error", "-y", "-f", "lavfi", "-i", "sine=duration=6:sample_rate=44100", "-c:a", "libmp3lame", mp3});
        QVERIFY(ff.waitForFinished(60000));
        QCOMPARE(ff.exitCode(), 0);
        trigger("&Visualizer (audio files)");
        p->openFiles({mp3});
        QTRY_COMPARE_WITH_TIMEOUT(status(), QString("Playing"), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(p->prop("video-params/w").toInt(), 640, 5000);
        // A file with real video is left alone.
        p->openFiles({clipA});
        QTRY_COMPARE_WITH_TIMEOUT(status(), QString("Playing"), 5000);
        QTRY_COMPARE(p->prop("lavfi-complex"), QString(""));
    }

    // Clearing the spectrum graph must hand the audio and video tracks back, or playback stops.
    void visualiserDoesNotStallPlayback()
    {
        if (QStandardPaths::findExecutable("ffmpeg").isEmpty())
            QSKIP("ffmpeg is needed to make an audio-only file");
        startClips();
        const QString mp3 = tmp.filePath("long-tone.mp3");
        QProcess ff;
        ff.start("ffmpeg", {"-v", "error", "-y", "-f", "lavfi", "-i", "sine=duration=20:sample_rate=44100", "-c:a", "libmp3lame", mp3});
        QVERIFY(ff.waitForFinished(60000));
        QCOMPARE(ff.exitCode(), 0);
        trigger("&Visualizer (audio files)");
        p->openFiles({mp3});
        QTRY_COMPARE_WITH_TIMEOUT(p->prop("video-params/w").toInt(), 640, 5000);
        // Visualiser off while the sound plays: the picture goes, the sound must go on.
        trigger("&Visualizer (audio files)");
        double before = pos();
        QTRY_VERIFY_WITH_TIMEOUT(pos() > before + 0.5, 5000);
        // Visualiser on, then a file with real video: playback must carry on there too.
        trigger("&Visualizer (audio files)");
        QTRY_COMPARE_WITH_TIMEOUT(p->prop("video-params/w").toInt(), 640, 5000);
        p->openFiles({clipA});
        QTRY_COMPARE_WITH_TIMEOUT(status(), QString("Playing"), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(p->prop("vid") != QString("no"), 5000); // the picture comes back too
        before = pos();
        QTRY_VERIFY_WITH_TIMEOUT(pos() > before + 0.5, 5000);
    }

    void abSectionSave()
    {
        if (QStandardPaths::findExecutable("ffmpeg").isEmpty())
            QSKIP("ffmpeg is needed to cut the section");
        p->openFiles({clipA});
        QTRY_COMPARE_WITH_TIMEOUT(status(), QString("Playing"), 5000);
        p->command({"set", "ab-loop-a", "2"});
        p->command({"set", "ab-loop-b", "6"});
        QTRY_COMPARE(p->prop("ab-loop-b").toDouble(), 6.0); // markers are applied asynchronously
        QTRY_COMPARE(p->prop("ab-loop-a").toDouble(), 2.0);
        const QString out = tmp.filePath("section.mkv");
        p->saveAbSection(out);
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo(out).size() > 0, 20000);
    }

    void screenshotSeries()
    {
        const QString dir = tmp.filePath("series");
        QSettings().setValue("screenshotDir", dir);
        p->openFiles({clipA});
        QTRY_COMPARE_WITH_TIMEOUT(status(), QString("Playing"), 5000);
        p->startScreenshotSeries(1);
        QTest::qWait(2600);
        p->stopScreenshotSeries();
        QVERIFY(QDir(dir).entryList({"*.png"}, QDir::Files).size() >= 2);
        QVERIFY(!p->seriesTimer->isActive());
    }

    void pitchShift()
    {
        startClips();
        onModal([](QDialog *d) {
            d->findChild<QSlider *>("pitch")->setValue(2);
            d->accept();
        });
        trigger("&Open Equalizer...");
        QTRY_VERIFY(p->prop("af").contains("asetrate="));
        QVERIFY(p->prop("af").contains("atempo="));
        QCOMPARE(QSettings().value("eq/pitch").toInt(), 2);
        // Zero semitones adds no pitch filter at all.
        onModal([](QDialog *d) {
            d->findChild<QSlider *>("pitch")->setValue(0);
            d->accept();
        });
        trigger("&Open Equalizer...");
        QTRY_VERIFY(!p->prop("af").contains("asetrate="));
    }

    void seekPreviewFrames()
    {
        if (QStandardPaths::findExecutable("ffmpeg").isEmpty())
            QSKIP("ffmpeg is needed for frame previews");
        startClips();
        QTRY_VERIFY(p->seekBar->isEnabled());
        QPointF at(p->seekBar->width() / 4.0, p->seekBar->height() / 2.0);
        QMouseEvent move(QEvent::MouseMove, at, p->seekBar->mapToGlobal(at.toPoint()), Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(p->seekBar, &move);
        QTRY_VERIFY_WITH_TIMEOUT(p->seekPreview && p->seekPreview->isVisible(), 8000);
        QCOMPARE(p->seekPreview->pixmap().width(), 160); // 320x240 source, scaled to 160 wide
        QEvent leave(QEvent::Leave);
        QApplication::sendEvent(p->seekBar, &leave);
        QVERIFY(!p->seekPreview->isVisible());
    }

    void coverArtForAudio()
    {
        if (QStandardPaths::findExecutable("ffmpeg").isEmpty())
            QSKIP("ffmpeg is needed to make a file with cover art");
        // Own folder: mpv also shows a cover.jpg beside an audio file, which other tests would see.
        QDir().mkpath(tmp.filePath("art"));
        const QString jpg = tmp.filePath("art/cover.jpg");
        const QString mp3 = tmp.filePath("art/with_cover.mp3");
        QProcess ff;
        ff.start("ffmpeg", {"-v", "error", "-y", "-f", "lavfi", "-i", "color=c=red:s=64x64", "-frames:v", "1", jpg});
        QVERIFY(ff.waitForFinished(60000));
        QCOMPARE(ff.exitCode(), 0);
        ff.start("ffmpeg", {"-v", "error", "-y", "-f", "lavfi", "-i", "sine=duration=6:sample_rate=44100", "-i", jpg,
                            "-map", "0:a", "-map", "1:v", "-c:a", "libmp3lame", "-c:v", "mjpeg",
                            "-disposition:v", "attached_pic", mp3});
        QVERIFY(ff.waitForFinished(60000));
        QCOMPARE(ff.exitCode(), 0);
        p->openFiles({mp3});
        QTRY_COMPARE_WITH_TIMEOUT(status(), QString("Playing"), 5000);
        // The embedded picture is shown at its own size, not a black window.
        QTRY_COMPARE_WITH_TIMEOUT(p->prop("video-params/w").toInt(), 64, 5000);
        // The visualiser stays off when there is a picture to show.
        trigger("&Visualizer (audio files)");
        QTest::qWait(1000);
        QCOMPARE(p->prop("video-params/w").toInt(), 64);
    }
};

int main(int argc, char *argv[])
{
    // Tests never need a real sound card, and a missing or busy audio server hangs mpv.
    if (qEnvironmentVariableIsEmpty("REELPLAY_AO"))
        qputenv("REELPLAY_AO", "null");
    QApplication app(argc, argv);
    app.setApplicationName("reelplay-test");
    app.setOrganizationName("reelplay-test");
    PlayerTest t;
    return QTest::qExec(&t, argc, argv);
}

#include "test_player.moc"
