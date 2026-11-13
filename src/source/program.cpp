#include "moulang/parser.hpp"
#include "moulang/standard.hpp"

#include <algorithm>
#include <iterator>
#include <optional>

namespace moulang {
namespace {
namespace fs = std::filesystem;

class SourceGraph {
public:
    SourceGraph(std::vector<fs::path>& sources, std::optional<std::string_view> entryText,
        std::vector<std::string>* snapshots)
        : sources_(sources), entryText_(entryText), snapshots_(snapshots) {}

    Program load(const fs::path& entry) {
        visit(entry, std::nullopt);
        for (const auto& path : sources_) program_.sourceNames.push_back(pathText(path));
        return std::move(program_);
    }

private:
    struct File {
        fs::path path;
        bool active;
    };

    std::size_t visit(const fs::path& path, std::optional<Location> importLocation) {
        std::error_code error;
        const auto canonical = fs::canonical(path, error);
        if (error || !fs::is_regular_file(canonical, error)) {
            const auto message = "Cannot open source file: " + pathText(path);
            if (importLocation) throw Diagnostic(*importLocation, "ML3028", message.c_str());
            throw std::runtime_error(message);
        }
        for (std::size_t i = 0; i < files_.size(); ++i) {
            const auto& file = files_[i];
            if (fs::equivalent(file.path, canonical)) {
                if (file.active) throw Diagnostic(*importLocation, "ML3029", "Cyclic source import.");
                return i;
            }
        }
        const auto index = files_.size();
        files_.push_back({canonical, true});
        sources_.push_back(path);
        program_.dependencies.emplace_back();
        std::string source;
        try {
            if (!importLocation && entryText_) {
                source = *entryText_;
                validateSource(source);
            } else {
                source = readSource(canonical);
            }
        } catch (Diagnostic& diagnostic) {
            diagnostic.location.file = index;
            throw;
        }
        if (snapshots_) snapshots_->push_back(std::move(source));
        const auto& text = snapshots_ ? snapshots_->back() : source;
        auto unit = Parser(text, index, program_.typeReferences).parse();
        if (importLocation) {
            for (const auto& function : unit.functions) {
                if (function.name == "main") {
                    throw Diagnostic(function.location, "ML3030", "Imported sources cannot define main.");
                }
            }
        }
        for (auto& import : unit.imports) {
            if (import.path.find('\0') != std::string::npos) {
                throw Diagnostic(import.location, "ML3028", "Import path must not contain NUL.");
            }
            if (standardImport(import.path)) {
                program_.imports.push_back(std::move(import));
                continue;
            }
            const fs::path relative(std::u8string(import.path.begin(), import.path.end()));
            const auto suffix = relative.extension();
            if (relative.has_root_path() || std::ranges::none_of(sourceExtensions, [&](std::string_view extension) {
                    return suffix == fs::path(extension);
                })) {
                throw Diagnostic(import.location, "ML3028", "Import requires a relative path with a supported Moulang source extension.");
            }
            const auto imported = visit(canonical.parent_path() / relative, import.location);
            program_.dependencies[index].push_back(imported);
        }
        program_.functions.insert(program_.functions.end(),
            std::make_move_iterator(unit.functions.begin()), std::make_move_iterator(unit.functions.end()));
        program_.structures.insert(program_.structures.end(),
            std::make_move_iterator(unit.structures.begin()), std::make_move_iterator(unit.structures.end()));
        files_[index].active = false;
        return index;
    }

    std::vector<fs::path>& sources_;
    std::optional<std::string_view> entryText_;
    std::vector<std::string>* snapshots_;
    std::vector<File> files_;
    Program program_;
};
}

Program loadProgram(const std::filesystem::path& entry, std::vector<std::filesystem::path>& sources,
    std::optional<std::string_view> entryText, std::vector<std::string>* snapshots) {
    sources.clear();
    if (snapshots) snapshots->clear();
    return SourceGraph(sources, entryText, snapshots).load(entry);
}
}
