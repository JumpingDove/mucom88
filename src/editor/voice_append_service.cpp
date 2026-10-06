#include "editor/voice_append_service.h"
#include <regex>
#include <set>
#include <sstream>
namespace mucom88 {
ServiceResult<TextTransformPreview> VoiceAppendService::Preview(
    const DocumentSnapshot &source, const CompiledSong &song,
    const VoiceBankSnapshot &bank) const
{
    auto fail = [&](ServiceErrorCode code, const char *message) -> ServiceResult<TextTransformPreview> {
        return {{}, {code, message, source.path, true}};
    };
    if (song.document_id != source.document_id || song.revision != source.revision)
        return fail(ServiceErrorCode::Conflict, "Compile results belong to a different document revision.");
    if (song.driver != DriverMode::Mucom88 && song.driver != DriverMode::Automatic &&
        song.driver != DriverMode::Mucom88E && song.driver != DriverMode::Mucom88EM)
        return fail(ServiceErrorCode::UnsupportedDriver, "Voice append supports the classic MUCOM88 driver.");
    DocumentService validation;
    const auto valid = validation.ReplaceText(source.utf8_text);
    if (!valid.Succeeded()) return {{}, valid.error};
    std::set<int> used(song.used_voice_numbers.begin(), song.used_voice_numbers.end());
    for (int number : used) if (number < 0 || number > 255)
        return fail(ServiceErrorCode::InvalidData, "Invalid used voice number.");
    std::set<int> defined;
    std::istringstream lines(source.utf8_text);
    std::string line;
    const std::regex definition(R"(^[ \t]*@[ \t]*([0-9]{1,3})[ \t]*:[ \t]*\{)");
    while (std::getline(lines, line)) {
        std::smatch match;
        if (std::regex_search(line, match, definition)) defined.insert(std::stoi(match[1]));
    }
    std::ostringstream appended;
    for (int number : used) {
        if (defined.count(number)) continue;
        const auto &tone = bank.voices[static_cast<std::size_t>(number)];
        if (tone.algorithm > 7 || tone.feedback > 7)
            return fail(ServiceErrorCode::InvalidData, "Invalid voice algorithm or feedback.");
        for (const auto &op : tone.operators) {
            if (op.dt > 7 || op.ml > 15 || op.tl > 127 || op.ks > 3 ||
                op.ar > 31 || op.dr > 31 || op.sr > 31 || op.sl > 15 || op.rr > 15)
                return fail(ServiceErrorCode::InvalidData, "Invalid voice operator parameter.");
            if (op.am) return fail(ServiceErrorCode::UnsupportedFormat,
                "Classic inline voice definitions cannot preserve AM flags.");
        }
        std::string name;
        for (unsigned char c : tone.name)
            name += c >= 32 && c < 127 && c != '"' && c != '{' && c != '}' && c != '\\' ? char(c) : ' ';
        while (!name.empty() && name.back() == ' ') name.pop_back();
        appended << "  @" << number << ":{\n  " << int(tone.feedback) << ',' << int(tone.algorithm) << '\n';
        for (std::size_t i = 0; i < tone.operators.size(); ++i) {
            const auto &op = tone.operators[i];
            appended << "  " << int(op.ar) << ',' << int(op.dr) << ',' << int(op.sr) << ','
                << int(op.rr) << ',' << int(op.sl) << ',' << int(op.tl) << ','
                << int(op.ks) << ',' << int(op.ml) << ',' << int(op.dt);
            if (i == 3) appended << ",\"" << name << "\"}";
            appended << '\n';
        }
    }
    TextTransformPreview preview{source.document_id, source.revision, source.utf8_text, source.line_endings};
    std::string suffix = appended.str();
    if (!suffix.empty()) {
        if (!preview.utf8_text.empty() && preview.utf8_text.back() != '\n') suffix.insert(0, 1, '\n');
        for (char c : suffix) if (c == '\n') preview.line_endings->push_back(source.preferred_newline);
        preview.utf8_text += suffix;
    }
    return {std::move(preview), {}};
}
}
