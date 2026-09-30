#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileInfoList>
#include <QTextCodec>
#include <QTextStream>

namespace {

QString NormalizePath(const QString &path) {
    const QFileInfo info(path);
    if (info.exists()) {
        const QString canonical = info.canonicalFilePath();
        if (!canonical.isEmpty()) {
            return QDir::toNativeSeparators(canonical);
        }
    }
    return QDir::toNativeSeparators(QDir(path).absolutePath());
}

void WriteLine(QTextStream *stream, const QString &line = QString()) {
    (*stream) << line << Qt::endl;
    stream->flush();
}

int RunListDirectory(
    QTextStream *stream,
    const QString &directoryPath,
    int maxEntries) {
    const QFileInfo directoryInfo(directoryPath);
    if (!directoryInfo.exists()) {
        WriteLine(stream, "status=failed");
        WriteLine(stream, "error=directory does not exist");
        return 33;
    }
    if (!directoryInfo.isDir()) {
        WriteLine(stream, "status=failed");
        WriteLine(stream, "error=path is not a directory");
        return 34;
    }

    QDir dir(directoryPath);
    QFileInfoList allEntries = dir.entryInfoList(
        QDir::NoDotAndDotDot | QDir::AllEntries,
        QDir::Name | QDir::IgnoreCase);
    QStringList entryLabels;
    QStringList filePaths;
    QStringList fileNames;

    WriteLine(stream, "status=success");
    WriteLine(stream, "normalized_path=" + NormalizePath(directoryPath));
    WriteLine(stream, "total_entries=" + QString::number(allEntries.size()));

    int returnedCount = 0;
    int fileCount = 0;
    int directoryCount = 0;
    for (int i = 0; i < allEntries.size(); ++i) {
        const QFileInfo &info = allEntries.at(i);
        const bool isDir = info.isDir();
        const QString label = (isDir ? "[dir] " : "[file] ") + info.fileName();
        entryLabels.push_back(label);
        if (!isDir) {
            filePaths.push_back(NormalizePath(info.filePath()));
            fileNames.push_back(info.fileName());
            ++fileCount;
        } else {
            ++directoryCount;
        }
        if (returnedCount < maxEntries) {
            WriteLine(stream, "entry_" + QString::number(returnedCount) + "=" + label);
            ++returnedCount;
        }
    }

    WriteLine(stream, "returned_count=" + QString::number(returnedCount));
    WriteLine(stream, "entry_count=" + QString::number(returnedCount));
    WriteLine(stream, "file_count=" + QString::number(fileCount));
    WriteLine(stream, "directory_count=" + QString::number(directoryCount));
    WriteLine(stream, "remaining_entries=" + QString::number(qMax(0, allEntries.size() - returnedCount)));

    for (int i = 0; i < filePaths.size(); ++i) {
        WriteLine(stream, "file_path_" + QString::number(i) + "=" + filePaths.at(i));
        WriteLine(stream, "file_name_" + QString::number(i) + "=" + fileNames.at(i));
    }
    return 0;
}

int RunReadFilePage(
    QTextStream *stream,
    const QString &filePath,
    int startLine,
    int maxLines) {
    QFile file(filePath);
    if (!file.exists()) {
        WriteLine(stream, "status=failed");
        WriteLine(stream, "error=file does not exist");
        return 22;
    }
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        WriteLine(stream, "status=failed");
        WriteLine(stream, "error=failed to open file");
        return 23;
    }

    QTextStream input(&file);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    input.setEncoding(QStringConverter::Utf8);
#else
    input.setCodec("UTF-8");
#endif

    const int boundedStartLine = qMax(1, startLine);
    const int boundedMaxLines = qMax(1, maxLines);

    QStringList pageLines;
    int currentLine = 0;
    int lastLineRead = 0;
    while (!input.atEnd()) {
        const QString line = input.readLine();
        ++currentLine;
        if (currentLine < boundedStartLine) {
            continue;
        }
        if (pageLines.size() >= boundedMaxLines) {
            continue;
        }
        pageLines.push_back(line);
        lastLineRead = currentLine;
    }

    const bool hasMore = !pageLines.isEmpty() && lastLineRead < currentLine;
    WriteLine(stream, "status=success");
    WriteLine(stream, "normalized_path=" + NormalizePath(filePath));
    WriteLine(stream, "start_line=" + QString::number(boundedStartLine));
    WriteLine(stream, "end_line=" + (pageLines.isEmpty() ? QString() : QString::number(lastLineRead)));
    WriteLine(stream, "line_count=" + QString::number(pageLines.size()));
    WriteLine(stream, "total_lines=" + QString::number(currentLine));
    WriteLine(stream, "has_more=" + QString(hasMore ? "true" : "false"));
    WriteLine(stream, "next_start_line=" + (hasMore ? QString::number(boundedStartLine + pageLines.size()) : QString()));
    WriteLine(stream, "content_begin<<<");
    for (const QString &line : pageLines) {
        (*stream) << line << Qt::endl;
    }
    WriteLine(stream, ">>>content_end");
    return 0;
}

}  // namespace

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("directory_access");

    QCommandLineParser parser;
    parser.setApplicationDescription("Qt helper for codex-lan-agent directory listing and file paging.");
    parser.addHelpOption();

    const QCommandLineOption modeOption("mode", "Mode: list-directory or read-file-page.", "mode");
    const QCommandLineOption directoryOption("directory", "Directory path.", "path");
    const QCommandLineOption fileOption("file", "File path.", "path");
    const QCommandLineOption startLineOption("start-line", "1-based start line for file paging.", "line", "1");
    const QCommandLineOption maxLinesOption("max-lines", "Max lines for file paging.", "count", "500");
    const QCommandLineOption maxEntriesOption("max-entries", "Max entries to emit directly.", "count", "200");
    const QCommandLineOption extensionsOption("extensions", "Reserved for future extension filtering.", "csv");

    parser.addOption(modeOption);
    parser.addOption(directoryOption);
    parser.addOption(fileOption);
    parser.addOption(startLineOption);
    parser.addOption(maxLinesOption);
    parser.addOption(maxEntriesOption);
    parser.addOption(extensionsOption);
    parser.process(app);

    QTextStream out(stdout);
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    out.setEncoding(QStringConverter::Utf8);
#else
    out.setCodec("UTF-8");
#endif
    WriteLine(&out, "timestamp=" + QDateTime::currentDateTime().toString(Qt::ISODate));

    const QString mode = parser.value(modeOption).trimmed();
    if (mode == "list-directory") {
        const QString directoryPath = parser.value(directoryOption).trimmed();
        if (directoryPath.isEmpty()) {
            WriteLine(&out, "status=failed");
            WriteLine(&out, "error=missing --directory");
            return 30;
        }
        return RunListDirectory(&out, directoryPath, parser.value(maxEntriesOption).toInt());
    }
    if (mode == "read-file-page") {
        const QString filePath = parser.value(fileOption).trimmed();
        if (filePath.isEmpty()) {
            WriteLine(&out, "status=failed");
            WriteLine(&out, "error=missing --file");
            return 20;
        }
        return RunReadFilePage(
            &out,
            filePath,
            parser.value(startLineOption).toInt(),
            parser.value(maxLinesOption).toInt());
    }

    WriteLine(&out, "status=failed");
    WriteLine(&out, "error=unsupported --mode");
    return 2;
}
