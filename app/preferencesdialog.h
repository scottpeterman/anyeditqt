// app/preferencesdialog.h
//
// A dialog over the same anyedit::Settings object the JSON file writes to.
// Neither is the source of truth; Settings is, and both are just editors of it.
// That is why changing a control here shows up immediately in a settings.json
// tab, and why saving that tab moves the controls here.
//
// LIVE PREVIEW WITH A REAL CANCEL. Every control applies as it changes, because
// picking a font size without seeing it is guesswork. The settings as they were
// when the dialog opened are kept, and Cancel puts them back -- so the preview
// costs nothing if you do not want it. Closing the window is a Cancel, the same
// as pressing it.
#pragma once

#include <QDialog>

#include "app/settings.h"

class QCheckBox;
class QComboBox;
class QSpinBox;

namespace anyedit {

class PreferencesDialog : public QDialog {
    Q_OBJECT

public:
    // The settings object is not owned and must outlive the dialog.
    explicit PreferencesDialog(Settings *settings, QWidget *parent = nullptr);
    ~PreferencesDialog() override;

    // What Cancel would restore. Public so a test can check it rather than
    // inferring it from behaviour.
    const EditorSettings &openedWithEditor() const { return savedEditor_; }
    const AppSettings &openedWithApp() const { return savedApp_; }

public Q_SLOTS:
    void restoreDefaults();
    // QDialog::reject() is a public slot and stays one. Closing the window
    // routes through it, which is why Cancel and the window's close button do
    // the same thing without a second code path.
    void reject() override;

private:
    QWidget *buildEditorPage();
    QWidget *buildAppPage();
    // Controls -> Settings. Does nothing while loadFromSettings() is running.
    void pushToSettings();
    // Settings -> controls. Blocks signals, so it cannot feed back.
    void loadFromSettings();
    // Monospace families, with a first entry meaning "whatever this platform's
    // default is". Stored as an empty string so the file stays portable.
    void fillFontCombos();

    Settings *settings_;
    EditorSettings savedEditor_;
    AppSettings savedApp_;
    bool loading_ = false;

    QComboBox *editorFont_ = nullptr;
    QSpinBox *editorFontSize_ = nullptr;
    QSpinBox *tabWidth_ = nullptr;
    QCheckBox *insertSpaces_ = nullptr;
    QCheckBox *showLineNumbers_ = nullptr;
    QCheckBox *highlightCurrentLine_ = nullptr;
    QCheckBox *softWrap_ = nullptr;
    QSpinBox *wrapColumn_ = nullptr;
    QComboBox *paletteDark_ = nullptr;
    QComboBox *paletteLight_ = nullptr;

    QComboBox *theme_ = nullptr;
    QComboBox *uiFont_ = nullptr;
    QSpinBox *uiFontSize_ = nullptr;
    QCheckBox *rememberGeometry_ = nullptr;
};

}  // namespace anyedit
