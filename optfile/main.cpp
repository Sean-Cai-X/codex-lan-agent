#include <QCoreApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTextStream>

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

namespace {

bool read_file_range_mcp(
    const std::string& path,
    qint64 offset,
    qint64 length,
    QString* json_output,
    QString* error_message) {
    constexpr qint64 kMaxReadLength = 16 * 1024 * 1024;
    if (offset < 0) {
        if (error_message != nullptr) {
            *error_message = "read offset must be >= 0";
        }
        return false;
    }
    if (length <= 0 || length > kMaxReadLength) {
        if (error_message != nullptr) {
            *error_message = "read length must be between 1 and 16777216 bytes";
        }
        return false;
    }

    QFile file(QString::fromStdString(path));
    if (!file.open(QIODevice::ReadOnly)) {
        if (error_message != nullptr) {
            *error_message = "failed to open file for bounded reading";
        }
        return false;
    }

    const qint64 file_size = file.size();
    if (offset > file_size) {
        if (error_message != nullptr) {
            *error_message = "read offset is beyond end of file";
        }
        file.close();
        return false;
    }
    if (!file.seek(offset)) {
        if (error_message != nullptr) {
            *error_message = "failed to seek to read offset";
        }
        file.close();
        return false;
    }

    const QByteArray chunk = file.read(length);
    if (chunk.size() < 0) {
        if (error_message != nullptr) {
            *error_message = "failed to read bounded file chunk";
        }
        file.close();
        return false;
    }
    file.close();

    const qint64 next_offset = offset + chunk.size();
    const bool has_more = next_offset < file_size;
    QJsonObject root;
    root["status"] = "ok";
    root["operation"] = "read_file_range";
    root["start_offset"] = offset;
    root["requested_length"] = length;
    root["bytes_read"] = chunk.size();
    root["file_size"] = file_size;
    root["next_offset"] = next_offset;
    root["has_more"] = has_more;
    root["eof"] = !has_more;
    root["content_encoding"] = "utf-8";
    root["content_text"] = QString::fromUtf8(chunk.constData(), chunk.size());
    root["content_base64"] = QString::fromLatin1(chunk.toBase64());
    root["content_sha256"] = QString::fromLatin1(
        QCryptographicHash::hash(chunk, QCryptographicHash::Sha256).toHex());
    if (json_output != nullptr) {
        *json_output = QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
    }
    return true;
}

bool load_file_lines_internal(
    const std::string& path,
    std::vector<QString>* lines,
    QString* error_message) {
    if (lines == nullptr) {
        if (error_message != nullptr) {
            *error_message = "lines output is null";
        }
        return false;
    }

    QFile file(QString::fromStdString(path));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error_message != nullptr) {
            *error_message = "failed to open file for reading";
        }
        return false;
    }

    QTextStream input(&file);
    input.setCodec("UTF-8");
    lines->clear();
    while (!input.atEnd()) {
        lines->push_back(input.readLine());
    }
    file.close();
    return true;
}

bool save_file_lines_atomic(
    const std::string& path,
    const std::vector<QString>& lines,
    QString* error_message) {
    bool use_crlf = false;
    bool had_final_line_ending = false;
    QFile existing_file(QString::fromStdString(path));
    if (existing_file.open(QIODevice::ReadOnly)) {
        const QByteArray existing_content = existing_file.readAll();
        const int first_lf = existing_content.indexOf('\n');
        use_crlf = first_lf > 0 && existing_content.at(first_lf - 1) == '\r';
        had_final_line_ending =
            existing_content.endsWith('\n') || existing_content.endsWith('\r');
        existing_file.close();
    }

    QSaveFile file(QString::fromStdString(path));
    if (!file.open(QIODevice::WriteOnly)) {
        if (error_message != nullptr) {
            *error_message = "open QSaveFile failed";
        }
        return false;
    }

    QTextStream out(&file);
    out.setCodec("UTF-8");
    out.setGenerateByteOrderMark(false);
    const QString line_ending = use_crlf ? QStringLiteral("\r\n") : QStringLiteral("\n");
    for (std::size_t index = 0; index < lines.size(); ++index) {
        QString normalized_line = lines[index];
        while (normalized_line.endsWith('\r')) {
            normalized_line.chop(1);
        }
        out << normalized_line;
        if (index + 1 < lines.size() || had_final_line_ending) {
            out << line_ending;
        }
    }
    out.flush();
    if (file.error() != QFile::NoError) {
        if (error_message != nullptr) {
            *error_message = "write QSaveFile failed";
        }
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        if (error_message != nullptr) {
            *error_message = "QSaveFile commit failed";
        }
        return false;
    }
    return true;
}

QString decode_cli_text(QString text) {
    text.replace("\\r\\n", "\n");
    text.replace("\\n", "\n");
    text.replace("\\t", "\t");
    text.replace("\\r", "\r");
    return text;
}

QString resolve_replacement_text(
    const QCommandLineParser& parser,
    const QCommandLineOption& text_option,
    const QCommandLineOption& base64_option,
    bool decode_escapes) {
    QString text;
    if (parser.isSet(base64_option)) {
        text = QString::fromUtf8(QByteArray::fromBase64(parser.value(base64_option).toLatin1()));
    } else {
        text = parser.value(text_option);
    }
    return decode_escapes ? decode_cli_text(text) : text;
}

QString sha256_text(const QString& text) {
    const QByteArray hash = QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha256);
    return QString::fromLatin1(hash.toHex());
}

QString hash_line_range(const std::vector<QString>& lines, int start_line, int end_line) {
    if (start_line < 1 || end_line < start_line || end_line > static_cast<int>(lines.size())) {
        return QString();
    }
    QString joined;
    for (int line = start_line; line <= end_line; ++line) {
        if (line > start_line) {
            joined += "\n";
        }
        joined += lines[static_cast<std::size_t>(line - 1)];
    }
    return sha256_text(joined);
}

std::vector<std::string> split_string(const std::string& s, char delimiter) {
    std::string token;
    std::stringstream token_stream(s);
    std::vector<std::string> tokens;
    while (std::getline(token_stream, token, delimiter)) {
        tokens.push_back(token);
    }
    return tokens;
}

std::vector<std::string> split_string_vec(const std::string& s, char delimiter) {
    std::vector<std::string> tokens;
    std::string token;
    std::stringstream token_stream(s);
    while (std::getline(token_stream, token, delimiter)) {
        tokens.push_back(token);
    }
    return tokens;
}

std::string join_lines_with_newlines(const std::vector<std::string>& lines) {
    std::ostringstream output;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        if (index > 0) {
            output << '\n';
        }
        output << lines[index];
    }
    return output.str();
}

bool is_ignorable_unified_diff_line(const std::string& line) {
    return line.rfind("diff --git ", 0) == 0
        || line.rfind("index ", 0) == 0
        || line.rfind("--- ", 0) == 0
        || line.rfind("+++ ", 0) == 0
        || line.rfind("new file mode ", 0) == 0
        || line.rfind("deleted file mode ", 0) == 0;
}

bool build_content_from_simple_unified_diff(
    const std::string& original_text,
    const std::string& diff_text,
    std::string* new_content,
    std::string* error_message) {
    if (new_content == nullptr) {
        if (error_message != nullptr) {
            *error_message = "new_content output is null";
        }
        return false;
    }

    const std::vector<std::string> original_lines = split_string_vec(original_text, '\n');
    const std::vector<std::string> diff_lines = split_string_vec(diff_text, '\n');
    std::vector<std::string> result_lines;
    std::size_t original_index = 0;
    bool in_hunk = false;

    for (const std::string& raw_line : diff_lines) {
        if (raw_line.empty()) {
            if (in_hunk) {
                result_lines.push_back(std::string());
            }
            continue;
        }
        if (is_ignorable_unified_diff_line(raw_line)) {
            continue;
        }
        if (raw_line.rfind("@@", 0) == 0) {
            in_hunk = true;
            continue;
        }
        if (!in_hunk) {
            continue;
        }

        const char prefix = raw_line[0];
        const std::string payload = raw_line.substr(1);
        if (prefix == ' ') {
            if (original_index >= original_lines.size() || original_lines[original_index] != payload) {
                if (error_message != nullptr) {
                    *error_message = "context line does not match target file";
                }
                return false;
            }
            result_lines.push_back(payload);
            ++original_index;
        } else if (prefix == '-') {
            if (original_index >= original_lines.size() || original_lines[original_index] != payload) {
                if (error_message != nullptr) {
                    *error_message = "deleted line does not match target file";
                }
                return false;
            }
            ++original_index;
        } else if (prefix == '+') {
            result_lines.push_back(payload);
        }
    }

    while (original_index < original_lines.size()) {
        result_lines.push_back(original_lines[original_index++]);
    }

    *new_content = join_lines_with_newlines(result_lines);
    return true;
}

std::string read_test_file(const std::string& path) {
    QFile file(QString::fromStdString(path));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return std::string();
    }
    QTextStream in(&file);
    in.setCodec("UTF-8");
    const QString content = in.readAll();
    file.close();
    const QByteArray utf8 = content.toUtf8();
    return std::string(utf8.constData(), static_cast<std::size_t>(utf8.size()));
}

bool write_test_file(const std::string& path, const std::string& content) {
    QFile file(QString::fromStdString(path));
    QDir().mkpath(QFileInfo(file).absolutePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        return false;
    }
    QTextStream out(&file);
    out.setCodec("UTF-8");
    out.setGenerateByteOrderMark(false);
    out << QString::fromUtf8(content.data(), static_cast<int>(content.size()));
    out.flush();
    const bool ok = file.error() == QFile::NoError;
    file.close();
    return ok;
}
bool apply_diff_write_file(const std::string& path, const std::string& diff_text) {
    const std::string original = read_test_file(path);
    std::string new_content;
    std::string error_message;
    if (!build_content_from_simple_unified_diff(original, diff_text, &new_content, &error_message)) {
        qCritical() << "[ERROR]" << QString::fromStdString(error_message);
        return false;
    }
    return write_test_file(path, new_content);
}

bool insert_content_at_line(const std::string& path, const std::string& content, int line_number) {
    std::vector<QString> lines;
    QString error_message;
    if (!load_file_lines_internal(path, &lines, &error_message)) {
        return false;
    }
    if (line_number < 1 || line_number > static_cast<int>(lines.size()) + 1) {
        return false;
    }
    const QStringList new_lines = QString::fromStdString(content).split('\n');
    lines.insert(lines.begin() + line_number - 1, new_lines.begin(), new_lines.end());
    return save_file_lines_atomic(path, lines, &error_message);
}

bool delete_content_at_line(const std::string& path, int line_number) {
    std::vector<QString> lines;
    QString error_message;
    if (!load_file_lines_internal(path, &lines, &error_message)) {
        return false;
    }
    if (line_number < 1 || line_number > static_cast<int>(lines.size())) {
        return false;
    }
    lines.erase(lines.begin() + line_number - 1);
    return save_file_lines_atomic(path, lines, &error_message);
}

int levenshtein_distance(const QString& left, const QString& right) {
    const int rows = left.length() + 1;
    const int cols = right.length() + 1;
    std::vector<int> previous(static_cast<std::size_t>(cols));
    std::vector<int> current(static_cast<std::size_t>(cols));

    for (int col = 0; col < cols; ++col) {
        previous[static_cast<std::size_t>(col)] = col;
    }

    for (int row = 1; row < rows; ++row) {
        current[0] = row;
        for (int col = 1; col < cols; ++col) {
            const int substitution_cost = left[row - 1] == right[col - 1] ? 0 : 1;
            current[static_cast<std::size_t>(col)] = std::min({
                previous[static_cast<std::size_t>(col)] + 1,
                current[static_cast<std::size_t>(col - 1)] + 1,
                previous[static_cast<std::size_t>(col - 1)] + substitution_cost});
        }
        previous.swap(current);
    }

    return previous[static_cast<std::size_t>(cols - 1)];
}

double normalized_similarity(const QString& left, const QString& right) {
    const int max_len = std::max(left.length(), right.length());
    if (max_len == 0) {
        return 100.0;
    }
    const int distance = levenshtein_distance(left, right);
    return std::max(0.0, (1.0 - static_cast<double>(distance) / static_cast<double>(max_len)) * 100.0);
}

double calculate_similarity(const QString& line, const QString& anchor) {
    const QString normalized_line = line.toLower();
    const QString normalized_anchor = anchor.trimmed().toLower();
    if (normalized_anchor.isEmpty()) {
        return normalized_line.isEmpty() ? 100.0 : 0.0;
    }
    if (normalized_line.contains(normalized_anchor)) {
        return 100.0;
    }

    double best_score = normalized_similarity(normalized_line, normalized_anchor);
    QString token;
    for (const QChar& ch : normalized_line) {
        if (ch.isLetterOrNumber() || ch == '_' || ch == '-') {
            token.append(ch);
            continue;
        }
        if (!token.isEmpty()) {
            best_score = std::max(best_score, normalized_similarity(token, normalized_anchor));
            token.clear();
        }
    }
    if (!token.isEmpty()) {
        best_score = std::max(best_score, normalized_similarity(token, normalized_anchor));
    }

    return best_score;
}
bool fuzzy_match_lines(
    const std::vector<QString>& lines,
    const QString& anchor_text,
    int fuzzy_threshold,
    QJsonArray* out_matches,
    int max_results = 32) {
    if (out_matches == nullptr) {
        return false;
    }
    *out_matches = QJsonArray();

    std::vector<std::tuple<int, double, QString>> candidates;
    for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
        const double score = calculate_similarity(lines[static_cast<std::size_t>(i)], anchor_text);
        if (score >= static_cast<double>(fuzzy_threshold)) {
            candidates.emplace_back(i + 1, score, lines[static_cast<std::size_t>(i)]);
        }
    }

    std::sort(candidates.begin(), candidates.end(), [](const auto& left, const auto& right) {
        return std::get<1>(left) > std::get<1>(right);
    });

    int count = 0;
    for (const auto& [line_no, score, line] : candidates) {
        if (count++ >= max_results) {
            break;
        }
        QJsonObject item;
        item["line"] = line_no;
        item["similarity"] = std::round(score * 10.0) / 10.0;
        item["line_hash"] = sha256_text(line);
        item["line_length"] = line.length();
        item["preview"] = line.left(160);
        out_matches->append(item);
    }

    return !out_matches->isEmpty();
}

bool find_line_metadata_mcp(
    const std::string& path,
    int line_number,
    bool show_preview,
    QString* json_output,
    QString* error_message) {
    std::vector<QString> lines;
    if (!load_file_lines_internal(path, &lines, error_message)) {
        return false;
    }
    if (line_number < 1 || line_number > static_cast<int>(lines.size())) {
        if (error_message != nullptr) {
            *error_message = "line_number out of range";
        }
        return false;
    }

    const QString& line = lines[static_cast<std::size_t>(line_number - 1)];
    QJsonObject root;
    root["operation"] = "find_line_metadata";
    root["line"] = line_number;
    root["line_hash"] = sha256_text(line);
    root["line_length"] = line.length();
    if (show_preview) {
        root["preview"] = line.left(160);
    }
    root["status"] = "ok";
    *json_output = QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
    return true;
}

bool locate_text_lines_mcp(
    const std::string& path,
    const QString& anchor_text,
    bool show_preview,
    int fuzzy_threshold,
    QString* json_output,
    QString* error_message) {
    std::vector<QString> lines;
    if (!load_file_lines_internal(path, &lines, error_message)) {
        return false;
    }

    QJsonObject root;
    root["operation"] = "locate_text";
    root["anchor"] = anchor_text;
    root["fuzzy_threshold"] = fuzzy_threshold;

    QJsonArray exact_matches;
    for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
        const QString& line = lines[static_cast<std::size_t>(i)];
        if (line.contains(anchor_text)) {
            QJsonObject item;
            item["line"] = i + 1;
            item["line_hash"] = sha256_text(line);
            item["line_length"] = line.length();
            if (show_preview) {
                item["preview"] = line.left(160);
            }
            exact_matches.append(item);
        }
    }

    if (!exact_matches.isEmpty()) {
        root["match_type"] = "exact";
        root["match_count"] = exact_matches.size();
        root["matches"] = exact_matches;
    } else {
        QJsonArray fuzzy_matches;
        if (fuzzy_match_lines(lines, anchor_text, fuzzy_threshold, &fuzzy_matches)) {
            root["match_type"] = "fuzzy";
            root["match_count"] = fuzzy_matches.size();
            root["matches"] = fuzzy_matches;
            root["message"] = "exact not found, returned similar lines";
        } else {
            root["match_type"] = "none";
            root["match_count"] = 0;
            root["matches"] = QJsonArray();
            root["message"] = "no similar lines found";
        }
    }

    *json_output = QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
    return true;
}

bool insert_after_anchor_atomic_mcp(
    const std::string& path,
    const QString& anchor_text,
    int occurrence,
    const QString& insert_text,
    const QString& expected_anchor_line_hash,
    QString* json_output,
    QString* error_message) {
    if (occurrence < 1) {
        if (error_message != nullptr) {
            *error_message = "occurrence must be >= 1";
        }
        return false;
    }

    std::vector<QString> lines;
    if (!load_file_lines_internal(path, &lines, error_message)) {
        return false;
    }

    int target = -1;
    int seen = 0;
    for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
        if (lines[static_cast<std::size_t>(i)].contains(anchor_text) && ++seen == occurrence) {
            target = i;
            break;
        }
    }
    if (target < 0) {
        if (error_message != nullptr) {
            *error_message = "anchor not found";
        }
        return false;
    }

    const QString current_hash = sha256_text(lines[static_cast<std::size_t>(target)]);
    if (!expected_anchor_line_hash.isEmpty() && current_hash != expected_anchor_line_hash) {
        if (error_message != nullptr) {
            *error_message = "anchor hash mismatch";
        }
        return false;
    }

    const QStringList split = insert_text.split('\n');
    std::vector<QString> new_lines(split.begin(), split.end());
    lines.insert(lines.begin() + target + 1, new_lines.begin(), new_lines.end());
    if (!save_file_lines_atomic(path, lines, error_message)) {
        return false;
    }

    QJsonObject root;
    root["operation"] = "insert_after_anchor_atomic";
    root["anchor"] = anchor_text;
    root["occurrence"] = occurrence;
    root["anchor_line"] = target + 1;
    root["insert_start_line"] = target + 2;
    root["insert_line_count"] = static_cast<int>(new_lines.size());
    root["anchor_line_hash_before"] = current_hash;
    root["status"] = "ok";
    *json_output = QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
    return true;
}

bool replace_line_range_atomic_mcp(
    const std::string& path,
    int start_line,
    int end_line,
    const QString& replacement_text,
    const QString& expected_range_hash,
    QString* json_output,
    QString* error_message) {
    std::vector<QString> lines;
    if (!load_file_lines_internal(path, &lines, error_message)) {
        return false;
    }
    if (start_line < 1 || end_line < start_line || end_line > static_cast<int>(lines.size())) {
        if (error_message != nullptr) {
            *error_message = "invalid replace line range";
        }
        return false;
    }

    const QString current_hash = hash_line_range(lines, start_line, end_line);
    if (!expected_range_hash.isEmpty() && current_hash != expected_range_hash) {
        if (error_message != nullptr) {
            *error_message = "range hash mismatch";
        }
        return false;
    }

    lines.erase(lines.begin() + start_line - 1, lines.begin() + end_line);
    std::vector<QString> replacement_lines;
    if (!replacement_text.isEmpty()) {
        const QStringList split = replacement_text.split('\n');
        replacement_lines.assign(split.begin(), split.end());
        lines.insert(lines.begin() + start_line - 1, replacement_lines.begin(), replacement_lines.end());
    }

    if (!save_file_lines_atomic(path, lines, error_message)) {
        return false;
    }

    QJsonObject root;
    root["operation"] = "replace_line_range_atomic";
    root["start_line"] = start_line;
    root["end_line"] = end_line;
    root["replacement_line_count"] = static_cast<int>(replacement_lines.size());
    root["range_hash_before"] = current_hash;
    root["status"] = "ok";
    *json_output = QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
    return true;
}

bool delete_line_atomic_mcp(
    const std::string& path,
    int line_number,
    const QString& expected_line_hash,
    QString* json_output,
    QString* error_message) {
    std::vector<QString> lines;
    if (!load_file_lines_internal(path, &lines, error_message)) {
        return false;
    }
    if (line_number < 1 || line_number > static_cast<int>(lines.size())) {
        if (error_message != nullptr) {
            *error_message = "line_number out of range";
        }
        return false;
    }

    const QString current_hash = sha256_text(lines[static_cast<std::size_t>(line_number - 1)]);
    if (!expected_line_hash.isEmpty() && current_hash != expected_line_hash) {
        if (error_message != nullptr) {
            *error_message = "line hash mismatch";
        }
        return false;
    }

    lines.erase(lines.begin() + line_number - 1);
    if (!save_file_lines_atomic(path, lines, error_message)) {
        return false;
    }

    QJsonObject root;
    root["operation"] = "delete_line_atomic";
    root["line"] = line_number;
    root["deleted_line_hash_before"] = current_hash;
    root["status"] = "ok";
    *json_output = QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
    return true;
}

bool delete_content_atomic_mcp(
    const std::string& path,
    const QString& anchor_text,
    int occurrence,
    const QString& expected_anchor_line_hash,
    QString* json_output,
    QString* error_message) {
    if (occurrence < 1) {
        if (error_message != nullptr) {
            *error_message = "occurrence must be >= 1";
        }
        return false;
    }

    std::vector<QString> lines;
    if (!load_file_lines_internal(path, &lines, error_message)) {
        return false;
    }

    int target = -1;
    int seen = 0;
    for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
        if (lines[static_cast<std::size_t>(i)].contains(anchor_text) && ++seen == occurrence) {
            target = i;
            break;
        }
    }
    if (target < 0) {
        if (error_message != nullptr) {
            *error_message = "anchor not found";
        }
        return false;
    }

    const QString current_hash = sha256_text(lines[static_cast<std::size_t>(target)]);
    if (!expected_anchor_line_hash.isEmpty() && current_hash != expected_anchor_line_hash) {
        if (error_message != nullptr) {
            *error_message = "anchor hash mismatch";
        }
        return false;
    }

    lines.erase(lines.begin() + target);
    if (!save_file_lines_atomic(path, lines, error_message)) {
        return false;
    }

    QJsonObject root;
    root["operation"] = "delete_content_atomic";
    root["anchor"] = anchor_text;
    root["occurrence"] = occurrence;
    root["deleted_line"] = target + 1;
    root["deleted_line_hash_before"] = current_hash;
    root["status"] = "ok";
    *json_output = QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
    return true;
}

bool write_file_atomic_mcp(
    const std::string& path,
    const QByteArray& payload,
    bool append,
    const QString& expected_file_hash,
    QString* json,
    QString* error_message) {
    QFile existing(QString::fromStdString(path));
    QByteArray old_content;
    const bool file_existed = existing.exists();
    if (file_existed) {
        if (!existing.open(QIODevice::ReadOnly)) {
            if (error_message != nullptr) {
                *error_message = "failed to open existing file for reading";
            }
            return false;
        }
        old_content = existing.readAll();
        existing.close();
    }

    const QString old_hash = QString::fromLatin1(
        QCryptographicHash::hash(old_content, QCryptographicHash::Sha256).toHex());
    if (!expected_file_hash.isEmpty()
        && expected_file_hash.compare(old_hash, Qt::CaseInsensitive) != 0) {
        if (error_message != nullptr) {
            *error_message = "expected file hash mismatch";
        }
        return false;
    }

    const QByteArray final_content = append ? (old_content + payload) : payload;
    QFileInfo target_info(QString::fromStdString(path));
    if (!QDir().mkpath(target_info.absolutePath())) {
        if (error_message != nullptr) {
            *error_message = "failed to create parent directory";
        }
        return false;
    }

    QSaveFile output(target_info.absoluteFilePath());
    if (!output.open(QIODevice::WriteOnly)) {
        if (error_message != nullptr) {
            *error_message = "failed to open QSaveFile for writing";
        }
        return false;
    }
    if (output.write(final_content) != final_content.size()) {
        if (error_message != nullptr) {
            *error_message = "failed to write complete payload";
        }
        output.cancelWriting();
        return false;
    }
    if (!output.commit()) {
        if (error_message != nullptr) {
            *error_message = "QSaveFile commit failed";
        }
        return false;
    }

    QFile verify(target_info.absoluteFilePath());
    if (!verify.open(QIODevice::ReadOnly)) {
        if (error_message != nullptr) {
            *error_message = "failed to reopen file for verification";
        }
        return false;
    }
    const QByteArray verified_content = verify.readAll();
    verify.close();
    if (verified_content != final_content) {
        if (error_message != nullptr) {
            *error_message = "write verification content mismatch";
        }
        return false;
    }

    const QString new_hash = QString::fromLatin1(
        QCryptographicHash::hash(verified_content, QCryptographicHash::Sha256).toHex());
    QJsonObject root;
    root["status"] = "ok";
    root["operation"] = append ? "append_file_atomic" : "write_file_atomic";
    root["file_existed"] = file_existed;
    root["append"] = append;
    root["old_bytes"] = static_cast<qint64>(old_content.size());
    root["payload_bytes"] = static_cast<qint64>(payload.size());
    root["new_bytes"] = static_cast<qint64>(verified_content.size());
    root["old_hash"] = old_hash;
    root["new_hash"] = new_hash;
    root["write_verified"] = true;
    if (json != nullptr) {
        *json = QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
    }
    return true;
}

int write_explicit_result(const QString& json, const QString& err) {
    QTextStream out(stdout);
    out.setCodec("UTF-8");
    out.setGenerateByteOrderMark(false);
    if (!err.isEmpty()) {
        QJsonObject root;
        root["status"] = "error";
        root["error"] = err;
        out << QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact)) << '\n';
        out.flush();
        return 2;
    }

    out << json << '\n';
    out.flush();
    return 0;
}

}  // namespace

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription("Advanced File Operation Tool (Qt)");

    QCommandLineOption targetDirOption(QStringList{"td", "target-dir"}, "target dir", "path");
    QCommandLineOption testFileNameOption(QStringList{"tf", "test-file"}, "file name", "name");
    QCommandLineOption contentToWriteOption(QStringList{"cw", "content-write"}, "content to write (overwrite)", "text");
    QCommandLineOption contentToWriteBase64Option("content-write-base64", "base64 content to write", "base64");
    QCommandLineOption appendFileOption("append-file", "append content instead of overwrite");
    QCommandLineOption expectedFileHashOption("expected-file-hash", "expected SHA256 of existing file bytes", "hash");
    QCommandLineOption contentFileOption("content-file", "read raw content bytes from payload file", "path");
    QCommandLineOption contentToInsertOption(QStringList{"ci", "content-insert"}, "content to insert", "text");
    QCommandLineOption lineInsertOption(QStringList{"li", "line-insert"}, "line number to insert", "n");
    QCommandLineOption lineDeleteOption(QStringList{"ld", "line-delete"}, "line number to delete", "n");
    QCommandLineOption diffTextOption(QStringList{"dt", "diff-text"}, "unified diff text", "text");

    QCommandLineOption locateTextOption("locate-text", "find lines (exact/fuzzy)", "text");
    QCommandLineOption findLineOption("find-line", "find one line by line number", "n");
    QCommandLineOption deleteLineOption("delete-line", "delete one line atomically by line number", "n");
    QCommandLineOption deleteContentOption("delete-content", "delete one matched content line atomically", "text");
    QCommandLineOption showPreviewOption("show-preview", "show preview in locate result");
    QCommandLineOption fuzzyThresholdOption("fuzzy-threshold", "fuzzy threshold (0-100, default 60)", "n", "60");
    QCommandLineOption insertAfterAnchorOption("insert-after-anchor", "insert after anchor", "text");
    QCommandLineOption occurrenceOption("occurrence", "Nth occurrence of anchor (default 1)", "n", "1");
    QCommandLineOption expectedAnchorHashOption("expected-anchor-hash", "anchor line hash", "hash");
    QCommandLineOption expectedLineHashOption("expected-line-hash", "target line hash", "hash");
    QCommandLineOption replaceStartLineOption("replace-start-line", "replace start line", "n");
    QCommandLineOption replaceEndLineOption("replace-end-line", "replace end line", "n");
    QCommandLineOption replacementTextOption("replacement-text", "text for insert/replace", "text");
    QCommandLineOption replacementTextBase64Option("replacement-text-base64", "base64 text for insert/replace", "base64");
    QCommandLineOption decodeEscapesOption("decode-escapes", "decode \\n, \\r, and \\t sequences in replacement-text");
    QCommandLineOption expectedRangeHashOption("expected-range-hash", "range hash", "hash");
    QCommandLineOption readOffsetOption(
        QStringList{"read-offset", "start-offset"},
        "byte offset for one bounded read (default 0)",
        "bytes");
    QCommandLineOption readLengthOption(
        QStringList{"read-length", "chunk-size"},
        "maximum bytes returned by one bounded read (default 65536, max 16777216)",
        "bytes");

    parser.addOption(targetDirOption);
    parser.addOption(testFileNameOption);
    parser.addOption(contentToWriteOption);
    parser.addOption(contentToWriteBase64Option);
    parser.addOption(contentFileOption);
    parser.addOption(appendFileOption);
    parser.addOption(expectedFileHashOption);
    parser.addOption(contentToInsertOption);
    parser.addOption(lineInsertOption);
    parser.addOption(lineDeleteOption);
    parser.addOption(diffTextOption);
    parser.addOption(locateTextOption);
    parser.addOption(findLineOption);
    parser.addOption(deleteLineOption);
    parser.addOption(deleteContentOption);
    parser.addOption(showPreviewOption);
    parser.addOption(fuzzyThresholdOption);
    parser.addOption(insertAfterAnchorOption);
    parser.addOption(occurrenceOption);
    parser.addOption(expectedAnchorHashOption);
    parser.addOption(expectedLineHashOption);
    parser.addOption(replaceStartLineOption);
    parser.addOption(replaceEndLineOption);
    parser.addOption(replacementTextOption);
    parser.addOption(replacementTextBase64Option);
    parser.addOption(decodeEscapesOption);
    parser.addOption(expectedRangeHashOption);
    parser.addOption(readOffsetOption);
    parser.addOption(readLengthOption);
    parser.process(app);

    const int occurrence = parser.isSet(occurrenceOption)
        ? std::max(1, parser.value(occurrenceOption).toInt())
        : 1;

    const std::string path = parser.value(targetDirOption).toStdString() + "/" + parser.value(testFileNameOption).toStdString();
    QString json;
    QString err;
    if (parser.isSet(readOffsetOption) || parser.isSet(readLengthOption)) {
        const qint64 offset = parser.isSet(readOffsetOption)
            ? parser.value(readOffsetOption).toLongLong()
            : 0;
        const qint64 length = parser.isSet(readLengthOption)
            ? parser.value(readLengthOption).toLongLong()
            : 65536;
        read_file_range_mcp(path, offset, length, &json, &err);
        return write_explicit_result(json, err);
    }
    const int whole_file_source_count =
        (parser.isSet(contentToWriteOption) ? 1 : 0)
        + (parser.isSet(contentToWriteBase64Option) ? 1 : 0)
        + (parser.isSet(contentFileOption) ? 1 : 0);
    if (whole_file_source_count > 0) {
        if (whole_file_source_count != 1) {
            return write_explicit_result(QString(), "exactly one whole-file content source is required");
        }

        QByteArray payload;
        if (parser.isSet(contentFileOption)) {
            QFile payload_file(parser.value(contentFileOption));
            if (!payload_file.open(QIODevice::ReadOnly)) {
                return write_explicit_result(QString(), "failed to read content-file");
            }
            payload = payload_file.readAll();
        } else if (parser.isSet(contentToWriteBase64Option)) {
            const QByteArray encoded = parser.value(contentToWriteBase64Option).toLatin1();
            payload = QByteArray::fromBase64(encoded);
            if (!encoded.isEmpty() && payload.isEmpty()) {
                return write_explicit_result(QString(), "invalid content-write-base64");
            }
        } else {
            payload = parser.value(contentToWriteOption).toUtf8();
        }

        write_file_atomic_mcp(
            path,
            payload,
            parser.isSet(appendFileOption),
            parser.value(expectedFileHashOption),
            &json,
            &err);
        return write_explicit_result(json, err);
    }

    if (parser.isSet(findLineOption)) {
        find_line_metadata_mcp(path, parser.value(findLineOption).toInt(), parser.isSet(showPreviewOption), &json, &err);
        return write_explicit_result(json, err);
    }

    if (parser.isSet(locateTextOption)) {
        int threshold = parser.value(fuzzyThresholdOption).toInt();
        if (threshold <= 0) {
            threshold = 60;
        }
        locate_text_lines_mcp(path, parser.value(locateTextOption), parser.isSet(showPreviewOption), threshold, &json, &err);
        return write_explicit_result(json, err);
    }

    if (parser.isSet(deleteLineOption)) {
        delete_line_atomic_mcp(path, parser.value(deleteLineOption).toInt(), parser.value(expectedLineHashOption), &json, &err);
        return write_explicit_result(json, err);
    }

    if (parser.isSet(deleteContentOption)) {
        delete_content_atomic_mcp(path, parser.value(deleteContentOption), occurrence, parser.value(expectedAnchorHashOption), &json, &err);
        return write_explicit_result(json, err);
    }

    const bool decode_escapes = parser.isSet(decodeEscapesOption);

    if (parser.isSet(insertAfterAnchorOption)) {
        const QString replacement_text = resolve_replacement_text(
            parser,
            replacementTextOption,
            replacementTextBase64Option,
            decode_escapes);
        insert_after_anchor_atomic_mcp(path, parser.value(insertAfterAnchorOption), occurrence, replacement_text, parser.value(expectedAnchorHashOption), &json, &err);
        return write_explicit_result(json, err);
    }

    if (parser.isSet(replaceStartLineOption) || parser.isSet(replaceEndLineOption)) {
        const QString replacement_text = resolve_replacement_text(
            parser,
            replacementTextOption,
            replacementTextBase64Option,
            decode_escapes);
        replace_line_range_atomic_mcp(path, parser.value(replaceStartLineOption).toInt(), parser.value(replaceEndLineOption).toInt(), replacement_text, parser.value(expectedRangeHashOption), &json, &err);
        return write_explicit_result(json, err);
    }

    const std::string content_to_write = parser.value(contentToWriteOption).toStdString();
    const std::string content_to_insert = parser.value(contentToInsertOption).toStdString();
    const int line_insert = parser.value(lineInsertOption).toInt();
    const int line_delete = parser.value(lineDeleteOption).toInt();
    const std::string diff_text = parser.value(diffTextOption).toStdString();

    if (!content_to_write.empty()) {
        return write_test_file(path, content_to_write) ? 0 : 2;
    }
    if (!diff_text.empty()) {
        return apply_diff_write_file(path, diff_text) ? 0 : 2;
    }
    if (!content_to_insert.empty() && line_insert > 0) {
        return insert_content_at_line(path, content_to_insert, line_insert) ? 0 : 2;
    }
    if (line_delete > 0) {
        return delete_content_at_line(path, line_delete) ? 0 : 2;
    }

    return write_explicit_result(QString(), "no operation specified");
}
