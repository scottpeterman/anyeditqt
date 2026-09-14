// tools/render_probe.cpp
//
// The smallest thing that proves the two halves meet: load a grammar, tokenize
// real source with aced::core, turn the token types into QTextCharFormat runs,
// lay them out with QTextLayout, and paint to a QImage with no display.
//
// It exists to answer questions that reading cannot. Does QTextLayout actually
// wrap the way the design assumes? Do the token spans -- which are byte offsets
// from PCRE2 -- line up with QString's UTF-16 indices? What does a real file
// look like before any of the widget is written?
//
//   QT_QPA_PLATFORM=offscreen ./render_probe [grammars.json] <file> <out.png>
//
// With no grammar path it resolves one the way a packaged application has to:
// Resources/grammars.json beside the executable, then ../Resources alongside
// it, then the source tree. That is deliberately the same lookup the bundle
// scripts verify -- a packaged tree that cannot find its own corpus starts,
// draws nothing, and gives no clue why, which is the failure the theme path
// already caused once in Omega.
//
// Prints one summary line per run and writes a PNG. Mismatched byte/UTF-16
// offsets show up as a non-zero "clamped" count, not as silently wrong colour.

#include <QApplication>
#include <QColor>
#include <QFont>
#include <QFontMetricsF>
#include <QImage>
#include <QPainter>
#include <QTextLayout>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "aced/document.h"
#include "aced/grammar.h"
#include "aced/tokenizer.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

namespace {

// Candidates in the order a packaged application should try them. The first
// two are the shipped layouts (Windows/macOS put Resources beside the binary,
// the Linux tree puts bin/ and Resources/ as siblings); the last is a
// developer running out of the build directory.
QString findGrammars() {
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        appDir + "/Resources/grammars.json",
        appDir + "/../Resources/grammars.json",
        appDir + "/../Resources/anyeditqt/grammars.json",
        appDir + "/../../grammars/grammars.json",
        appDir + "/../../../grammars/grammars.json",
    };
    for (const QString &c : candidates)
        if (QFileInfo::exists(c)) return QDir::cleanPath(c);
    return {};
}

// A deliberately small scope table. The point is the longest-prefix fallback:
// "string.quoted.double" misses, "string.quoted" misses, "string" hits. That is
// what lets a handful of entries colour all 198 grammars.
QColor colorForScope(const QString &type) {
    static const std::vector<std::pair<QString, QColor>> kScopes = {
        {"comment",         QColor(0x96, 0x98, 0x96)},
        {"string",          QColor(0xb5, 0xbd, 0x68)},
        {"constant.numeric", QColor(0xde, 0x93, 0x5f)},
        {"constant",        QColor(0xde, 0x93, 0x5f)},
        {"keyword.operator", QColor(0x8a, 0xbe, 0xb7)},
        {"keyword",         QColor(0xb2, 0x94, 0xbb)},
        {"storage",         QColor(0xb2, 0x94, 0xbb)},
        {"entity.name.function", QColor(0x81, 0xa2, 0xbe)},
        {"entity",          QColor(0x81, 0xa2, 0xbe)},
        {"variable.parameter", QColor(0xde, 0x93, 0x5f)},
        {"variable",        QColor(0xcc, 0x66, 0x66)},
        {"support",         QColor(0x81, 0xa2, 0xbe)},
        {"paren",           QColor(0xc5, 0xc8, 0xc6)},
        {"punctuation",     QColor(0xc5, 0xc8, 0xc6)},
        {"meta.tag",        QColor(0xcc, 0x66, 0x66)},
    };
    QString scope = type;
    while (!scope.isEmpty()) {
        for (const auto &[k, c] : kScopes)
            if (scope == k) return c;
        int dot = scope.lastIndexOf('.');
        if (dot < 0) break;
        scope.truncate(dot);
    }
    return QColor(0xc5, 0xc8, 0xc6);
}

}  // namespace

int main(int argc, char **argv) {
    QApplication app(argc, argv);

    std::string grammarPath, sourcePath, outPath;
    if (argc == 4) {
        grammarPath = argv[1];
        sourcePath = argv[2];
        outPath = argv[3];
    } else if (argc == 3) {
        grammarPath = findGrammars().toStdString();
        sourcePath = argv[1];
        outPath = argv[2];
        if (grammarPath.empty()) {
            // Said loudly and by name. A packaged tree reaching this line is
            // the whole reason the bundle scripts run this binary at all.
            std::fprintf(stderr,
                         "no grammars found: looked for Resources/grammars.json "
                         "beside %s and one level up\n",
                         QCoreApplication::applicationDirPath().toUtf8().constData());
            return 1;
        }
        std::fprintf(stderr, "grammars: %s\n", grammarPath.c_str());
    } else {
        std::fprintf(stderr,
                     "usage: render_probe [grammars.json] <source-file> <out.png>\n");
        return 2;
    }

    aced::Grammar grammar;
    if (!grammar.loadFile(grammarPath)) {
        std::fprintf(stderr, "grammar: %s\n", grammar.error().c_str());
        return 1;
    }

    std::ifstream in(sourcePath, std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "cannot open %s\n", sourcePath.c_str());
        return 1;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    aced::Document doc(ss.str());

    const std::string mode = aced::Grammar::modeForFilename(sourcePath);
    const aced::Tokenizer *tk = grammar.tokenizer(mode);
    if (!tk) {
        std::fprintf(stderr, "no grammar for mode '%s'\n", mode.c_str());
        return 1;
    }

    QFont font("monospace");
    font.setStyleHint(QFont::Monospace);
    font.setPixelSize(14);
    const QFontMetricsF fm(font);
    const qreal lineHeight = fm.height();
    const int width = 900;
    const int rows = std::min(doc.lineCount(), 60);

    QImage image(width, static_cast<int>(lineHeight * rows) + 8, QImage::Format_RGB32);
    image.fill(QColor(0x1d, 0x1f, 0x21));
    QPainter painter(&image);
    painter.setFont(font);

    std::string state = "start";
    std::vector<std::string> stack;
    int tokenCount = 0, clamped = 0;
    qreal y = 4;

    for (int row = 0; row < rows; ++row) {
        const std::string &line = doc.line(row);
        auto result = tk->tokenize(line, state, stack);
        state = result.state;
        stack = result.stack;

        const QString text = QString::fromUtf8(line.c_str(),
                                               static_cast<int>(line.size()));
        QList<QTextLayout::FormatRange> formats;

        // Token values are byte slices of the UTF-8 line; QString indices are
        // UTF-16. Convert by re-measuring each prefix rather than assuming they
        // agree, and count any run that had to be clamped.
        int byteOffset = 0;
        for (const auto &t : result.tokens) {
            const int startUtf16 =
                QString::fromUtf8(line.c_str(), byteOffset).size();
            byteOffset += static_cast<int>(t.value.size());
            const int endUtf16 =
                QString::fromUtf8(line.c_str(),
                                  std::min<int>(byteOffset,
                                                static_cast<int>(line.size()))).size();
            if (byteOffset > static_cast<int>(line.size())) ++clamped;

            QTextLayout::FormatRange fr;
            fr.start = startUtf16;
            fr.length = endUtf16 - startUtf16;
            fr.format.setForeground(
                colorForScope(QString::fromStdString(t.type)));
            if (fr.length > 0) formats.append(fr);
            ++tokenCount;
        }

        QTextLayout layout(text, font);
        layout.setFormats(formats);
        layout.beginLayout();
        QTextLine tl = layout.createLine();
        if (tl.isValid()) {
            tl.setLineWidth(width - 8);
            tl.setPosition(QPointF(4, 0));
        }
        layout.endLayout();
        layout.draw(&painter, QPointF(0, y));
        y += lineHeight;
    }
    painter.end();

    if (!image.save(QString::fromStdString(outPath))) {
        std::fprintf(stderr, "could not write %s\n", outPath.c_str());
        return 1;
    }
    std::printf("mode=%s rows=%d tokens=%d clamped=%d -> %s\n",
                mode.c_str(), rows, tokenCount, clamped, outPath.c_str());
    return clamped == 0 ? 0 : 1;
}
