#include "thunder/content/definition/ContentLoader.hpp"
#include "thunder/foundation/base/Hash.hpp"

namespace thunder {

ContentLoadResult ContentLoader::load(const VirtualFileSystem& vfs, DefinitionDatabase& definitions) const {
    ContentLoadResult result;
    Fnv1a64 hash;
    if (vfs.has_load_plan()) {
        hash.add(std::string_view{"thunder.mod-load-plan.v1"});
        hash.add(vfs.load_plan_hash());
    }
    const auto all_files = vfs.enumerate();
    std::vector<VfsFile> files;
    for (const auto& file : all_files) {
        const auto ext = file.physical_path.extension().string();
        if (ext == ".thunder") {
            files.push_back(file);
        }
    }
    result.file_count = files.size();
    ThunderScriptParser parser{symbols_};

    for (const auto& file : files) {
        const auto text = VirtualFileSystem::read_text(file);
        hash.add(file.priority);
        hash.add(file.logical_path);
        hash.add(std::string_view{text});
        const auto parsed = parser.parse(text, file.logical_path);
        result.object_count += parsed.objects.size();
        if (!parsed.ok()) {
            for (const auto& d : parsed.diagnostics) result.diagnostics.push_back({d.message, d.line});
            continue;
        }
        definitions.localization().ingest(parsed);
        (void)definitions.ingest(parsed, result.diagnostics);
        (void)definitions.compile_scripts(parsed, result.diagnostics);
        (void)definitions.ingest_gameplay(parsed, result.diagnostics);
    }
    (void)definitions.scripts().validate_links(symbols_, result.diagnostics);
    result.content_hash = hash.value();
    return result;
}

} // namespace thunder
