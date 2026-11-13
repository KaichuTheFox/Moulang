#include "moulang/driver.hpp"
#include "moulang/parser.hpp"
#include "moulang/semantic.hpp"
#include "moulang/tiki.hpp"
#include "toolchain.hpp"
#include "diagnostics.hpp"

#include <Windows.h>
#include <fcntl.h>
#include <io.h>
#include <algorithm>
#include <iostream>
#include <iterator>
#include <sstream>

namespace {
namespace fs = std::filesystem;
enum class DiagnosticFormat { automatic, plain, rich };

bool diagnosticOption(std::wstring_view option, DiagnosticFormat& format) {
    if (format != DiagnosticFormat::automatic) return false;
    if (option == L"--diagnostic-format=plain") format = DiagnosticFormat::plain;
    else if (option == L"--diagnostic-format=rich") format = DiagnosticFormat::rich;
    else return false;
    return true;
}

bool showDiagnosticDetail(DiagnosticFormat format) {
    if (format != DiagnosticFormat::automatic) return format == DiagnosticFormat::rich;
    DWORD mode;
    return GetConsoleMode(GetStdHandle(STD_ERROR_HANDLE), &mode) != 0;
}

void writeDiagnostic(const std::string& text) {
    const auto output = GetStdHandle(STD_ERROR_HANDLE);
    DWORD mode;
    if (GetConsoleMode(output, &mode)) {
        const int bytes = static_cast<int>(text.size());
        const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), bytes, nullptr, 0);
        if (size) {
            std::wstring wide(size, L'\0');
            if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), bytes, wide.data(), size)) {
                DWORD written;
                if (WriteConsoleW(output, wide.data(), size, &written, nullptr)) return;
            }
        }
    }
    std::cerr << text;
}

void reportDiagnostic(const moulang::Diagnostic& diagnostic, const std::vector<fs::path>& sources,
    const fs::path& input, const std::vector<std::string>& snapshots, bool rich, bool warning = false) {
    const auto location = diagnostic.location;
    const auto& source = location.file < sources.size() ? sources[location.file] : input;
    std::ostringstream text;
    text << moulang::pathText(source) << ':' << location.line << ':' << location.column
         << (warning ? ": warning " : ": error ") << diagnostic.code << ": " << diagnostic.what();
    if (warning && !rich) text << ' ' << diagnostic.detail.explanation << " Hint: " << diagnostic.detail.hint;
    text << '\n';
    if (rich) moulang::renderDiagnosticDetail(text, diagnostic, snapshots);
    writeDiagnostic(text.str());
}

void help(std::ostream& out) {
    const auto extension = moulang::sourceExtensions.front();
    out << "Usage:\n"
           "  moulang --version\n"
           "  moulang --help\n"
           "  moulang run <source" << extension << ">\n"
           "  moulang check <source" << extension << "> [--stdin] [--library]\n"
           "  moulang build <source" << extension << "> [-o <output.exe>] [-O0|-O2|-O3]\n"
           "  run, check, and build accept --diagnostic-format=plain or --diagnostic-format=rich.\n"
           "  check accepts --tiki=basic|perf|deep|all and --tiki-disable=CODE[,CODE...].\n";
    out << "  Moulang source extensions:";
    for (const auto suffix : moulang::sourceExtensions) out << ' ' << suffix;
    out << '\n';
}

bool buildOptions(int argc, wchar_t* argv[], fs::path& output, moulang::Optimization& optimization,
    DiagnosticFormat& format) {
    bool optimizationSet = false;
    for (int i = 3; i < argc; ++i) {
        const std::wstring_view option = argv[i];
        if (option == L"-o" && output.empty() && i + 1 < argc && argv[i + 1][0] != L'\0') {
            output = argv[++i];
        } else if (!optimizationSet && (option == L"-O0" || option == L"-O2" || option == L"-O3")) {
            optimizationSet = true;
            if (option == L"-O2") optimization = moulang::Optimization::speed;
            if (option == L"-O3") optimization = moulang::Optimization::aggressive;
        } else if (!diagnosticOption(option, format)) {
            return false;
        }
    }
    return true;
}

bool disableTikiRules(std::wstring_view rules, moulang::TikiOptions& tiki) {
    for (;;) {
        const auto comma = rules.find(L',');
        const auto code = rules.substr(0, comma);
        std::size_t index = 0;
        for (const auto id : moulang::tikiRuleIds) {
            if (std::ranges::equal(code, id)) break;
            ++index;
        }
        if (index == tiki.disabled.size() || tiki.disabled[index]) return false;
        tiki.disabled[index] = true;
        if (comma == std::wstring_view::npos) return true;
        rules.remove_prefix(comma + 1);
    }
}

bool checkOptions(int argc, wchar_t* argv[], bool& fromStdin, bool& library, DiagnosticFormat& format,
    moulang::TikiOptions& tiki) {
    bool disabled = false;
    constexpr std::wstring_view disablePrefix = L"--tiki-disable=";
    for (int i = 3; i < argc; ++i) {
        const std::wstring_view option = argv[i];
        if (option == L"--stdin" && !fromStdin) fromStdin = true;
        else if (option == L"--library" && !library) library = true;
        else if (option.starts_with(disablePrefix) && !disabled) {
            if (!disableTikiRules(option.substr(disablePrefix.size()), tiki)) return false;
            disabled = true;
        } else if (option.starts_with(L"--tiki=") && tiki.mode == moulang::TikiMode::off) {
            if (option == L"--tiki=basic") tiki.mode = moulang::TikiMode::basic;
            else if (option == L"--tiki=perf") tiki.mode = moulang::TikiMode::performance;
            else if (option == L"--tiki=deep") tiki.mode = moulang::TikiMode::deep;
            else if (option == L"--tiki=all") tiki.mode = moulang::TikiMode::all;
            else return false;
        } else if (!diagnosticOption(option, format)) return false;
    }
    return !disabled || tiki.mode != moulang::TikiMode::off;
}
}

int wmain(int argc, wchar_t* argv[]) {
    const std::wstring_view command = argc > 1 ? argv[1] : L"";
    if (argc == 2 && command == L"--help") {
        help(std::cout);
        return 0;
    }
    if (argc == 2 && command == L"--version") {
        std::cout << "Moulang " << moulang::version << '\n';
        return 0;
    }

    auto format = DiagnosticFormat::automatic;
    const bool run = command == L"run" && (argc == 3 || (argc == 4 && diagnosticOption(argv[3], format)));
    bool fromStdin = false;
    bool library = false;
    moulang::TikiOptions tiki;
    const bool check = command == L"check" && argc >= 3 && checkOptions(argc, argv, fromStdin, library, format, tiki);
    fs::path output;
    auto optimization = moulang::Optimization::none;
    const bool build = command == L"build" && argc >= 3 && buildOptions(argc, argv, output, optimization, format);
    if ((!run && !build && !check) || argv[2][0] == L'\0') {
        help(std::cerr);
        return 2;
    }

    const fs::path input = argv[2];
    std::vector<fs::path> sources;
    std::vector<std::string> snapshots;
    const bool rich = showDiagnosticDetail(format);
    try {
        std::optional<std::string> overlay;
        if (fromStdin) {
            if (_setmode(_fileno(stdin), _O_BINARY) == -1) {
                throw std::runtime_error("Cannot open standard input in binary mode.");
            }
            overlay.emplace(std::istreambuf_iterator<char>(std::cin), std::istreambuf_iterator<char>());
        }
        auto program = moulang::loadProgram(input, sources,
            overlay ? std::optional<std::string_view>(*overlay) : std::nullopt, rich ? &snapshots : nullptr);
        moulang::analyze(program, !library);
        if (check) {
            if (tiki.mode != moulang::TikiMode::off) {
                for (const auto& warning : moulang::inspect(program, tiki)) {
                    reportDiagnostic(warning, sources, input, snapshots, rich, true);
                }
            }
            return 0;
        }
        if (run) {
            return static_cast<int>(moulang::runExecutable(program));
        }
        if (output.empty()) output = input.filename().replace_extension(L".exe");
        if (fs::exists(output)) {
            for (const auto& source : sources) {
                if (fs::equivalent(source, output)) {
                    throw std::runtime_error("Output must not overwrite the source file.");
                }
            }
        }
        moulang::buildExecutable(program, output, optimization);
        return 0;
    } catch (const moulang::Diagnostic& diagnostic) {
        reportDiagnostic(diagnostic, sources, input, snapshots, rich);
    } catch (const std::exception& error) {
        std::cerr << "moulang: error ML4001: " << error.what() << '\n';
    }
    return 1;
}
