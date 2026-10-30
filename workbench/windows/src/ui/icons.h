#pragma once

#include <d2d1_3.h>
#include <wrl/client.h>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

enum class Icon {
    Folder, Search, Branch, Play, Terminal, Settings, Chevron, Down, Close, Plus,
    More, File, Check, Stop, Refresh, Layout, Structure, Download, Upload, Help,
    Maximize, Trash, Flask, Clock, Book, Sun, Save, Minimize, Restore,
    Project, FolderSource, Library, Class, Function, Method, Heading,
    Python, Markdown, Toml, Json, JavaScript, TypeScript, Text, Config, GitIgnore,
    Java, Html, Css, Yaml, Xml, Csv, Shell, Sql, FoldOpen, FoldClosed, Count
};

namespace orbit {

struct IconPixels {
    int size = 0;
    std::vector<unsigned char> rgba;
};

struct IconCacheStats {
    std::size_t sourceLoads = 0;
    std::size_t rasterizations = 0;
    std::size_t cacheHits = 0;
    std::size_t bitmaps = 0;
};

// One atlas per Direct2D device, shared by the workbench and its popups.
// SVG files are read once; warmed draws only use cached GPU bitmaps.
class IconAtlas {
public:
    bool initialize(ID2D1Device* device, std::filesystem::path directory);
    ID2D1Bitmap1* bitmap(Icon icon, float size, float dpi, bool light, unsigned tint = 0);
    const IconPixels& pixels(Icon icon, float size, float dpi, bool light);
    IconCacheStats stats() const;

private:
    using Key = std::tuple<Icon, int, bool, unsigned>;
    Microsoft::WRL::ComPtr<ID2D1DeviceContext5> context_;
    std::filesystem::path directory_;
    std::map<std::pair<std::string, bool>, std::string> sources_;
    std::map<Key, Microsoft::WRL::ComPtr<ID2D1Bitmap1>> bitmaps_;
    std::map<Key, IconPixels> pixels_;
    std::size_t sourceLoads_ = 0, rasterizations_ = 0, cacheHits_ = 0;
};

std::string_view iconAssetName(Icon icon);
Icon fileIcon(std::string_view path);
Icon symbolIcon(char kind, int depth = 0);
std::shared_ptr<IconAtlas> systemIcons();
bool configureFoldIcons(HWND editor, float dpi, bool light);

} // namespace orbit
