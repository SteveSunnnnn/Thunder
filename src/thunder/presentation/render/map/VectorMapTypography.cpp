#include "thunder/presentation/render/map/VectorMapTypography.hpp"
#include <algorithm>
#include <cmath>
#include <string_view>
#include <vector>

namespace thunder {
namespace {

std::vector<std::string> utf8_codepoints(std::string_view text) {
    std::vector<std::string> result;
    for (std::size_t index = 0; index < text.size();) {
        const auto lead = static_cast<unsigned char>(text[index]);
        std::size_t count = lead < 0x80u ? 1u :
                            (lead & 0xe0u) == 0xc0u ? 2u :
                            (lead & 0xf0u) == 0xe0u ? 3u :
                            (lead & 0xf8u) == 0xf0u ? 4u : 1u;
        count = std::min(count, text.size() - index);
        result.emplace_back(text.substr(index, count));
        index += count;
    }
    return result;
}

std::vector<VectorPoint> fit_smooth_country_spine(std::span<const WorldMapLabelPoint> spine) {
    std::vector<VectorPoint> pts;
    if (spine.size() < 2u) return pts;

    const VectorPoint p0{spine.front().u, spine.front().v};
    const VectorPoint p_end{spine.back().u, spine.back().v};

    const float chord_x = p_end.x - p0.x;
    const float chord_y = p_end.y - p0.y;
    const float chord_len = std::sqrt(chord_x * chord_x + chord_y * chord_y);

    if (chord_len < 1.0e-5f) {
        pts.push_back(p0);
        pts.push_back(p_end);
        return pts;
    }

    const float tx = chord_x / chord_len;
    const float ty = chord_y / chord_len;
    const float nx = -ty;
    const float ny = tx;

    // Least-squares fit for parabolic arch deflection h_mid over interior points
    float num = 0.0f;
    float den = 0.0f;
    for (std::size_t i = 1; i + 1 < spine.size(); ++i) {
        const float px = spine[i].u - p0.x;
        const float py = spine[i].v - p0.y;
        const float t = (px * tx + py * ty) / chord_len;
        const float h = px * nx + py * ny;
        if (t > 0.05f && t < 0.95f) {
            const float basis = 4.0f * t * (1.0f - t);
            num += basis * h;
            den += basis * basis;
        }
    }

    float h_mid = 0.0f;
    if (den > 1.0e-5f) {
        h_mid = num / den;
    }

    // Clamp deflection to at most 8% of chord length for an elegant, majestic arc
    const float max_deflection = chord_len * 0.08f;
    h_mid = std::clamp(h_mid, -max_deflection, max_deflection);

    constexpr std::size_t kSamples = 9;
    pts.reserve(kSamples);
    for (std::size_t i = 0; i < kSamples; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(kSamples - 1);
        const float arch = 4.0f * h_mid * t * (1.0f - t);
        const float x = p0.x + t * chord_x + arch * nx;
        const float y = p0.y + t * chord_y + arch * ny;
        pts.push_back({x, y});
    }
    return pts;
}

} // namespace

std::vector<SplinePoint> VectorMapTypography::sample_spline(std::span<const VectorPoint> anchors,
                                                            std::size_t sample_count) {
    std::vector<SplinePoint> result;
    if (anchors.size() < 2) return result;
    if (sample_count < 2) sample_count = 2;

    // Pre-filter noisy raw spine anchors with 8-pass Laplacian smoothing on interior points,
    // keeping endpoints fixed to preserve total geographic extent while eliminating high-frequency kinks
    std::vector<VectorPoint> pts(anchors.begin(), anchors.end());
    if (pts.size() >= 3) {
        for (int pass = 0; pass < 8; ++pass) {
            std::vector<VectorPoint> next_pts = pts;
            for (std::size_t k = 1; k + 1 < pts.size(); ++k) {
                next_pts[k].x = 0.25f * pts[k - 1].x + 0.50f * pts[k].x + 0.25f * pts[k + 1].x;
                next_pts[k].y = 0.25f * pts[k - 1].y + 0.50f * pts[k].y + 0.25f * pts[k + 1].y;
            }
            pts = std::move(next_pts);
        }
    }

    result.reserve(sample_count);
    float accum_dist = 0.0f;
    const std::size_t n = pts.size();

    for (std::size_t i = 0; i < sample_count; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(sample_count - 1);
        const float scaled_t = t * static_cast<float>(n - 1);
        const std::size_t idx = std::min(n - 2, static_cast<std::size_t>(scaled_t));
        const float u = scaled_t - static_cast<float>(idx);

        const auto& p0 = pts[idx == 0 ? 0 : idx - 1];
        const auto& p1 = pts[idx];
        const auto& p2 = pts[idx + 1];
        const auto& p3 = pts[std::min(n - 1, idx + 2)];

        const float u2 = u * u;
        const float u3 = u2 * u;

        // Catmull-Rom cubic position
        const float x = 0.5f * (
            (2.0f * p1.x) +
            (-p0.x + p2.x) * u +
            (2.0f * p0.x - 5.0f * p1.x + 4.0f * p2.x - p3.x) * u2 +
            (-p0.x + 3.0f * p1.x - 3.0f * p2.x + p3.x) * u3
        );
        const float y = 0.5f * (
            (2.0f * p1.y) +
            (-p0.y + p2.y) * u +
            (2.0f * p0.y - 5.0f * p1.y + 4.0f * p2.y - p3.y) * u2 +
            (-p0.y + 3.0f * p1.y - 3.0f * p2.y + p3.y) * u3
        );

        // Continuous C1 derivative for tangent vector
        float dx = 0.5f * (
            (-p0.x + p2.x) +
            2.0f * (2.0f * p0.x - 5.0f * p1.x + 4.0f * p2.x - p3.x) * u +
            3.0f * (-p0.x + 3.0f * p1.x - 3.0f * p2.x + p3.x) * u2
        );
        float dy = 0.5f * (
            (-p0.y + p2.y) +
            2.0f * (2.0f * p0.y - 5.0f * p1.y + 4.0f * p2.y - p3.y) * u +
            3.0f * (-p0.y + 3.0f * p1.y - 3.0f * p2.y + p3.y) * u2
        );
        float len = std::sqrt(dx * dx + dy * dy);
        if (len < 1.0e-5f) {
            dx = p2.x - p1.x;
            dy = p2.y - p1.y;
            len = std::max(1.0e-5f, std::sqrt(dx * dx + dy * dy));
        }

        if (i > 0) {
            const auto& prev = result.back();
            accum_dist += std::sqrt((x - prev.x) * (x - prev.x) + (y - prev.y) * (y - prev.y));
        }

        result.push_back({x, y, dx / len, dy / len, accum_dist});
    }
    return result;
}

CurvedLabelLayout VectorMapTypography::layout_curved_label(std::string text,
                                                          std::span<const VectorPoint> anchors,
                                                          float font_size,
                                                          std::uint32_t rgba,
                                                          int priority,
                                                          float tracking_factor,
                                                          float fill_ratio) {
    CurvedLabelLayout layout;
    layout.text = std::move(text);
    layout.priority = priority;
    if (layout.text.empty() || anchors.size() < 2) return layout;

    const auto spline = sample_spline(anchors, 64);
    if (spline.empty()) return layout;

    const float total_length = spline.back().distance;
    const float safe_tracking = std::clamp(tracking_factor, 0.35f, 1.75f);
    const float safe_fill = std::clamp(fill_ratio, 0.45f, 1.0f);
    const auto codepoints = utf8_codepoints(layout.text);
    float char_spacing = font_size * 0.75f * safe_tracking;
    float total_text_w = static_cast<float>(codepoints.size()) * char_spacing;
    if (total_text_w > total_length * 1.25f) {
        // Preserve geographic label composition instead of letting a long
        // country name fold or stack vertically. The glyph scale is bounded
        // so small countries do not turn into unreadable hairlines.
        const float fit = std::clamp((total_length * safe_fill) /
                                         std::max(total_text_w, 1.0f),
                                     0.45f, 1.0f);
        font_size *= fit;
        char_spacing = font_size * 0.75f * safe_tracking;
        total_text_w = static_cast<float>(codepoints.size()) * char_spacing;
    }

    const float start_dist = std::max(0.0f, (total_length - total_text_w) * 0.5f);

    float min_x = 1e9f, max_x = -1e9f;
    float min_y = 1e9f, max_y = -1e9f;

    for (std::size_t c_idx = 0; c_idx < codepoints.size(); ++c_idx) {
        const float target_d = start_dist + (static_cast<float>(c_idx) + 0.5f) * char_spacing;

        // Find position on spline
        SplinePoint sp = spline[0];
        for (std::size_t s = 0; s + 1 < spline.size(); ++s) {
            if (target_d >= spline[s].distance && target_d <= spline[s + 1].distance) {
                const float seg_len = spline[s + 1].distance - spline[s].distance;
                const float frac = seg_len > 1.0e-4f ? (target_d - spline[s].distance) / seg_len : 0.0f;
                sp.x = spline[s].x + (spline[s + 1].x - spline[s].x) * frac;
                sp.y = spline[s].y + (spline[s + 1].y - spline[s].y) * frac;
                const float tx = spline[s].tangent_x + (spline[s + 1].tangent_x - spline[s].tangent_x) * frac;
                const float ty = spline[s].tangent_y + (spline[s + 1].tangent_y - spline[s].tangent_y) * frac;
                const float tlen = std::max(1.0e-5f, std::sqrt(tx * tx + ty * ty));
                sp.tangent_x = tx / tlen;
                sp.tangent_y = ty / tlen;
                break;
            }
        }

        const float angle = std::atan2(sp.tangent_y, sp.tangent_x);
        layout.glyphs.push_back({codepoints[c_idx], sp.x, sp.y, angle, font_size, rgba});
    }

    if (layout.glyphs.size() >= 2) {
        std::vector<float> angles(layout.glyphs.size());
        for (std::size_t i = 0; i < layout.glyphs.size(); ++i) {
            angles[i] = layout.glyphs[i].angle_rad;
        }
        for (std::size_t i = 1; i < angles.size(); ++i) {
            float diff = angles[i] - angles[i - 1];
            while (diff > 3.14159265f) { angles[i] -= 6.2831853f; diff -= 6.2831853f; }
            while (diff < -3.14159265f) { angles[i] += 6.2831853f; diff += 6.2831853f; }
        }
        if (angles.size() >= 3) {
            std::vector<float> sm = angles;
            for (std::size_t i = 1; i + 1 < angles.size(); ++i) {
                sm[i] = 0.25f * angles[i - 1] + 0.50f * angles[i] + 0.25f * angles[i + 1];
            }
            angles = sm;
        }
        constexpr float kMaxDelta = 0.045f;
        for (std::size_t i = 1; i < angles.size(); ++i) {
            float d = angles[i] - angles[i - 1];
            if (d > kMaxDelta) angles[i] = angles[i - 1] + kMaxDelta;
            else if (d < -kMaxDelta) angles[i] = angles[i - 1] - kMaxDelta;
        }
        for (std::size_t i = 0; i < layout.glyphs.size(); ++i) {
            layout.glyphs[i].angle_rad = angles[i];
        }
    }

    for (const auto& g : layout.glyphs) {
        min_x = std::min(min_x, g.x - font_size * 0.5f);
        max_x = std::max(max_x, g.x + font_size * 0.5f);
        min_y = std::min(min_y, g.y - font_size * 0.5f);
        max_y = std::max(max_y, g.y + font_size * 0.5f);
    }

    layout.aabb = {min_x, min_y, std::max(1.0f, max_x - min_x), std::max(1.0f, max_y - min_y)};
    return layout;
}

void VectorMapTypography::prune_collisions(std::span<CurvedLabelLayout> labels) {
    for (std::size_t i = 0; i < labels.size(); ++i) {
        if (!labels[i].is_visible) continue;
        for (std::size_t j = i + 1; j < labels.size(); ++j) {
            if (!labels[j].is_visible) continue;

            const auto& a = labels[i].aabb;
            const auto& b = labels[j].aabb;

            // Bounding box overlap test
            if (a.x < b.x + b.w && a.x + a.w > b.x &&
                a.y < b.y + b.h && a.y + a.h > b.y) {
                if (labels[i].priority >= labels[j].priority) {
                    labels[j].is_visible = false;
                } else {
                    labels[i].is_visible = false;
                    break;
                }
            }
        }
    }
}

void VectorMapTypography::render_labels(UiDrawList& ui, std::span<const CurvedLabelLayout> labels, UiRect scissor) {
    for (const auto& lbl : labels) {
        if (!lbl.is_visible) continue;
        for (const auto& g : lbl.glyphs) {
            ui.map_text(g.utf8, g.x, g.y, g.font_size, g.rgba,
                        g.angle_rad, 0.0f, scissor);
        }
    }
}

EngravedMapLabel VectorMapTypography::layout_engraved_country(
    std::string text,
    std::span<const WorldMapLabelPoint> spine,
    double geographic_area_km2,
    std::uint32_t rgba) {
    EngravedMapLabel label;
    label.text = std::move(text);
    if (label.text.empty() || spine.size() < 2u) return label;

    float min_u = 1.0f, max_u = 0.0f;
    float min_v = 1.0f, max_v = 0.0f;
    for (const auto pt : spine) {
        min_u = std::min(min_u, pt.u);
        max_u = std::max(max_u, pt.u);
        min_v = std::min(min_v, pt.v);
        max_v = std::max(max_v, pt.v);
    }
    std::vector<VectorPoint> anchors = fit_smooth_country_spine(spine);
    if (anchors.size() < 2u) {
        anchors.clear();
        anchors.reserve(spine.size());
        for (const auto pt : spine) {
            anchors.push_back({pt.u, pt.v});
        }
    }
    for (const auto& a : anchors) {
        min_u = std::min(min_u, a.x);
        max_u = std::max(max_u, a.x);
        min_v = std::min(min_v, a.y);
        max_v = std::max(max_v, a.y);
    }
    label.center_u = (min_u + max_u) * 0.5f;
    label.center_v = (min_v + max_v) * 0.5f;

    const auto spline = sample_spline(anchors, 64);
    if (spline.empty() || spline.back().distance < 1.0e-5f) return label;
    const float uv_length = spline.back().distance;

    const auto codepoints = utf8_codepoints(label.text);
    if (codepoints.empty()) return label;

    const float len_fs_uv = uv_length * 0.14f;
    const float area_fs_uv = static_cast<float>(std::sqrt(std::max(geographic_area_km2, 1.0) / 5.1e8) * 0.28);
    float font_size_uv = std::max(len_fs_uv, area_fs_uv);

    // Letter tracking multiplier; upper bound mirrors the official
    // COUNTRY_NAMES_MAX_STRETCH_FACTOR = 1.6 (names stretch, never squeeze).
    float tracking = std::clamp(
        uv_length / (std::max<float>(static_cast<float>(codepoints.size()), 1.0f) * std::max(font_size_uv * 0.6f, 0.005f)),
        1.10f, 1.60f);
    float char_spacing_uv = font_size_uv * 0.75f * tracking;
    float total_text_w = static_cast<float>(codepoints.size()) * char_spacing_uv;
    if (total_text_w > uv_length * 0.95f) {
        const float fit = (uv_length * 0.92f) / std::max(total_text_w, 1.0e-6f);
        font_size_uv *= fit;
        char_spacing_uv = font_size_uv * 0.75f * tracking;
        total_text_w = static_cast<float>(codepoints.size()) * char_spacing_uv;
    }

    const float start_dist = std::max(0.0f, (uv_length - total_text_w) * 0.5f);
    label.glyphs.reserve(codepoints.size());

    for (std::size_t c_idx = 0; c_idx < codepoints.size(); ++c_idx) {
        const float target_d = start_dist + (static_cast<float>(c_idx) + 0.5f) * char_spacing_uv;
        SplinePoint sp = spline[0];
        for (std::size_t s = 0; s + 1 < spline.size(); ++s) {
            if (target_d >= spline[s].distance && target_d <= spline[s + 1].distance) {
                const float seg_len = spline[s + 1].distance - spline[s].distance;
                const float frac = seg_len > 1.0e-5f ? (target_d - spline[s].distance) / seg_len : 0.0f;
                sp.x = spline[s].x + (spline[s + 1].x - spline[s].x) * frac;
                sp.y = spline[s].y + (spline[s + 1].y - spline[s].y) * frac;
                const float tx = spline[s].tangent_x + (spline[s + 1].tangent_x - spline[s].tangent_x) * frac;
                const float ty = spline[s].tangent_y + (spline[s + 1].tangent_y - spline[s].tangent_y) * frac;
                const float tlen = std::max(1.0e-5f, std::sqrt(tx * tx + ty * ty));
                sp.tangent_x = tx / tlen;
                sp.tangent_y = ty / tlen;
                break;
            }
        }
        label.glyphs.push_back({
            codepoints[c_idx],
            sp.x, sp.y,
            sp.tangent_x, sp.tangent_y,
            font_size_uv,
            rgba
        });
    }

    // Post-process glyph tangents for smooth, elegant country typography without jagged angle jumps
    if (label.glyphs.size() >= 2) {
        std::vector<float> angles(label.glyphs.size());
        for (std::size_t i = 0; i < label.glyphs.size(); ++i) {
            angles[i] = std::atan2(label.glyphs[i].tangent_v, label.glyphs[i].tangent_u);
        }
        // Phase unwrap
        for (std::size_t i = 1; i < angles.size(); ++i) {
            float diff = angles[i] - angles[i - 1];
            while (diff > 3.14159265f) { angles[i] -= 6.2831853f; diff -= 6.2831853f; }
            while (diff < -3.14159265f) { angles[i] += 6.2831853f; diff += 6.2831853f; }
        }
        // Smooth angles
        if (angles.size() >= 3) {
            std::vector<float> sm = angles;
            for (std::size_t i = 1; i + 1 < angles.size(); ++i) {
                sm[i] = 0.25f * angles[i - 1] + 0.50f * angles[i] + 0.25f * angles[i + 1];
            }
            angles = sm;
        }
        // Clamp inter-glyph curvature delta to max ~0.040 rad (~2.3 deg)
        constexpr float kMaxDelta = 0.040f;
        for (std::size_t i = 1; i < angles.size(); ++i) {
            float d = angles[i] - angles[i - 1];
            if (d > kMaxDelta) angles[i] = angles[i - 1] + kMaxDelta;
            else if (d < -kMaxDelta) angles[i] = angles[i - 1] - kMaxDelta;
        }
        for (std::size_t i = 0; i < label.glyphs.size(); ++i) {
            label.glyphs[i].tangent_u = std::cos(angles[i]);
            label.glyphs[i].tangent_v = std::sin(angles[i]);
        }
    }

    for (const auto& g : label.glyphs) {
        min_u = std::min(min_u, g.u - font_size_uv);
        max_u = std::max(max_u, g.u + font_size_uv);
        min_v = std::min(min_v, g.v - font_size_uv);
        max_v = std::max(max_v, g.v + font_size_uv);
    }
    label.min_u = min_u;
    label.max_u = max_u;
    label.min_v = min_v;
    label.max_v = max_v;
    return label;
}

std::vector<EngravedMapLabel> VectorMapTypography::prepare_engraved_countries(
    const WorldMapLabels& labels) {
    std::vector<EngravedMapLabel> result;
    // Engraved ink body at 80% alpha — matches the official Victoria 3 map
    // name define MAX_OPACITY = 0.8 (semi-transparent by design; "PlayfairDisplay"
    // is their map-name font, THICKNESS_BIAS = 0.0 means never emboldened).
    constexpr std::uint32_t kCountryInkColor = 0xcc221d19u;

    for (const auto& record : labels.records()) {
        if (record.kind != WorldMapLabelKind::Country) continue;

        std::span<const WorldMapLabelPoint> spine = record.spine;
        double area_km2 = record.geographic_area_km2;

        if (spine.size() < 2u) continue;
        auto label = layout_engraved_country(record.text, spine, area_km2, kCountryInkColor);
        label.key = record.key;
        if (!label.glyphs.empty()) {
            result.push_back(std::move(label));
        }
    }
    return result;
}

void VectorMapTypography::render_engraved_labels(
    UiDrawList& ui,
    std::span<const EngravedMapLabel> engraved_labels,
    double center_u, double center_v,
    double half_u, double half_v,
    int screen_width, int screen_height,
    bool horizontal_wrap) {
    if (screen_width <= 0 || screen_height <= 0 || half_u <= 1.0e-8 || half_v <= 1.0e-8) {
        return;
    }

    const double margin_u = half_u * 1.5;
    const double margin_v = half_v * 1.5;
    const double min_vis_u = center_u - margin_u;
    const double max_vis_u = center_u + margin_u;
    const double min_vis_v = center_v - margin_v;
    const double max_vis_v = center_v + margin_v;

    const float scale_x = static_cast<float>(static_cast<double>(screen_width) / (2.0 * half_u));
    const float scale_y = static_cast<float>(static_cast<double>(screen_height) / (2.0 * half_v));

    for (const auto& label : engraved_labels) {
        if (label.glyphs.empty()) continue;

        int k_min = 0;
        int k_max = 0;
        if (horizontal_wrap) {
            const double base_shift = std::round(center_u - static_cast<double>(label.center_u));
            const int base_k = static_cast<int>(base_shift);
            k_min = base_k - 1;
            k_max = base_k + 1;
        }

        for (int k = k_min; k <= k_max; ++k) {
            const double wrap_offset = -static_cast<double>(k);
            const double lbl_min_u = static_cast<double>(label.min_u) - wrap_offset;
            const double lbl_max_u = static_cast<double>(label.max_u) - wrap_offset;
            if (lbl_max_u < min_vis_u || lbl_min_u > max_vis_u ||
                static_cast<double>(label.max_v) < min_vis_v || static_cast<double>(label.min_v) > max_vis_v) {
                continue;
            }

            for (const auto& glyph : label.glyphs) {
                const double delta_u = static_cast<double>(glyph.u) - wrap_offset - center_u;
                const double delta_v = static_cast<double>(glyph.v) - center_v;
                const double ndc_x = delta_u / half_u;
                const double ndc_y = delta_v / half_v;
                const float sx = static_cast<float>((ndc_x * 0.5 + 0.5) * static_cast<double>(screen_width));
                const float sy = static_cast<float>((ndc_y * 0.5 + 0.5) * static_cast<double>(screen_height));

                const float screen_font_size = glyph.font_size_uv * scale_y;
                if (screen_font_size < 2.0f || screen_font_size >= 220.0f) continue;

                // Safe culling margin guarantees that a partially-visible glyph quad is never culled
                const float culling_margin = std::max(128.0f, screen_font_size * 1.5f);
                if (sx < -culling_margin || sx > static_cast<float>(screen_width) + culling_margin ||
                    sy < -culling_margin || sy > static_cast<float>(screen_height) + culling_margin) {
                    continue;
                }

                const float screen_tx = glyph.tangent_u * scale_x;
                const float screen_ty = glyph.tangent_v * scale_y;
                const float screen_angle = std::atan2(screen_ty, screen_tx);

                float alpha_factor = 1.0f;
                if (screen_font_size < 5.5f) {
                    // Smooth hermite fade-in for microstates and extreme far zoom (no hard pop at low end)
                    const float t_low = std::clamp((screen_font_size - 2.0f) / 3.5f, 0.0f, 1.0f);
                    alpha_factor = t_low * t_low * (3.0f - 2.0f * t_low);
                } else if (screen_font_size > 135.0f) {
                    // Smooth hermite fade-out when camera transitions into regional/tactical view
                    const float t_high = std::clamp((screen_font_size - 135.0f) / 85.0f, 0.0f, 1.0f);
                    alpha_factor = 1.0f - t_high * t_high * (3.0f - 2.0f * t_high);
                }

                std::uint32_t a = static_cast<std::uint32_t>(static_cast<float>((glyph.rgba >> 24u) & 0xffu) * alpha_factor);
                a = std::clamp(a, 0u, 255u);
                if (a == 0u) continue;
                const std::uint32_t final_color = (glyph.rgba & 0x00ffffffu) | (a << 24u);

                ui.map_text(glyph.utf8, sx, sy, screen_font_size, final_color, screen_angle);
            }
        }
    }
}

} // namespace thunder
