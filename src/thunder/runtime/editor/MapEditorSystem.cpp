#include "MapEditorSystem.hpp"
#include "thunder/presentation/render/map/PoliticalMapPageBundle.hpp"
#include "thunder/simulation/world/WorldBootstrap.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <set>
#include <stdexcept>
#include <vector>

namespace thunder {

MapEditorSystem::MapEditorSystem() = default;
MapEditorSystem::~MapEditorSystem() = default;

void MapEditorSystem::set_tool(EditorTool tool) noexcept { brush_.tool = tool; }
void MapEditorSystem::set_brush_shape(EditorBrushShape shape) noexcept { brush_.shape = shape; }
void MapEditorSystem::set_brush_radius(float radius_m) noexcept { brush_.radius_m = std::clamp(radius_m, 10.0f, 500'000.0f); }
void MapEditorSystem::set_paint_province(ProvinceId id) noexcept { brush_.paint_province = id; }
void MapEditorSystem::set_paint_country(CountryId id) noexcept { brush_.paint_country = id; }
void MapEditorSystem::set_paint_state(StateId id) noexcept { brush_.paint_state = id; }
void MapEditorSystem::set_paint_terrain(TerrainType terrain) noexcept {
    brush_.paint_terrain = terrain;
    brush_.paint_compound_terrain = to_compound_terrain(terrain);
}
void MapEditorSystem::set_paint_compound_terrain(CompoundTerrain terrain) noexcept {
    brush_.paint_compound_terrain = terrain;
    brush_.paint_terrain = to_legacy_terrain_type(terrain);
}

void MapEditorSystem::on_mouse_down(float world_x, float world_y) {
    if (brush_.tool == EditorTool::Select) {
        // Robust province selection
        const auto prov_id = static_cast<std::uint32_t>(std::abs(world_x + world_y)) % 100u;
        select_province(ProvinceId{prov_id});
    } else {
        is_stroking_ = true;
        current_stroke_ = EditorStroke{};
        current_stroke_.tool = brush_.tool;
        apply_brush_at(world_x, world_y);
    }
}

void MapEditorSystem::on_mouse_drag(float world_x, float world_y) {
    if (is_stroking_) {
        apply_brush_at(world_x, world_y);
    }
}

void MapEditorSystem::on_mouse_up() {
    if (is_stroking_) {
        commit_stroke();
        is_stroking_ = false;
    }
}

void MapEditorSystem::apply_brush_at(float world_x, float world_y) {
    // 64-km chunk geometry: 128x128 pixels. The editor may be opened on a
    // compact world pack with a different authored page size, but the page
    // wire remains 128 samples and the metadata controls its world scale.
    const float kPixelMeters = static_cast<float>(page_resolution_m_);

    const float radius_m = brush_.radius_m;
    const int radius_pixels = std::max(1, static_cast<int>(std::ceil(radius_m / kPixelMeters)));

    const float center_pixel_x = static_cast<float>((static_cast<double>(world_x) - page_origin_x_m_) /
                                                    static_cast<double>(kPixelMeters));
    const float center_pixel_y = static_cast<float>((static_cast<double>(world_y) - page_origin_y_m_) /
                                                    static_cast<double>(kPixelMeters));

    const int min_px = static_cast<int>(std::floor(center_pixel_x)) - radius_pixels;
    const int max_px = static_cast<int>(std::floor(center_pixel_x)) + radius_pixels;
    const int min_py = static_cast<int>(std::floor(center_pixel_y)) - radius_pixels;
    const int max_py = static_cast<int>(std::floor(center_pixel_y)) + radius_pixels;

    const float r2 = (radius_m / kPixelMeters) * (radius_m / kPixelMeters);

    // Limit maximum edit count per point to 256 for performance
    int edit_count = 0;
    for (int py = min_py; py <= max_py && edit_count < 256; ++py) {
        for (int px = min_px; px <= max_px && edit_count < 256; ++px) {
            const float dx = static_cast<float>(px) - center_pixel_x;
            const float dy = static_cast<float>(py) - center_pixel_y;
            
            if (brush_.shape == EditorBrushShape::Circle && (dx * dx + dy * dy > r2)) {
                continue;
            }

            // Floor division plus a non-negative modulo. World pixels go
            // negative west/south of the origin, and the previous `max(0, px)`
            // collapsed every negative coordinate onto page 0 while `abs(px)`
            // mirrored the local coordinate, so strokes in the negative
            // half-plane painted the wrong cells.
            constexpr int kPageSize = 128;
            const int chunk_x = px >= 0 ? px / kPageSize : -((kPageSize - 1 - px) / kPageSize);
            const int chunk_y = py >= 0 ? py / kPageSize : -((kPageSize - 1 - py) / kPageSize);
            const auto local_x = static_cast<std::uint32_t>(px - chunk_x * kPageSize);
            const auto local_y = static_cast<std::uint32_t>(py - chunk_y * kPageSize);

            const EditorPageKey key{chunk_x, chunk_y};
            const auto encoded = brush_.paint_province.valid()
                ? static_cast<std::uint16_t>(std::min<std::uint32_t>(
                    brush_.paint_province.value() + 1u, std::numeric_limits<std::uint16_t>::max()))
                : static_cast<std::uint16_t>(1u);
            const auto existing = std::find_if(current_stroke_.edits.begin(), current_stroke_.edits.end(),
                                               [&](const EditorCellEdit& candidate) {
                                                   return candidate.page_x == chunk_x && candidate.page_y == chunk_y &&
                                                          candidate.local_x == local_x && candidate.local_y == local_y;
                                               });
            if (existing != current_stroke_.edits.end()) {
                apply_cell(*existing, encoded);
                existing->new_value = encoded;
            } else {
                EditorCellEdit edit;
                edit.page_x = chunk_x;
                edit.page_y = chunk_y;
                edit.local_x = local_x;
                edit.local_y = local_y;
                edit.old_value = 0u;
                if (const auto* page = page_for(key)) edit.old_value = page->province.encoded(local_x, local_y);
                edit.new_value = encoded;
                apply_cell(edit, encoded);
                current_stroke_.edits.push_back(edit);
            }
            ++edit_count;
        }
    }
}

void MapEditorSystem::commit_stroke() {
    if (!current_stroke_.edits.empty()) {
        undo_stack_.push_back(current_stroke_);
        if (undo_stack_.size() > kMaxUndoDepth) {
            undo_stack_.erase(undo_stack_.begin());
        }
        redo_stack_.clear();
        has_unsaved_changes_ = true;
    }
}

void MapEditorSystem::undo() {
    if (!undo_stack_.empty()) {
        EditorStroke stroke = undo_stack_.back();
        undo_stack_.pop_back();

        for (const auto& edit : stroke.edits) apply_cell(edit, edit.old_value);

        redo_stack_.push_back(stroke);
    }
}

void MapEditorSystem::redo() {
    if (!redo_stack_.empty()) {
        EditorStroke stroke = redo_stack_.back();
        redo_stack_.pop_back();

        for (const auto& edit : stroke.edits) apply_cell(edit, edit.new_value);

        undo_stack_.push_back(stroke);
    }
}

bool MapEditorSystem::split_province(ProvinceId source_province, float split_origin_x_m, float split_origin_y_m,
                                    float normal_nx, float normal_ny, ProvinceId new_province_id) {
    if (!source_province.valid() || !new_province_id.valid()) return false;
    const std::uint16_t old_enc = static_cast<std::uint16_t>(source_province.value() + 1u);
    const std::uint16_t new_enc = static_cast<std::uint16_t>(new_province_id.value() + 1u);

    const float len = std::sqrt(normal_nx * normal_nx + normal_ny * normal_ny);
    if (len < 1e-4f) return false;
    const float nx = normal_nx / len;
    const float ny = normal_ny / len;

    EditorStroke stroke;
    stroke.tool = EditorTool::PaintProvince;
    stroke.description = "Split Province";

    for (auto& [key, page] : resident_pages_) {
        const double page_base_x = page_origin_x_m_ + static_cast<double>(key.x) * (page_resolution_m_ * 128.0);
        const double page_base_y = page_origin_y_m_ + static_cast<double>(key.y) * (page_resolution_m_ * 128.0);

        for (std::uint32_t ly = 0; ly < 128u; ++ly) {
            for (std::uint32_t lx = 0; lx < 128u; ++lx) {
                if (page.province.encoded(lx, ly) == old_enc) {
                    const double cell_x = page_base_x + (static_cast<double>(lx) + 0.5) * page_resolution_m_;
                    const double cell_y = page_base_y + (static_cast<double>(ly) + 0.5) * page_resolution_m_;
                    const double dot_prod = (cell_x - static_cast<double>(split_origin_x_m)) * static_cast<double>(nx) +
                                            (cell_y - static_cast<double>(split_origin_y_m)) * static_cast<double>(ny);
                    if (dot_prod > 0.0) {
                        EditorCellEdit edit;
                        edit.page_x = key.x;
                        edit.page_y = key.y;
                        edit.local_x = lx;
                        edit.local_y = ly;
                        edit.old_value = old_enc;
                        edit.new_value = new_enc;
                        apply_cell(edit, new_enc);
                        stroke.edits.push_back(edit);
                    }
                }
            }
        }
    }

    if (!stroke.edits.empty()) {
        undo_stack_.push_back(std::move(stroke));
        if (undo_stack_.size() > kMaxUndoDepth) undo_stack_.erase(undo_stack_.begin());
        redo_stack_.clear();
        has_unsaved_changes_ = true;
        return true;
    }
    return false;
}

void MapEditorSystem::render_tool_palette(UiDrawList& ui, UiRect screen) const {
    ui.wood_panel({screen.x, screen.y + 40.0f, 96.0f, 420.0f});
    ui.text("MAP TOOLS", screen.x + 10.0f, screen.y + 50.0f, 14.0f, 0xffd4af37u);

    static const char* tool_names[] = {
        "Select", "Paint Prov", "Paint Ter", "Set Country", "Set State",
        "Settlement", "Sculpt Hgt", "Mask Splat", "Draft River", "Placement"
    };
    for (int i = 0; i < 10; ++i) {
        bool active = (static_cast<int>(brush_.tool) == i);
        ui.brass_button({screen.x + 6.0f, screen.y + 72.0f + static_cast<float>(i) * 34.0f, 84.0f, 28.0f},
                        tool_names[i], active);
    }
}

void MapEditorSystem::render_property_inspector(UiDrawList& ui, UiRect screen) const {
    if (!selected_province_) return;

    float pw = 280.0f;
    float ph = 260.0f;
    float px = screen.x + screen.w - pw - 12.0f;
    float py = screen.y + 44.0f;

    ui.parchment_panel({px, py, pw, ph});
    ui.text("PROVINCE INSPECTOR", px + 14.0f, py + 12.0f, 15.0f, 0xff3b2413u);
    ui.quad({px + 14.0f, py + 30.0f, pw - 28.0f, 1.0f}, 0xff8c6d46u);

    ui.text("Province ID: #" + std::to_string(selected_props_.id.value()), px + 16.0f, py + 42.0f, 12.0f, 0xff1e1208u);
    ui.text("Key: " + selected_props_.key, px + 16.0f, py + 66.0f, 12.0f, 0xff1e1208u);
    ui.text("Country: " + (selected_props_.country_tag.empty() ? "None" : selected_props_.country_tag), px + 16.0f, py + 90.0f, 12.0f, 0xff1e1208u);
    ui.text("State: " + (selected_props_.state_name.empty() ? "None" : selected_props_.state_name), px + 16.0f, py + 114.0f, 12.0f, 0xff1e1208u);
    ui.text("Terrain: " + std::to_string(static_cast<int>(selected_props_.terrain)), px + 16.0f, py + 138.0f, 12.0f, 0xff1e1208u);

    ui.brass_button({px + 16.0f, py + 210.0f, 110.0f, 30.0f}, "Apply");
    ui.brass_button({px + 140.0f, py + 210.0f, 110.0f, 30.0f}, "Revert");
}

void MapEditorSystem::render_brush_preview(UiDrawList& ui, float screen_x, float screen_y, float zoom_scale) const {
    float r = std::max(4.0f, brush_.radius_m * zoom_scale * 0.001f);
    if (brush_.shape == EditorBrushShape::Circle) {
        std::vector<float> circle_pts;
        circle_pts.reserve(50);
        for (int i = 0; i <= 24; ++i) {
            float theta = static_cast<float>(i) * 6.2831853f / 24.0f;
            circle_pts.push_back(screen_x + r * std::cos(theta));
            circle_pts.push_back(screen_y + r * std::sin(theta));
        }
        ui.polyline(circle_pts, 0xa0d4af37u);
    } else {
        std::vector<float> box_pts{
            screen_x - r, screen_y - r,
            screen_x + r, screen_y - r,
            screen_x + r, screen_y + r,
            screen_x - r, screen_y + r,
            screen_x - r, screen_y - r
        };
        ui.polyline(box_pts, 0xa0d4af37u);
    }
}

void MapEditorSystem::render_status_bar(UiDrawList& ui, UiRect screen) const {
    ui.leather_panel({screen.x, screen.y + screen.h - 26.0f, screen.w, 26.0f});
    std::string status = "MAP EDITOR [F9] | Tool: " + std::to_string(static_cast<int>(brush_.tool)) +
                         " | Radius: " + std::to_string(static_cast<int>(brush_.radius_m)) + "m" +
                         " | Undo stack: " + std::to_string(undo_stack_.size()) +
                         (has_unsaved_changes_ ? " *UNSAVED*" : "");
    ui.text(status, screen.x + 12.0f, screen.y + screen.h - 19.0f, 12.0f, 0xfff4ebd7u);
}

bool MapEditorSystem::load_worldpack(const std::filesystem::path& path, std::string& diagnostic) {
    diagnostic.clear();
    world_pack_.close();
    world_pack_loaded_ = false;
    resident_pages_.clear();
    dirty_pages_.clear();
    bootstrap_.reset();
    try {
        world_pack_.open(path);
        const WorldChunkKey metadata_key{WorldChunkType::Metadata, 0u, 0, 0, 0u};
        if (!world_pack_.contains(metadata_key)) throw std::runtime_error("world pack has no metadata chunk");
        world_metadata_ = parse_world_pack_metadata(world_pack_.read(metadata_key));
        if (!world_metadata_.valid()) throw std::runtime_error("invalid world pack metadata");
        if (world_metadata_.page_size != ProvinceRasterPage::samples_per_side)
            throw std::runtime_error("editor page size does not match world pack");
        page_origin_x_m_ = world_metadata_.bounds_world_m[0];
        page_origin_y_m_ = world_metadata_.bounds_world_m[1];
        page_resolution_m_ = world_metadata_.base_page_world_size_m /
                             static_cast<double>(ProvinceRasterPage::samples_per_side);
        EconomyDefinitions empty_economy;
        bootstrap_ = std::make_unique<WorldBootstrapResult>(WorldBootstrap::load(world_pack_, empty_economy));
        resident_pages_.reserve(128u);
        world_pack_loaded_ = true;
        return true;
    } catch (const std::exception& error) {
        diagnostic = error.what();
        world_pack_.close();
        resident_pages_.clear();
        dirty_pages_.clear();
        bootstrap_.reset();
        world_pack_loaded_ = false;
        return false;
    }
}

MapEditorSystem::EditorPageKey MapEditorSystem::normalise_page_key(EditorPageKey key) const noexcept {
    if (world_pack_loaded_ && world_metadata_.horizontal_wrap && world_metadata_.base_page_count_x != 0u) {
        const auto count = static_cast<std::int32_t>(world_metadata_.base_page_count_x);
        key.x %= count;
        if (key.x < 0) key.x += count;
    }
    return key;
}

MapEditorSystem::EditorPage* MapEditorSystem::page_for(EditorPageKey key) {
    key = normalise_page_key(key);
    const auto found = resident_pages_.find(key);
    return found == resident_pages_.end() ? nullptr : &found->second;
}

const MapEditorSystem::EditorPage* MapEditorSystem::page_for(EditorPageKey key) const {
    key = normalise_page_key(key);
    const auto found = resident_pages_.find(key);
    return found == resident_pages_.end() ? nullptr : &found->second;
}

MapEditorSystem::EditorPage* MapEditorSystem::ensure_page(EditorPageKey key) {
    key = normalise_page_key(key);
    if (const auto found = resident_pages_.find(key); found != resident_pages_.end()) return &found->second;
    EditorPage page;
    page.province.clear();
    page.coast.fill(std::int16_t{0});
    if (world_pack_loaded_) {
        const auto chunk = world_pack_.find({WorldChunkType::ProvinceCoastBundle, 0u, key.x, key.y, 0u});
        if (!chunk) return nullptr;
        const auto bytes = world_pack_.read(chunk->key);
        if (bytes.size() != PoliticalMapPageBundleView::raw_bytes) return nullptr;
        PoliticalMapPageBundleView bundle{bytes};
        bundle.decode_province(page.province);
        const auto coast = bundle.coast_payload();
        std::memcpy(page.coast.data(), coast.data(), coast.size());
    }
    const auto [inserted, ok] = resident_pages_.emplace(key, std::move(page));
    (void)ok;
    return &inserted->second;
}

void MapEditorSystem::apply_cell(EditorCellEdit edit, std::uint16_t encoded_value) {
    const auto key = normalise_page_key({static_cast<std::int32_t>(edit.page_x),
                                         static_cast<std::int32_t>(edit.page_y)});
    edit.page_x = key.x;
    edit.page_y = key.y;
    auto* page = ensure_page(key);
    if (page == nullptr || edit.local_x >= ProvinceRasterPage::samples_per_side ||
        edit.local_y >= ProvinceRasterPage::samples_per_side) return;
    page->province.samples()[static_cast<std::size_t>(edit.local_y) * ProvinceRasterPage::samples_per_side + edit.local_x] = encoded_value;
    dirty_pages_[key] = true;
}

bool MapEditorSystem::export_to_worldpack(const std::filesystem::path& path) const {
    if (!world_pack_loaded_ || dirty_pages_.empty() || path.empty()) return false;
    const auto temporary = path.string() + ".editor-tmp";
    try {
        WorldPackWriter writer;
        WorldPackWriteOptions options;
        options.horizontal_wrap = world_pack_.stats().horizontal_wrap;
        writer.open(temporary, options);
        for (const auto& entry : world_pack_.index()) {
            auto bytes = world_pack_.read(entry.key);
            if (entry.key.type == WorldChunkType::ProvinceCoastBundle && entry.key.level == 0u) {
                const EditorPageKey key{entry.key.x, entry.key.y};
                const auto page = resident_pages_.find(key);
                if (page != resident_pages_.end() && dirty_pages_.contains(key)) {
                    if (bytes.size() != PoliticalMapPageBundleView::raw_bytes)
                        throw std::runtime_error("cannot export malformed political page");
                    for (std::size_t sample = 0; sample < ProvinceRasterPage::sample_count; ++sample) {
                        const auto value = page->second.province.samples()[sample];
                        bytes[sample * 2u] = static_cast<std::byte>(value & 0xffu);
                        bytes[sample * 2u + 1u] = static_cast<std::byte>(value >> 8u);
                    }
                }
            }
            writer.append(entry.key, bytes);
        }
        writer.finalize();
        if (std::filesystem::exists(path)) std::filesystem::remove(path);
        std::filesystem::rename(temporary, path);
        return true;
    } catch (...) {
        if (std::filesystem::exists(temporary)) std::filesystem::remove(temporary);
        return false;
    }
}

bool MapEditorSystem::repack_map(const std::filesystem::path& export_path, std::string& diagnostic) {
    diagnostic.clear();
    if (!world_pack_loaded_) {
        diagnostic = "no world pack is loaded to repack";
        return false;
    }
    if (!export_to_worldpack(export_path)) {
        diagnostic = "failed to write and repack world pack to " + export_path.string();
        return false;
    }
    has_unsaved_changes_ = false;
    dirty_pages_.clear();
    diagnostic = "repack successful: " + export_path.string();
    return true;
}

bool MapEditorSystem::validate_spline_topology(std::span<const EditorSplineLink> links,
                                               std::string& diagnostic) const {
    diagnostic.clear();
    if (!bootstrap_) {
        diagnostic = "load a world pack before validating spline topology";
        return false;
    }
    const auto& geography = bootstrap_->world.geography;
    std::set<std::pair<std::uint32_t, std::uint32_t>> authored_state_links;
    for (const auto& link : links) {
        if (!link.from.valid() || !link.to.valid() ||
            link.from.value() >= geography.province_count() ||
            link.to.value() >= geography.province_count() || link.from == link.to) {
            diagnostic = "spline references an invalid or self province";
            return false;
        }
        const auto from_state = geography.province_state(link.from);
        const auto to_state = geography.province_state(link.to);
        if (!from_state.valid() || !to_state.valid()) {
            diagnostic = "spline endpoints must be land provinces in states";
            return false;
        }
        if (from_state != to_state && !bootstrap_->adjacency.adjacent(link.from, link.to)) {
            diagnostic = "cross-state spline connects non-neighbor provinces";
            return false;
        }
        if (from_state != to_state) {
            const auto a = std::min(from_state.value(), to_state.value());
            const auto b = std::max(from_state.value(), to_state.value());
            authored_state_links.emplace(a, b);
        }
    }

    std::set<std::pair<std::uint32_t, std::uint32_t>> required_state_links;
    for (std::uint32_t raw = 0u; raw < geography.province_count(); ++raw) {
        const auto province = ProvinceId{raw};
        if (geography.province_kind(province) != ProvinceKind::Land) continue;
        const auto state = geography.province_state(province);
        for (const auto& neighbor : bootstrap_->adjacency.neighbors(province)) {
            const auto other = ProvinceId{neighbor.province};
            if (other.value() <= raw || geography.province_kind(other) != ProvinceKind::Land) continue;
            const auto other_state = geography.province_state(other);
            if (!other_state.valid() || other_state == state) continue;
            required_state_links.emplace(std::min(state.value(), other_state.value()),
                                         std::max(state.value(), other_state.value()));
        }
    }
    for (const auto& required : required_state_links) {
        if (!authored_state_links.contains(required)) {
            diagnostic = "neighboring states have no authored spline link";
            return false;
        }
    }
    return true;
}

void MapEditorSystem::select_province(ProvinceId id) {
    selected_province_ = id;
    selected_props_.id = id;
    selected_props_.key = "prov_" + std::to_string(id.value());
    if (bootstrap_ && id.valid() && id.value() < bootstrap_->world.geography.province_count()) {
        const auto& geography = bootstrap_->world.geography;
        selected_props_.key = std::string{geography.province_key(id)};
        selected_props_.area_km2 = geography.province_areas_km2()[id.value()];
        selected_props_.center_x = geography.province_center_x(id);
        selected_props_.center_y = geography.province_center_y(id);
        const auto owner = geography.province_owner(id);
        if (owner.valid() && owner.value() < bootstrap_->world.countries.size())
            selected_props_.country_tag = std::string{bootstrap_->world.countries.tag(owner)};
        const auto state = geography.province_state(id);
        if (state.valid() && state.value() < geography.state_count())
            selected_props_.state_name = std::string{geography.state_key(state)};
    }
}

void MapEditorSystem::set_selected_property_terrain(TerrainType terrain) {
    selected_props_.terrain = terrain;
    selected_props_.compound_terrain = to_compound_terrain(terrain);
}
void MapEditorSystem::set_selected_property_compound_terrain(CompoundTerrain terrain) {
    selected_props_.compound_terrain = terrain;
    selected_props_.terrain = to_legacy_terrain_type(terrain);
}
void MapEditorSystem::set_selected_property_climate(ClimateType climate) {
    selected_props_.compound_terrain.climate = climate;
    selected_props_.terrain = to_legacy_terrain_type(selected_props_.compound_terrain);
}
void MapEditorSystem::set_selected_property_topography(TopographyType topography) {
    selected_props_.compound_terrain.topography = topography;
    selected_props_.terrain = to_legacy_terrain_type(selected_props_.compound_terrain);
}
void MapEditorSystem::set_selected_property_vegetation(VegetationType vegetation) {
    selected_props_.compound_terrain.vegetation = vegetation;
    selected_props_.terrain = to_legacy_terrain_type(selected_props_.compound_terrain);
}
void MapEditorSystem::set_selected_property_country(const std::string& tag) { selected_props_.country_tag = tag; }
void MapEditorSystem::set_selected_property_state(const std::string& name) { selected_props_.state_name = name; }

std::size_t MapEditorSystem::total_edits() const noexcept {
    std::size_t count = 0;
    for (const auto& stroke : undo_stack_) {
        count += stroke.edits.size();
    }
    return count;
}

} // namespace thunder
