// app/aboutdialog.h
//
// Help -> About.
//
// This is a licensing artefact as much as a courtesy. anyeditqt is GPLv3 and
// links Qt under the LGPLv3, and both have things they want said where a user
// can find them: GPLv3 section 5 wants an appropriate legal notice carrying the
// warranty disclaimer, and the LGPL wants the use of the library acknowledged
// with its licence available. A README satisfies neither for someone who was
// handed a binary.
//
// It also carries the version and the Qt versions, which is the first thing
// anyone is asked for in a bug report and the last thing they can find.
#pragma once

#include <QDialog>

class QLabel;

namespace anyedit {

class AboutDialog : public QDialog {
    Q_OBJECT

public:
    explicit AboutDialog(QWidget *parent = nullptr);

    // Version this build was compiled with, from ANYEDIT_VERSION, which
    // app/CMakeLists.txt takes from project(VERSION). One place.
    static QString version();
    // "Qt 6.10.3 (compiled against 6.10.3)", collapsing to one when they
    // agree. A package that deployed the wrong Qt shows two, which is worth
    // being able to ask for.
    static QString qtVersions();
    // Plain text, for a test to read and for anyone who wants to copy it.
    static QString summary();

    // Where the notices live: beside the executable in a package, at the top
    // of the tree in a development build. Empty if neither exists, and the
    // dialog then omits the link rather than offering a dead one.
    static QString noticesPath();

private:
    void openNotices();
};

}  // namespace anyedit
