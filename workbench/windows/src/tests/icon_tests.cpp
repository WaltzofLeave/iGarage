#include "../app.h"

namespace {

struct AlphaBounds {
    int left = 1000, top = 1000, right = -1, bottom = -1;
};

AlphaBounds bounds(const orbit::IconPixels& image) {
    AlphaBounds result;
    for (int y = 0; y < image.size; ++y) for (int x = 0; x < image.size; ++x) {
        if (image.rgba[(y * image.size + x) * 4 + 3] <= 12) continue;
        result.left = std::min(result.left, x); result.right = std::max(result.right, x);
        result.top = std::min(result.top, y); result.bottom = std::max(result.bottom, y);
    }
    return result;
}

} // namespace

int App::iconTest(const fs::path& report) {
    json checks = json::array(), details = json::object();
    const auto check = [&](std::string name, bool passed) {
        checks.push_back({{"name", std::move(name)}, {"passed", passed}});
    };
    try {
        auto atlas = orbit::systemIcons();
        if (!atlas) throw std::runtime_error("Direct2D SVG device unavailable");
        std::vector<std::string> missing;
        for (const bool light : {false, true}) for (const float testDpi : {96.f, 144.f, 192.f}) {
            for (int id = 0; id < static_cast<int>(Icon::Count); ++id) {
                const auto icon = static_cast<Icon>(id);
                const auto& image = atlas->pixels(icon, 16, testDpi, light);
                if (image.size != std::lround(16 * testDpi / 96) || bounds(image).right < 0)
                    missing.emplace_back(orbit::iconAssetName(icon));
            }
        }
        check("every_icon_renders_in_both_themes_at_100_150_200_percent", missing.empty());
        details["empty_or_wrong_size_icons"] = missing;
        details["rendered_variants"] = static_cast<int>(Icon::Count) * 6;

        const auto& python = atlas->pixels(Icon::Python, 48, 96, false);
        int blue = 0, yellow = 0;
        for (std::size_t i = 0; i < python.rgba.size(); i += 4) {
            const auto* p = python.rgba.data() + i;
            if (p[3] < 100) continue;
            if (p[2] > p[0] * 1.2 && p[2] > 80) ++blue;
            if (p[0] > 130 && p[1] > 100 && p[2] < p[0] * .8) ++yellow;
        }
        check("official_python_gradients_render_as_blue_and_yellow", blue > 100 && yellow > 100);
        details["python_blue_yellow_pixel_counts"] = {blue, yellow};

        bool chevrons = true;
        for (const bool light : {false, true}) for (const float testDpi : {96.f, 144.f, 192.f}) {
            const auto& open = atlas->pixels(Icon::FoldOpen, 9, testDpi, light);
            const auto a = bounds(open);
            const auto centerAtTop = open.rgba[(a.top * open.size + open.size / 2) * 4 + 3];
            const auto& closed = atlas->pixels(Icon::FoldClosed, 9, testDpi, light);
            const auto b = bounds(closed);
            chevrons &= a.right >= 0 && b.right >= 0 && centerAtTop < 20 &&
                a.right - a.left < std::lround(10 * testDpi / 96) &&
                b.right - b.left < b.bottom - b.top && a.bottom - a.top < a.right - a.left;
        }
        check("fold_markers_are_narrow_open_strokes_without_triangle_top_edge", chevrons);
        check("file_types_and_symbols_share_consistent_real_icons",
            orbit::fileIcon("MAIN.PY") == Icon::Python && orbit::fileIcon(".gitignore") == Icon::GitIgnore &&
            orbit::fileIcon("pyproject.toml") == Icon::Toml && orbit::fileIcon("data.JSON") == Icon::Json &&
            orbit::fileIcon("README.md") == Icon::Markdown && orbit::fileIcon("unknown.data") == Icon::File &&
            orbit::symbolIcon('f', 1) == Icon::Method && orbit::symbolIcon('f') == Icon::Function);

        atlas->bitmap(Icon::Search, 17, dpi, false, theme.muted);
        const auto before = atlas->stats();
        const auto start = Clock::now();
        for (int i = 0; i < 10000; ++i) atlas->bitmap(Icon::Search, 17, dpi, false, theme.muted);
        const auto elapsed = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        const auto after = atlas->stats();
        check("warm_icon_lookups_do_not_read_files_or_parse_or_rasterize_svg",
            after.sourceLoads == before.sourceLoads && after.rasterizations == before.rasterizations &&
            after.cacheHits - before.cacheHits == 10000);
        check("gpu_icon_cache_is_bounded", after.bitmaps <= 256);
        details["warm_10000_lookup_ms"] = elapsed;
        details["warm_lookup_scope"] = "CPU cache access only; not GPU draw or input-to-screen timing.";
        details["source_loads"] = after.sourceLoads;
        details["rasterizations"] = after.rasterizations;

        check("native_fold_markers_use_rgba_instead_of_default_triangles",
            sci(SCI_MARKERSYMBOLDEFINED, SC_MARKNUM_FOLDER) == SC_MARK_RGBAIMAGE &&
            sci(SCI_MARKERSYMBOLDEFINED, SC_MARKNUM_FOLDEROPEN) == SC_MARK_RGBAIMAGE);
        check("fold_click_target_keeps_13_DIP_width_and_sensitivity",
            sci(SCI_GETMARGINWIDTHN, 1) == px(13) && sci(SCI_GETMARGINSENSITIVEN, 1));

        // Exercise folding in an isolated in-memory document without touching a project file.
        const auto original = sci(SCI_GETDOCPOINTER);
        sci(SCI_ADDREFDOCUMENT, 0, original);
        const bool previousSuppression = suppress;
        suppress = true;
        const auto temporary = sci(SCI_CREATEDOCUMENT);
        sci(SCI_SETDOCPOINTER, 0, temporary);
        sci(SCI_RELEASEDOCUMENT, 0, temporary);
        sci(SCI_SETTEXT, 0, reinterpret_cast<sptr_t>("class Example:\n    pass\n\n"));
        sci(SCI_SETFOLDLEVEL, 0, SC_FOLDLEVELBASE | SC_FOLDLEVELHEADERFLAG);
        sci(SCI_SETFOLDLEVEL, 1, SC_FOLDLEVELBASE + 1);
        sci(SCI_SETFOLDLEVEL, 2, SC_FOLDLEVELBASE);
        sci(SCI_FOLDLINE, 0, SC_FOLDACTION_CONTRACT);
        const bool hidden = !sci(SCI_GETLINEVISIBLE, 1) && !sci(SCI_GETFOLDEXPANDED, 0);
        sci(SCI_FOLDLINE, 0, SC_FOLDACTION_EXPAND);
        check("native_fold_expand_and_contract_still_hide_and_show_code",
            hidden && sci(SCI_GETLINEVISIBLE, 1) && sci(SCI_GETFOLDEXPANDED, 0));
        sci(SCI_SETDOCPOINTER, 0, original);
        sci(SCI_RELEASEDOCUMENT, 0, original);
        suppress = previousSuppression;

        bool switches = true;
        for (const bool light : {true, false}) {
            switches &= orbit::configureFoldIcons(editor, dpi, light) &&
                sci(SCI_MARKERSYMBOLDEFINED, SC_MARKNUM_FOLDEROPEN) == SC_MARK_RGBAIMAGE;
        }
        orbit::configureFoldIcons(editor, dpi, settings.light);
        check("theme_switch_reloads_the_correct_native_fold_marker_variant", switches);
    } catch (const std::exception& error) {
        checks.push_back({{"name", "unexpected_exception"}, {"passed", false}, {"error", error.what()}});
    }
    bool passed = true;
    for (const auto& result : checks) passed &= result["passed"].get<bool>();
    atomicWrite(report, json{{"passed", passed}, {"checks", checks.size()}, {"results", checks}, {"details", details}}.dump(2));
    return passed ? 0 : 1;
}
