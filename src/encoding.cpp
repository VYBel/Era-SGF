#include "erasgf/encoding.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace erasgf {
namespace {
#include "codec_tables.inc"

std::u32string unicode(std::string_view s) {
    std::u32string out;
    for (std::size_t i = 0; i < s.size();) {
        const auto first = static_cast<unsigned char>(s[i++]);
        if (first < 0x80) { out += first; continue; }
        int count = 0;
        char32_t cp = 0, minimum = 0;
        if (first >= 0xC2 && first <= 0xDF) { count = 1; cp = first & 0x1F; minimum = 0x80; }
        else if (first >= 0xE0 && first <= 0xEF) { count = 2; cp = first & 0x0F; minimum = 0x800; }
        else if (first >= 0xF0 && first <= 0xF4) { count = 3; cp = first & 7; minimum = 0x10000; }
        else throw std::runtime_error("Invalid UTF-8 at byte " + std::to_string(i) + "; select the file encoding");
        if (i + static_cast<std::size_t>(count) > s.size()) throw std::runtime_error("Truncated UTF-8; select the file encoding");
        for (int n = 0; n < count; ++n) {
            const auto b = static_cast<unsigned char>(s[i++]);
            if ((b & 0xC0) != 0x80) throw std::runtime_error("Invalid UTF-8 continuation; select the file encoding");
            cp = (cp << 6) | (b & 0x3F);
        }
        if (cp < minimum || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) throw std::runtime_error("Invalid Unicode code point in UTF-8");
        out += cp;
    }
    return out;
}
void append(std::string& out, char32_t cp) {
    if (cp < 0x80) out += static_cast<char>(cp);
    else if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 63)); }
    else { out += static_cast<char>(0xE0 | (cp >> 12)); out += static_cast<char>(0x80 | ((cp >> 6) & 63)); out += static_cast<char>(0x80 | (cp & 63)); }
}

struct Score { std::string encoding; double score; int letters; int non_ascii; };
Score russian_score(std::string_view text, const std::string& encoding) {
    const auto characters = unicode(decode_text(text, encoding));
    static const std::u32string alphabet = U"оеаинтсрвлкмдпуяыьгзбчйхжшюцщэфъё";
    static const double frequencies[] = {10.97, 8.45, 8.01, 7.35, 6.70, 6.26, 5.47, 4.73, 4.54, 4.40, 3.49, 3.21, 2.98, 2.81, 2.62, 2.01, 1.90, 1.74, 1.70, 1.65, 1.59, 1.44, 1.21, .97, .94, .73, .64, .48, .36, .32, .26, .04, .04};
    static const std::u32string pairs = U"ст но то на ен ов ни ра во ко ро по пр ос не ре та ал ли ер ор от ан ка ла ил де од ел те ол ар ва ти ет ый ий ых ые ье че ме из за об ок пе ри ру гр па рт ви до же рн ны";
    Score result{encoding, 0, 0, 0};
    char32_t previous = 0;
    for (char32_t cp : characters) {
        if (cp > 127) ++result.non_ascii;
        const bool upper = (cp >= U'А' && cp <= U'Я') || cp == U'Ё';
        if (cp >= U'А' && cp <= U'Я') cp += 32;
        if (cp == U'Ё') cp = U'ё';
        const auto index = alphabet.find(cp);
        if (index != std::u32string::npos) {
            ++result.letters;
            result.score += std::log(frequencies[index] / 100.0) + 4.0;
            if (upper && previous) result.score -= 4.0;
            if (previous) {
                std::u32string pair{previous, cp};
                if (pairs.find(pair) != std::u32string::npos) result.score += 1.8;
                if ((previous == U'ь' || previous == U'ъ') && (cp == U'ь' || cp == U'ъ' || cp == U'ы')) result.score -= 7.0;
            }
            previous = cp;
        } else {
            previous = 0;
            if (cp >= 0x2500 && cp <= 0x259F) result.score -= 9;
            else if (cp >= 0x80 && cp < 0xA0) result.score -= 10;
            else if (cp > 127 && cp != 0x2014 && cp != 0x2013 && cp != 0xAB && cp != 0xBB && cp != 0x201C && cp != 0x201D) result.score -= 5;
        }
    }
    return result;
}
}

std::string normalize_encoding(std::string_view name) {
    std::string key;
    for (char c : name) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 'a' - 'A');
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) key += c;
    }
    if (key.empty() || key == "auto") return "auto";
    if (key == "utf8" || key == "unicode11utf8") return "UTF-8";
    if (key == "cp1251" || key == "windows1251" || key == "mscyrl") return "Windows-1251";
    if (key == "koi8r" || key == "cskoi8r") return "KOI8-R";
    if (key == "cp866" || key == "ibm866" || key == "866") return "CP866";
    if (key == "iso88595" || key == "cyrillic") return "ISO-8859-5";
    if (key == "iso88591" || key == "latin1" || key == "l1") return "ISO-8859-1";
    throw std::runtime_error("Unsupported encoding: " + std::string(name) + "; select a supported encoding manually");
}

std::string decode_text(std::string_view bytes, std::string_view encoding) {
    const auto name = normalize_encoding(encoding);
    if (name == "auto") throw std::runtime_error("A concrete encoding is required for decoding");
    if (name == "UTF-8") {
        (void)unicode(bytes);
        if (bytes.substr(0, 3) == "\xEF\xBB\xBF") bytes.remove_prefix(3);
        return std::string(bytes);
    }
    const char32_t* table = name == "Windows-1251" ? cp1251 : name == "KOI8-R" ? koi8r : name == "CP866" ? cp866 : name == "ISO-8859-5" ? iso88595 : nullptr;
    std::string result;
    result.reserve(bytes.size() * 2);
    for (unsigned char b : bytes) {
        const char32_t cp = b < 128 || !table ? b : table[b - 128];
        if (cp == 0xFFFD) throw std::runtime_error("Undefined byte in " + name + "; select another encoding");
        append(result, cp);
    }
    return result;
}

EncodingChoice choose_encoding(std::string_view sample, std::string_view declared, std::string_view manual) {
    auto selected = normalize_encoding(manual);
    if (selected != "auto") { (void)decode_text(sample, selected); return {selected, false, "manual"}; }
    if (!declared.empty()) {
        selected = normalize_encoding(declared);
        if (selected == "auto") throw std::runtime_error("Invalid SGF CA[auto]; select a file encoding");
        (void)decode_text(sample, selected);
        return {selected, false, "SGF CA"};
    }
    try { (void)unicode(sample); return {"UTF-8", false, "valid UTF-8"}; } catch (const std::runtime_error&) {}
    std::vector<Score> scores;
    for (const auto& name : {"Windows-1251", "KOI8-R", "CP866", "ISO-8859-5"}) {
        try { scores.push_back(russian_score(sample, name)); } catch (const std::runtime_error&) {}
    }
    std::sort(scores.begin(), scores.end(), [](const Score& a, const Score& b) { return a.score > b.score; });
    if (scores.size() > 1 && scores[0].letters >= 8 && scores[0].letters * 100 >= scores[0].non_ascii * 80 && scores[0].score - scores[1].score >= 3.0)
        return {scores[0].encoding, false, "Cyrillic heuristic; manual override available"};
    return {"ISO-8859-1", true, "ambiguous: SGF default; select encoding if text is unreadable"};
}

const std::vector<std::string>& supported_encodings() {
    static const std::vector<std::string> names{"UTF-8", "Windows-1251", "KOI8-R", "CP866", "ISO-8859-5", "ISO-8859-1"};
    return names;
}
}
