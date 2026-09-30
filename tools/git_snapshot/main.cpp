#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTextStream>

namespace {

QString NormalizeExistingPath(const QString &path) {
    const QFileInfo info(path);
    if (info.exists()) {
        return QDir::toNativeSeparators(info.canonicalFilePath());
    }
    return QDir::toNativeSeparators(QDir(path).absolutePath());
}

void WriteLine(QTextStream *stream, const QString &line = QString()) {
    (*stream) << line << Qt::endl;
    stream->flush();
}

int RunGitStep(
    QTextStream *stream,
    const QString &repoRoot,
    const QString &label,
    const QStringList &gitArgs) {
    QStringList args;
    args << "-C" << repoRoot;
    args << gitArgs;

    WriteLine(stream, "[git_step]");
    WriteLine(stream, "timestamp=" + QDateTime::currentDateTime().toString(Qt::ISODate));
    WriteLine(stream, "label=" + label);
    WriteLine(stream, "program=git");
    WriteLine(stream, "arguments=" + args.join(' '));

    QProcess process;
    process.setProgram("git");
    process.setArguments(args);
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start();
    if (!process.waitForStarted(10000)) {
        WriteLine(stream, "started=false");
        WriteLine(stream, "error=failed to start git");
        WriteLine(stream);
        return 127;
    }

    process.waitForFinished(-1);
    const QString stdoutText = QString::fromLocal8Bit(process.readAllStandardOutput());
    const QString stderrText = QString::fromLocal8Bit(process.readAllStandardError());
    WriteLine(stream, "started=true");
    WriteLine(stream, "exit_code=" + QString::number(process.exitCode()));
    WriteLine(stream, "stdout_begin");
    if (!stdoutText.isEmpty()) {
        (*stream) << stdoutText;
        if (!stdoutText.endsWith('\n')) {
            (*stream) << Qt::endl;
        }
    }
    WriteLine(stream, "stdout_end");
    WriteLine(stream, "stderr_begin");
    if (!stderrText.isEmpty()) {
        (*stream) << stderrText;
        if (!stderrText.endsWith('\n')) {
            (*stream) << Qt::endl;
        }
    }
    WriteLine(stream, "stderr_end");
    WriteLine(stream);
    return process.exitCode();
}

int RunSnapshot(const QString &repoRoot, QTextStream *stream) {
    WriteLine(stream, "[snapshot_start]");
    WriteLine(stream, "timestamp=" + QDateTime::currentDateTime().toString(Qt::ISODate));
    WriteLine(stream, "repo_root=" + repoRoot);
    WriteLine(stream);

    const QList<QPair<QString, QStringList>> steps = {
        {"rev_parse", {"rev-parse", "--show-toplevel"}},
        {"status_short", {"status", "--short"}},
        {"diff_stat", {"diff", "--no-ext-diff", "--stat"}},
        {"diff_full", {"diff", "--no-ext-diff", "--"}}
    };

    for (const auto &step : steps) {
        const int exitCode = RunGitStep(stream, repoRoot, step.first, step.second);
        if (exitCode != 0) {
            WriteLine(stream, "[snapshot_complete]");
            WriteLine(stream, "status=failed");
            WriteLine(stream, "failed_step=" + step.first);
            WriteLine(stream, "exit_code=" + QString::number(exitCode));
            return exitCode;
        }
    }

    WriteLine(stream, "[snapshot_complete]");
    WriteLine(stream, "status=success");
    WriteLine(stream, "exit_code=0");
    return 0;
}

}  // namespace

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("git_snapshot");

    QCommandLineParser parser;
    parser.setApplicationDescription("Write a git status/diff snapshot with explicit per-step evidence.");
    parser.addHelpOption();
    const QCommandLineOption repoRootOption("repo-root", "Git repository root.", "path");
    const QCommandLineOption outOption("out", "Optional output log file. Defaults to stdout.", "path");
    parser.addOption(repoRootOption);
    parser.addOption(outOption);
    parser.process(app);

    const QString repoRootRaw = parser.value(repoRootOption).trimmed();
    if (repoRootRaw.isEmpty()) {
        QTextStream err(stderr);
        WriteLine(&err, "error=missing --repo-root");
        return 2;
    }

    const QString repoRoot = NormalizeExistingPath(repoRootRaw);
    const QString outPath = parser.value(outOption).trimmed();
    if (!outPath.isEmpty()) {
        QFile outFile(outPath);
        QDir().mkpath(QFileInfo(outFile).absolutePath());
        if (!outFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            QTextStream err(stderr);
            WriteLine(&err, "error=failed to open output log");
            WriteLine(&err, "out=" + outPath);
            return 3;
        }
        QTextStream stream(&outFile);
        return RunSnapshot(repoRoot, &stream);
    }

    QTextStream stream(stdout);
    return RunSnapshot(repoRoot, &stream);
}
