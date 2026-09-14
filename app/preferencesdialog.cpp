// app/preferencesdialog.cpp
#include "app/preferencesdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

namespace anyedit {
namespace {

// Sentinel for "no family stored". An empty QVariant would be indistinguishable
// from a combo that has not been populated yet.
const char kDefaultFamily[] = "";

QString comboFamily(QComboBox *c) {
    return c->currentData().toString();
}

void selectFamily(QComboBox *c, const QString &family) {
    const int i = c->findData(family);
    c->setCurrentIndex(i >= 0 ? i : 0);
}

}  // namespace

PreferencesDialog::PreferencesDialog(Settings *settings, QWidget *parent)
    : QDialog(parent), settings_(settings) {
    setWindowTitle("Preferences");
    setModal(false);  // so the preview is visible against the window behind it

    savedEditor_ = settings_->editor();
    savedApp_ = settings_->app();

    // SET FOR THE WHOLE OF CONSTRUCTION. Populating a QComboBox emits
    // currentIndexChanged, and the pages connect that to pushToSettings before
    // fillFontCombos() runs -- so without this the dialog writes every spin
    // box's minimum into the settings the instant it is created, and the
    // "saved" copies above are the only surviving record of what the user
    // actually had. It cost a font size of 6 and a tab width of 1.
    loading_ = true;

    auto *tabs = new QTabWidget(this);
    tabs->addTab(buildEditorPage(), "Editor");
    tabs->addTab(buildAppPage(), "Application");

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::RestoreDefaults,
        this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::RestoreDefaults),
            &QPushButton::clicked, this, &PreferencesDialog::restoreDefaults);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(tabs);
    layout->addWidget(buttons);

    // OK writes the file. Everything up to that point has been applied in
    // memory only, so a crash mid-preview leaves the file as it was.
    connect(this, &QDialog::accepted, this, [this] {
        QString err;
        settings_->save(&err);
    });

    // The file is the other editor of the same object. When it changes -- the
    // settings.json tab was saved, or something else wrote it -- the controls
    // follow rather than sitting there showing stale values that the next
    // keystroke would write back.
    connect(settings_, &Settings::changed, this, &PreferencesDialog::loadFromSettings);

    fillFontCombos();
    loading_ = false;
    loadFromSettings();
}

PreferencesDialog::~PreferencesDialog() = default;

QWidget *PreferencesDialog::buildEditorPage() {
    auto *page = new QWidget(this);
    auto *form = new QFormLayout(page);

    editorFont_ = new QComboBox(page);
    form->addRow("Font", editorFont_);

    editorFontSize_ = new QSpinBox(page);
    editorFontSize_->setRange(6, 72);
    editorFontSize_->setSuffix(" pt");
    form->addRow("Font size", editorFontSize_);

    tabWidth_ = new QSpinBox(page);
    tabWidth_->setRange(1, 16);
    form->addRow("Tab width", tabWidth_);

    insertSpaces_ = new QCheckBox("Insert spaces instead of tabs", page);
    form->addRow(QString(), insertSpaces_);

    showLineNumbers_ = new QCheckBox("Show line numbers", page);
    form->addRow(QString(), showLineNumbers_);

    highlightCurrentLine_ = new QCheckBox("Highlight current line", page);
    form->addRow(QString(), highlightCurrentLine_);

    softWrap_ = new QCheckBox("Word wrap", page);
    form->addRow(QString(), softWrap_);

    wrapColumn_ = new QSpinBox(page);
    wrapColumn_->setRange(0, 1000);
    wrapColumn_->setSpecialValueText("window width");
    form->addRow("Wrap at column", wrapColumn_);

    paletteDark_ = new QComboBox(page);
    paletteLight_ = new QComboBox(page);
    for (const QString &n : paletteNames()) {
        paletteDark_->addItem(n, n);
        paletteLight_->addItem(n, n);
    }
    form->addRow("Colours when dark", paletteDark_);
    form->addRow("Colours when light", paletteLight_);

    for (QCheckBox *c : {insertSpaces_, showLineNumbers_, highlightCurrentLine_, softWrap_})
        connect(c, &QCheckBox::toggled, this, &PreferencesDialog::pushToSettings);
    for (QSpinBox *s : {editorFontSize_, tabWidth_, wrapColumn_})
        connect(s, QOverload<int>::of(&QSpinBox::valueChanged), this,
                &PreferencesDialog::pushToSettings);
    for (QComboBox *c : {editorFont_, paletteDark_, paletteLight_})
        connect(c, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                &PreferencesDialog::pushToSettings);

    // The column only means something while wrapping is on, and a spin box you
    // can change to no effect is worse than one you cannot change.
    connect(softWrap_, &QCheckBox::toggled, wrapColumn_, &QWidget::setEnabled);

    return page;
}

QWidget *PreferencesDialog::buildAppPage() {
    auto *page = new QWidget(this);
    auto *form = new QFormLayout(page);

    theme_ = new QComboBox(page);
    theme_->addItem("Follow the system", static_cast<int>(ThemeMode::System));
    theme_->addItem("Light", static_cast<int>(ThemeMode::Light));
    theme_->addItem("Dark", static_cast<int>(ThemeMode::Dark));
    form->addRow("Theme", theme_);

    uiFont_ = new QComboBox(page);
    form->addRow("Interface font", uiFont_);

    uiFontSize_ = new QSpinBox(page);
    uiFontSize_->setRange(0, 72);
    uiFontSize_->setSpecialValueText("system default");
    uiFontSize_->setSuffix(" pt");
    form->addRow("Interface font size", uiFontSize_);

    rememberGeometry_ = new QCheckBox("Remember window size on exit", page);
    form->addRow(QString(), rememberGeometry_);

    auto *where = new QLabel(page);
    where->setText("Settings are stored in " + Settings::configPath());
    where->setWordWrap(true);
    where->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow(QString(), where);

    connect(rememberGeometry_, &QCheckBox::toggled, this,
            &PreferencesDialog::pushToSettings);
    connect(uiFontSize_, QOverload<int>::of(&QSpinBox::valueChanged), this,
            &PreferencesDialog::pushToSettings);
    for (QComboBox *c : {theme_, uiFont_})
        connect(c, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                &PreferencesDialog::pushToSettings);

    return page;
}

void PreferencesDialog::fillFontCombos() {
    // Monospace families only in the editor combo. Offering the other four
    // hundred is offering to break the gutter alignment, since LineCache sizes
    // it from a measured character advance.
    editorFont_->addItem("default (" + Settings::defaultMonospaceFamily() + ")",
                         QString(kDefaultFamily));
    for (const QString &f : QFontDatabase::families()) {
        if (QFontDatabase::isFixedPitch(f)) editorFont_->addItem(f, f);
    }

    uiFont_->addItem("system default", QString(kDefaultFamily));
    for (const QString &f : QFontDatabase::families()) uiFont_->addItem(f, f);
}

void PreferencesDialog::loadFromSettings() {
    loading_ = true;
    const EditorSettings &e = settings_->editor();
    selectFamily(editorFont_, e.fontFamily);
    editorFontSize_->setValue(e.fontSize);
    tabWidth_->setValue(e.tabWidth);
    insertSpaces_->setChecked(e.insertSpaces);
    showLineNumbers_->setChecked(e.showLineNumbers);
    highlightCurrentLine_->setChecked(e.highlightCurrentLine);
    softWrap_->setChecked(e.softWrap);
    wrapColumn_->setValue(e.wrapColumn);
    wrapColumn_->setEnabled(e.softWrap);
    paletteDark_->setCurrentIndex(std::max(0, paletteDark_->findData(e.paletteDark)));
    paletteLight_->setCurrentIndex(std::max(0, paletteLight_->findData(e.paletteLight)));

    const AppSettings &a = settings_->app();
    theme_->setCurrentIndex(std::max(0, theme_->findData(static_cast<int>(a.theme))));
    selectFamily(uiFont_, a.uiFontFamily);
    uiFontSize_->setValue(a.uiFontSize);
    rememberGeometry_->setChecked(a.window.rememberGeometry);
    loading_ = false;
}

void PreferencesDialog::pushToSettings() {
    if (loading_) return;

    EditorSettings e = settings_->editor();
    e.fontFamily = comboFamily(editorFont_);
    e.fontSize = editorFontSize_->value();
    e.tabWidth = tabWidth_->value();
    e.insertSpaces = insertSpaces_->isChecked();
    e.showLineNumbers = showLineNumbers_->isChecked();
    e.highlightCurrentLine = highlightCurrentLine_->isChecked();
    e.softWrap = softWrap_->isChecked();
    e.wrapColumn = wrapColumn_->value();
    e.paletteDark = paletteDark_->currentData().toString();
    e.paletteLight = paletteLight_->currentData().toString();

    AppSettings a = settings_->app();
    a.theme = static_cast<ThemeMode>(theme_->currentData().toInt());
    a.uiFontFamily = comboFamily(uiFont_);
    a.uiFontSize = uiFontSize_->value();
    a.window.rememberGeometry = rememberGeometry_->isChecked();

    // Two calls, and each is a no-op when nothing in its group moved, so
    // changing a font size does not make the window re-read its geometry.
    settings_->setEditor(e);
    settings_->setApp(a);
}

void PreferencesDialog::restoreDefaults() {
    // The window size is NOT reset. It is a default in the struct, but it is
    // also whatever the user last dragged the window to, and having "restore
    // defaults" resize the window out from under them is a surprise.
    EditorSettings e;
    AppSettings a;
    a.window = settings_->app().window;
    settings_->setEditor(e);
    settings_->setApp(a);
    loadFromSettings();
}

void PreferencesDialog::reject() {
    settings_->setEditor(savedEditor_);
    settings_->setApp(savedApp_);
    QDialog::reject();
}

}  // namespace anyedit
