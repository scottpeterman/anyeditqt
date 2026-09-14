// app/aboutdialog.cpp
#include "app/aboutdialog.h"

#include <QApplication>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QLabel>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

#ifndef ANYEDIT_VERSION
#define ANYEDIT_VERSION "0.0.0-dev"
#endif

namespace anyedit {

namespace {
const char *kRepo = "https://github.com/scottpeterman/anyeditqt";
}

QString AboutDialog::version() { return QStringLiteral(ANYEDIT_VERSION); }

QString AboutDialog::qtVersions() {
    const QString runtime = QString::fromLatin1(qVersion());
    const QString compiled = QStringLiteral(QT_VERSION_STR);
    if (runtime == compiled) return QStringLiteral("Qt %1").arg(runtime);
    return QStringLiteral("Qt %1 (compiled against %2)").arg(runtime, compiled);
}

QString AboutDialog::noticesPath() {
    // Beside the executable in a package; at the top of the tree in a
    // development build, where the binary is three directories down.
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        appDir + "/Resources/THIRD_PARTY_NOTICES.md",
        appDir + "/THIRD_PARTY_NOTICES.md",
        appDir + "/../Resources/THIRD_PARTY_NOTICES.md",
        appDir + "/../../THIRD_PARTY_NOTICES.md",
        appDir + "/../../../THIRD_PARTY_NOTICES.md",
    };
    for (const QString &c : candidates) {
        const QFileInfo fi(c);
        if (fi.isFile()) return fi.canonicalFilePath();
    }
    return QString();
}

QString AboutDialog::summary() {
    // The warranty disclaimer is GPLv3 section 5c's "appropriate legal notice",
    // in the words the licence itself suggests. The Qt sentence is the LGPL
    // acknowledgement. Neither is decoration; both are why this dialog exists.
    return QStringLiteral(
               "AnyEdit %1\n"
               "%2\n"
               "%3\n"
               "\n"
               "A code editor built on ace's grammar corpus.\n"
               "\n"
               "Copyright (C) 2026 Scott Peterman\n"
               "\n"
               "This program is free software: you can redistribute it and/or modify "
               "it under the terms of the GNU General Public License as published by "
               "the Free Software Foundation, either version 3 of the License, or (at "
               "your option) any later version.\n"
               "\n"
               "This program is distributed in the hope that it will be useful, but "
               "WITHOUT ANY WARRANTY; without even the implied warranty of "
               "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU "
               "General Public License for more details.\n"
               "\n"
               "This program uses the Qt framework, which is used under the terms of "
               "the GNU Lesser General Public License v3.0 and is dynamically linked "
               "and unmodified. Syntax highlighting and code folding derive from ace "
               "(BSD-3-Clause, Ajax.org B.V.); the line map follows Scintilla's "
               "design (Neil Hodgson). Full notices and licence texts accompany this "
               "program.\n")
        .arg(version(), qtVersions(), QString::fromLatin1(kRepo));
}

AboutDialog::AboutDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle(tr("About AnyEdit"));
    setModal(true);

    auto *layout = new QVBoxLayout(this);

    auto *icon = new QLabel(this);
    const QIcon appIcon = QApplication::windowIcon();
    if (!appIcon.isNull()) icon->setPixmap(appIcon.pixmap(64, 64));
    icon->setAlignment(Qt::AlignHCenter);
    layout->addWidget(icon);

    auto *title = new QLabel(
        QStringLiteral("<h2 style='margin-bottom:0'>AnyEdit %1</h2>").arg(version()),
        this);
    title->setAlignment(Qt::AlignHCenter);
    layout->addWidget(title);

    auto *sub = new QLabel(
        QStringLiteral("<p align='center'>%1<br/><a href='%2'>%2</a></p>")
            .arg(qtVersions(), QString::fromLatin1(kRepo)),
        this);
    sub->setOpenExternalLinks(true);
    sub->setAlignment(Qt::AlignHCenter);
    layout->addWidget(sub);

    auto *body = new QLabel(this);
    body->setTextFormat(Qt::RichText);
    body->setWordWrap(true);
    body->setOpenExternalLinks(true);
    body->setText(QStringLiteral(
        "<p>A code editor built on ace's grammar corpus: a Qt-free C++ core, "
        "a Qt widget over it.</p>"
        "<p>Copyright &copy; 2026 Scott Peterman</p>"
        "<p>This program is free software: you can redistribute it and/or "
        "modify it under the terms of the "
        "<b>GNU General Public License</b> as published by the Free Software "
        "Foundation, either version 3 of the License, or (at your option) any "
        "later version.</p>"
        "<p>This program is distributed in the hope that it will be useful, but "
        "<b>WITHOUT ANY WARRANTY</b>; without even the implied warranty of "
        "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU "
        "General Public License for more details.</p>"
        "<p>This program uses the <b>Qt</b> framework under the terms of the GNU "
        "Lesser General Public License v3.0. Qt is dynamically linked and "
        "unmodified. Syntax highlighting and code folding derive from "
        "<b>ace</b> (BSD-3-Clause, Ajax.org B.V.); the line map follows "
        "<b>Scintilla</b>'s design (Neil Hodgson).</p>"));
    body->setMinimumWidth(460);
    layout->addWidget(body, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);

    // Qt's own dialog, not a hand-written summary: it carries The Qt Company's
    // required notice in the words they maintain, and it stays right across Qt
    // versions without anyone here remembering to update it.
    QPushButton *aboutQt = buttons->addButton(tr("About Qt"), QDialogButtonBox::ActionRole);
    connect(aboutQt, &QPushButton::clicked, qApp, &QApplication::aboutQt);

    if (!noticesPath().isEmpty()) {
        QPushButton *notices =
            buttons->addButton(tr("Third-party Notices"), QDialogButtonBox::ActionRole);
        connect(notices, &QPushButton::clicked, this, &AboutDialog::openNotices);
    }

    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

void AboutDialog::openNotices() {
    const QString p = noticesPath();
    if (p.isEmpty()) return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(p));
}

}  // namespace anyedit
