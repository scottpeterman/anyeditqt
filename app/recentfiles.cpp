// app/recentfiles.cpp
#include "app/recentfiles.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include "app/settings.h"

namespace anyedit {
namespace {

// Canonical where the file exists, absolute where it does not -- a path to a
// file that has since been deleted is still worth remembering and still worth
// de-duplicating. canonicalFilePath() returns empty for a missing file, which
// is why it cannot be used alone.
QString normalise(const QString &path) {
    if (path.isEmpty()) return {};
    const QFileInfo fi(path);
    const QString canon = fi.canonicalFilePath();
    return canon.isEmpty() ? QDir::cleanPath(fi.absoluteFilePath()) : canon;
}

}  // namespace

QString RecentFiles::path() { return Settings::configDir() + "/recent.json"; }

RecentFiles::RecentFiles(QObject *parent) : QObject(parent) {}

bool RecentFiles::load() {
    QFile f(path());
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (!doc.isObject()) return false;

    QStringList out;
    const QJsonArray arr = doc.object().value("files").toArray();
    for (const QJsonValue &v : arr) {
        if (!v.isString()) continue;
        const QString p = v.toString();
        if (p.isEmpty()) continue;
        // De-duplicated on the way IN as well as on add(). A file hand-edited,
        // or written by a build that normalised differently, should not produce
        // two menu entries for one file.
        if (!out.contains(p)) out.append(p);
        if (out.size() >= kMax) break;
    }
    if (out == paths_) return true;
    paths_ = out;
    Q_EMIT changed();
    return true;
}

bool RecentFiles::save() const {
    const QString dir = Settings::configDir();
    if (!QDir().mkpath(dir)) return false;

    QJsonArray arr;
    for (const QString &p : paths_) arr.append(p);
    QJsonObject root;
    root["version"] = 1;
    root["files"] = arr;

    // Same as settings.json: written to a temporary and renamed, so an
    // interrupted save leaves the previous list rather than half a file.
    QSaveFile out(path());
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (out.write(bytes) != bytes.size()) return false;
    return out.commit();
}

void RecentFiles::add(const QString &p) {
    const QString norm = normalise(p);
    if (norm.isEmpty()) return;
    // Moved to the front rather than appended: opening a file you already have
    // in the list should promote it, not leave it to age out while you keep
    // using it.
    paths_.removeAll(norm);
    paths_.prepend(norm);
    while (paths_.size() > kMax) paths_.removeLast();
    save();
    Q_EMIT changed();
}

void RecentFiles::remove(const QString &p) {
    const QString norm = normalise(p);
    // Both forms. A path that came from an older list may not normalise to
    // itself any more -- the file has since been deleted, or a symlink in it
    // has -- and removing only the normalised form would leave the entry that
    // is actually in the list.
    const int before = paths_.size();
    paths_.removeAll(norm);
    paths_.removeAll(p);
    if (paths_.size() == before) return;
    save();
    Q_EMIT changed();
}

void RecentFiles::clear() {
    if (paths_.isEmpty()) return;
    paths_.clear();
    save();
    Q_EMIT changed();
}

}  // namespace anyedit
