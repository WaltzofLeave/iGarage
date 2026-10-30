#include "../core.h"
#include "icons.h"

#include <shlwapi.h>
#include <array>
#include <cstdio>
#include <regex>

namespace orbit {
namespace {

struct IconSpec {
    const char* asset;
    bool tintable = true;
    float width = 16, height = 16;
};

constexpr std::array specs{
    IconSpec{"folder"}, IconSpec{"search"}, IconSpec{"branch"}, IconSpec{"play"},
    IconSpec{"shell"}, IconSpec{"settings"}, IconSpec{"chevron-right"},
    IconSpec{"chevron-down"}, IconSpec{"close"}, IconSpec{"plus"}, IconSpec{"more"},
    IconSpec{"file"}, IconSpec{"check"}, IconSpec{"stop"}, IconSpec{"refresh"},
    IconSpec{"layout"}, IconSpec{"structure"}, IconSpec{"download"}, IconSpec{"upload"},
    IconSpec{"help"}, IconSpec{"maximize"}, IconSpec{"trash"}, IconSpec{"flask", false},
    IconSpec{"clock"}, IconSpec{"book"}, IconSpec{"sun"}, IconSpec{"save"},
    IconSpec{"minimize"}, IconSpec{"restore"}, IconSpec{"project"},
    IconSpec{"folder-source", false}, IconSpec{"library"}, IconSpec{"class", false},
    IconSpec{"function", false}, IconSpec{"method", false}, IconSpec{"markdown", false},
    // The original PSF SVG uses points and has no viewBox. Preserve its full canvas.
    IconSpec{"python", false, 111.161356f, 134.668107f},
    IconSpec{"markdown", false}, IconSpec{"toml", false}, IconSpec{"json", false},
    IconSpec{"javascript", false}, IconSpec{"typescript", false}, IconSpec{"text", false},
    IconSpec{"config", false}, IconSpec{"gitignore", false}, IconSpec{"java", false},
    IconSpec{"html", false}, IconSpec{"css", false}, IconSpec{"yaml", false},
    IconSpec{"xml", false}, IconSpec{"csv", false}, IconSpec{"shell", false},
    IconSpec{"sql", false}, IconSpec{"fold-open", false, 9, 9},
    IconSpec{"fold-closed", false, 9, 9}
};
static_assert(specs.size() == static_cast<std::size_t>(Icon::Count));
constexpr std::size_t bitmapLimit = 256;

std::string tinted(std::string source, unsigned tint) {
    // Change only the primary ink, preserving the supplied background and accents.
    // SVG sources on disk and their embedded license headers remain unmodified.
    static const std::regex stroke(R"re(stroke="(#[0-9a-fA-F]{6})")re");
    static const std::regex colour(R"(#[0-9a-fA-F]{6})");
    std::smatch match;
    if (!std::regex_search(source, match, stroke) && !std::regex_search(source, match, colour)) return source;
    const auto ink = match[1].matched ? match[1].str() : match[0].str();
    char replacement[8]{};
    std::snprintf(replacement, sizeof(replacement), "#%06X", tint & 0xffffff);
    std::size_t offset = 0;
    while ((offset = source.find(ink, offset)) != source.npos) {
        source.replace(offset, ink.size(), replacement);
        offset += 7;
    }
    return source;
}

void trademarkIndicator(ID2D1DeviceContext* context, float extent, bool light) {
    // Adjacent TM, outside the unaltered official logo artwork.
    ComPtr<ID2D1SolidColorBrush> brush;
    context->CreateSolidColorBrush(color(light ? 0x41454c : 0xc7c8cc), &brush);
    if (!brush) return;
    const float unit = extent / 16;
    const auto line = [&](float x, float y, float xx, float yy) {
        context->DrawLine(D2D1::Point2F(x * unit, y * unit),
            D2D1::Point2F(xx * unit, yy * unit), brush.Get(), .4f * unit);
    };
    line(12.5f, 1, 14, 1); line(13.25f, 1, 13.25f, 3);
    line(14.5f, 3, 14.5f, 1); line(14.5f, 1, 15, 2);
    line(15, 2, 15.5f, 1); line(15.5f, 1, 15.5f, 3);
}

} // namespace

std::string_view iconAssetName(Icon icon) { return specs.at(static_cast<std::size_t>(icon)).asset; }

bool IconAtlas::initialize(ID2D1Device* device, fs::path directory) {
    ComPtr<ID2D1DeviceContext> base;
    if (!device || FAILED(device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &base)) ||
        FAILED(base.As(&context_))) return false;
    directory_ = std::move(directory);
    return true;
}

ID2D1Bitmap1* IconAtlas::bitmap(Icon icon, float size, float dpi, bool light, unsigned tint) {
    if (!context_ || icon >= Icon::Count) return nullptr;
    const auto& spec = specs[static_cast<std::size_t>(icon)];
    const int extent = std::clamp(static_cast<int>(std::lround(size * dpi / 96)), 1, 128);
    if (!spec.tintable) tint = 0;
    const Key key{icon, extent, light, tint};
    if (const auto found = bitmaps_.find(key); found != bitmaps_.end()) {
        ++cacheHits_;
        return found->second.Get();
    }
    const auto sourceKey = std::make_pair(std::string(spec.asset), light);
    auto source = sources_.find(sourceKey);
    if (source == sources_.end()) {
        auto filename = directory_ / (std::string(spec.asset) + (light || icon == Icon::Python ? ".svg" : "_dark.svg"));
        try {
            source = sources_.emplace(sourceKey, readFile(filename)).first;
            ++sourceLoads_;
        } catch (...) { return nullptr; }
    }
    const auto xml = tint ? tinted(source->second, tint) : source->second;
    ComPtr<IStream> stream;
    stream.Attach(SHCreateMemStream(reinterpret_cast<const BYTE*>(xml.data()), static_cast<UINT>(xml.size())));
    ComPtr<ID2D1SvgDocument> svg;
    if (!stream || FAILED(context_->CreateSvgDocument(stream.Get(), D2D1::SizeF(spec.width, spec.height), &svg))) return nullptr;

    ComPtr<ID2D1Bitmap1> bitmap;
    const auto properties = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    if (FAILED(context_->CreateBitmap(D2D1::SizeU(extent, extent), nullptr, 0, properties, &bitmap))) return nullptr;
    context_->SetTarget(bitmap.Get());
    context_->SetDpi(96, 96);
    context_->BeginDraw();
    context_->Clear(D2D1::ColorF(0, 0.f));
    const float scale = extent / std::max(spec.width, spec.height);
    context_->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale) *
        D2D1::Matrix3x2F::Translation((extent - spec.width * scale) / 2, (extent - spec.height * scale) / 2));
    context_->DrawSvgDocument(svg.Get());
    context_->SetTransform(D2D1::Matrix3x2F::Identity());
    if (icon == Icon::Python) trademarkIndicator(context_.Get(), static_cast<float>(extent), light);
    const auto result = context_->EndDraw();
    context_->SetTarget(nullptr);
    if (FAILED(result)) return nullptr;
    ++rasterizations_;
    if (bitmaps_.size() >= bitmapLimit) bitmaps_.clear();
    return bitmaps_.emplace(key, std::move(bitmap)).first->second.Get();
}

const IconPixels& IconAtlas::pixels(Icon icon, float size, float dpi, bool light) {
    const int extent = std::clamp(static_cast<int>(std::lround(size * dpi / 96)), 1, 128);
    const Key key{icon, extent, light, 0};
    if (const auto found = pixels_.find(key); found != pixels_.end()) return found->second;
    IconPixels result;
    auto* source = bitmap(icon, size, dpi, light);
    if (!source) throw std::runtime_error("Cannot render icon: " + std::string(iconAssetName(icon)));
    const auto properties = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    ComPtr<ID2D1Bitmap1> readable;
    if (FAILED(context_->CreateBitmap(source->GetPixelSize(), nullptr, 0, properties, &readable)) ||
        FAILED(readable->CopyFromBitmap(nullptr, source, nullptr))) throw std::runtime_error("Cannot read icon pixels");
    D2D1_MAPPED_RECT mapped{};
    if (FAILED(readable->Map(D2D1_MAP_OPTIONS_READ, &mapped))) throw std::runtime_error("Cannot map icon pixels");
    result.size = extent;
    result.rgba.resize(static_cast<std::size_t>(extent) * extent * 4);
    for (int y = 0; y < extent; ++y) {
        const auto* input = mapped.bits + y * mapped.pitch;
        auto* output = result.rgba.data() + static_cast<std::size_t>(y) * extent * 4;
        for (int x = 0; x < extent; ++x) {
            const unsigned alpha = input[x * 4 + 3];
            for (int channel = 0; channel < 3; ++channel)
                output[x * 4 + channel] = alpha ? static_cast<unsigned char>(std::min(255u, input[x * 4 + 2 - channel] * 255u / alpha)) : 0;
            output[x * 4 + 3] = static_cast<unsigned char>(alpha);
        }
    }
    readable->Unmap();
    // Only fold markers require CPU pixels during normal use. Bound unusual DPI churn.
    if (pixels_.size() >= 256) pixels_.clear();
    return pixels_.emplace(key, std::move(result)).first->second;
}

IconCacheStats IconAtlas::stats() const { return {sourceLoads_, rasterizations_, cacheHits_, bitmaps_.size()}; }

Icon fileIcon(std::string_view path) {
    const auto name = lower(baseName(std::string(path)));
    const auto type = extension(name);
    if (name == ".gitignore" || name == ".gitattributes") return Icon::GitIgnore;
    if (name == ".editorconfig" || name == ".env" || name.ends_with(".ini") || name.ends_with(".cfg")) return Icon::Config;
    if (type == "py" || type == "pyi" || type == "pyw") return Icon::Python;
    if (type == "md" || type == "markdown") return Icon::Markdown;
    if (type == "toml") return Icon::Toml;
    if (type == "json" || type == "jsonc") return Icon::Json;
    if (type == "js" || type == "jsx" || type == "mjs" || type == "cjs") return Icon::JavaScript;
    if (type == "ts" || type == "tsx") return Icon::TypeScript;
    if (type == "txt" || type == "log") return Icon::Text;
    if (type == "java") return Icon::Java;
    if (type == "html" || type == "htm") return Icon::Html;
    if (type == "css" || type == "scss") return Icon::Css;
    if (type == "yaml" || type == "yml") return Icon::Yaml;
    if (type == "xml" || type == "svg") return Icon::Xml;
    if (type == "csv" || type == "tsv") return Icon::Csv;
    if (type == "sh" || type == "bat" || type == "cmd" || type == "ps1") return Icon::Shell;
    if (type == "sql") return Icon::Sql;
    return Icon::File;
}

Icon symbolIcon(char kind, int depth) {
    return kind == 'C' ? Icon::Class : kind == 'f' ? (depth ? Icon::Method : Icon::Function) : Icon::Heading;
}

bool configureFoldIcons(HWND editor, float dpi, bool light) {
    const auto atlas = systemIcons();
    if (!atlas) return false;
    try {
        for (const auto& [marker, icon] : {std::pair{SC_MARKNUM_FOLDEROPEN, Icon::FoldOpen},
                                         std::pair{SC_MARKNUM_FOLDER, Icon::FoldClosed}}) {
            const auto& image = atlas->pixels(icon, 9, dpi, light);
            SendMessageW(editor, SCI_RGBAIMAGESETWIDTH, image.size, 0);
            SendMessageW(editor, SCI_RGBAIMAGESETHEIGHT, image.size, 0);
            // Win32 Scintilla's margin coordinates are already physical pixels.
            SendMessageW(editor, SCI_RGBAIMAGESETSCALE, 100, 0);
            SendMessageW(editor, SCI_MARKERDEFINERGBAIMAGE, marker, reinterpret_cast<LPARAM>(image.rgba.data()));
        }
        return true;
    } catch (...) { return false; }
}

} // namespace orbit
