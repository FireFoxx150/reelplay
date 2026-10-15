// Options dialog (View > Options, "O") and applying saved options.
#include "player.h"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QStandardPaths>
#include <QStyleFactory>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

// Hotkey-able actions are the ones act() created; their id looks like "Menu/Item".
static QList<QAction *> hotkeyActions(QWidget *w)
{
    QList<QAction *> list;
    for (QAction *a : w->actions())
        if (a->objectName().contains('/'))
            list << a;
    return list;
}

static void applyTheme(bool dark)
{
    static const QPalette original = qApp->palette();
    if (!dark) {
        qApp->setPalette(original);
        return;
    }
    qApp->setStyle(QStyleFactory::create("Fusion"));
    QPalette p;
    p.setColor(QPalette::Window, QColor(32, 32, 32));
    p.setColor(QPalette::WindowText, QColor(220, 220, 220));
    p.setColor(QPalette::Base, QColor(24, 24, 24));
    p.setColor(QPalette::AlternateBase, QColor(40, 40, 40));
    p.setColor(QPalette::Text, QColor(220, 220, 220));
    p.setColor(QPalette::Button, QColor(45, 45, 45));
    p.setColor(QPalette::ButtonText, QColor(220, 220, 220));
    p.setColor(QPalette::ToolTipBase, QColor(45, 45, 45));
    p.setColor(QPalette::ToolTipText, QColor(220, 220, 220));
    p.setColor(QPalette::Highlight, QColor(42, 110, 187));
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::PlaceholderText, QColor(130, 130, 130));
    p.setColor(QPalette::Disabled, QPalette::Text, QColor(110, 110, 110));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(110, 110, 110));
    p.setColor(QPalette::Disabled, QPalette::WindowText, QColor(110, 110, 110));
    qApp->setPalette(p);
}

void Player::applySettings()
{
    QSettings s;
    mpv_set_property_string(mpv, "hwdec", s.value("hwdec", "no").toString().toUtf8().constData());
    mpv_set_property_string(mpv, "alang", s.value("alang").toString().toUtf8().constData());
    mpv_set_property_string(mpv, "slang", s.value("slang").toString().toUtf8().constData());
    applyTheme(s.value("darkTheme", true).toBool());
    for (QAction *a : hotkeyActions(this))
        a->setShortcut(s.value("keys/" + a->objectName(), a->property("defaultKey")).value<QKeySequence>());
}

void Player::openOptions()
{
    QSettings s;
    QDialog d(this);
    d.setWindowTitle("Options");
    d.resize(520, 480);

    auto *general = new QWidget;
    auto *form = new QFormLayout(general);
    auto *hwdec = new QComboBox;
    hwdec->setObjectName("hwdec");
    hwdec->addItems({"no", "auto-safe", "vaapi", "nvdec", "vdpau"});
    hwdec->setCurrentText(s.value("hwdec", "no").toString());
    auto *alang = new QLineEdit(s.value("alang").toString());
    alang->setObjectName("alang");
    alang->setPlaceholderText("e.g. eng,jpn");
    auto *slang = new QLineEdit(s.value("slang").toString());
    slang->setObjectName("slang");
    slang->setPlaceholderText("e.g. eng");
    auto *shotDir = new QLineEdit(s.value("screenshotDir").toString());
    shotDir->setObjectName("screenshotDir");
    shotDir->setPlaceholderText(QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) + "/newmpc-be");
    auto *dark = new QCheckBox("Dark theme");
    dark->setObjectName("darkTheme");
    dark->setChecked(s.value("darkTheme", true).toBool());
    form->addRow("Hardware decoding:", hwdec);
    form->addRow("Preferred audio language:", alang);
    form->addRow("Preferred subtitle language:", slang);
    form->addRow("Screenshot folder:", shotDir);
    form->addRow(dark);

    auto *keysPage = new QWidget;
    auto *keysLay = new QVBoxLayout(keysPage);
    const QList<QAction *> actions = hotkeyActions(this);
    auto *table = new QTableWidget(actions.size(), 2);
    table->setHorizontalHeaderLabels({"Command", "Key"});
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->verticalHeader()->hide();
    QList<QKeySequenceEdit *> edits;
    for (int r = 0; r < actions.size(); ++r) {
        auto *item = new QTableWidgetItem(actions[r]->objectName());
        item->setFlags(Qt::ItemIsEnabled);
        table->setItem(r, 0, item);
        auto *edit = new QKeySequenceEdit(actions[r]->shortcut());
        edit->setObjectName(actions[r]->objectName());
        table->setCellWidget(r, 1, edit);
        edits << edit;
    }
    auto *reset = new QPushButton("Reset All to Defaults");
    connect(reset, &QPushButton::clicked, &d, [&] {
        for (int r = 0; r < actions.size(); ++r)
            edits[r]->setKeySequence(actions[r]->property("defaultKey").value<QKeySequence>());
    });
    keysLay->addWidget(table);
    keysLay->addWidget(reset, 0, Qt::AlignRight);

    auto *tabs = new QTabWidget;
    tabs->addTab(general, "Player");
    tabs->addTab(keysPage, "Keys");
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    auto *lay = new QVBoxLayout(&d);
    lay->addWidget(tabs);
    lay->addWidget(buttons);

    if (d.exec() != QDialog::Accepted)
        return;
    s.setValue("hwdec", hwdec->currentText());
    s.setValue("alang", alang->text().trimmed());
    s.setValue("slang", slang->text().trimmed());
    if (shotDir->text().trimmed().isEmpty())
        s.remove("screenshotDir");
    else
        s.setValue("screenshotDir", shotDir->text().trimmed());
    s.setValue("darkTheme", dark->isChecked());
    for (int r = 0; r < actions.size(); ++r) {
        // QKeySequenceEdit can record chords; hotkeys use only the first key.
        QKeySequence seq = edits[r]->keySequence().isEmpty() ? QKeySequence() : QKeySequence(edits[r]->keySequence()[0]);
        QString key = "keys/" + actions[r]->objectName();
        if (seq == actions[r]->property("defaultKey").value<QKeySequence>())
            s.remove(key);
        else
            s.setValue(key, seq);
    }
    applySettings();
}
