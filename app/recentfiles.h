// app/recentfiles.h
//
// The last 20 files opened, in ~/.anyeditqt/recent.json.
//
// NOT IN settings.json, deliberately. That file is preferences -- things you
// chose -- and this is state, which changes every time a file is opened. Mixing
// them would rewrite settings.json on every open, which fires the settings
// watcher, which reloads the preferences dialog and every tab, and churns a file
// you may well have open in a tab yourself. Two files, two lifetimes.
//
// A corrupt or unreadable recent.json is not an error and is not reported: the
// list is disposable, and refusing to start the editor over it would be absurd.
// settings.json is the opposite and says so loudly. That asymmetry is the point.
#pragma once

#include <QObject>
#include <QStringList>

namespace anyedit {

class RecentFiles : public QObject {
    Q_OBJECT

public:
    // The list holds this many. Twenty is enough to cover a day's work and
    // short enough that the menu is still a menu rather than a directory
    // listing.
    static constexpr int kMax = 20;

    static QString path();  // configDir()/recent.json

    explicit RecentFiles(QObject *parent = nullptr);

    bool load();
    bool save() const;

    // Most recent first. Absolute paths.
    const QStringList &paths() const { return paths_; }
    bool isEmpty() const { return paths_.isEmpty(); }

    // Moves an existing entry to the front rather than duplicating it, and
    // drops the oldest once the list is full. Saves.
    void add(const QString &path);
    void remove(const QString &path);
    void clear();

Q_SIGNALS:
    void changed();

private:
    QStringList paths_;
};

}  // namespace anyedit
