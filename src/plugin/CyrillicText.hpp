// SPDX-License-Identifier: MIT
// Russian text for the mod's own ImGui panels (the power wheel, the calibration, the squad panel): the VR wrapper
// is translated, so the faces it draws in need the Cyrillic letters the Latin fonts lack. Exo 2 (SIL OFL,
// third_party/fonts, deployed to ASI\fonts) carries them, in the weight of the face it is merged into.
#pragma once

#include "imgui.h"

#include <filesystem>
#include <string>

namespace mevr::host::cyr {

inline std::string Utf8(std::filesystem::path const& path) {
    std::u8string const wide = path.u8string();
    return std::string(reinterpret_cast<char const*>(wide.data()), wide.size());
}

// The Exo 2 file of the same weight as a Latin face's file name (Rajdhani-Bold -> Exo2-Bold), else Regular.
inline std::filesystem::path Exo2For(std::filesystem::path const& latinFile) {
    std::wstring const name = latinFile.stem().wstring();
    if (name.find(L"SemiBold") != std::wstring::npos) {
        return L"Exo2-SemiBold.ttf";
    }
    if (name.find(L"Bold") != std::wstring::npos) {
        return L"Exo2-Bold.ttf";
    }
    return L"Exo2-Regular.ttf";
}

// Latin plus Cyrillic: the ranges of a whole face that has both (the game's own fonts, Windows').
inline const ImWchar* LatinCyrillic(ImGuiIO& io) {
    static ImVector<ImWchar> ranges;
    if (ranges.empty()) {
        ImFontGlyphRangesBuilder builder;
        builder.AddRanges(io.Fonts->GetGlyphRangesDefault());
        builder.AddRanges(io.Fonts->GetGlyphRangesCyrillic());
        builder.BuildRanges(&ranges);
    }
    return ranges.Data;
}

// Adds a file's Cyrillic letters to the face just added (ImGui merge mode: the atlas' last font is the target).
// False when the file is not there.
inline bool MergeCyrillicFace(ImGuiIO& io, std::filesystem::path const& file, float sizePx) {
    std::error_code ec;
    if (!std::filesystem::exists(file, ec)) {
        return false;
    }
    ImFontConfig config;
    config.MergeMode = true;
    config.GlyphRanges = io.Fonts->GetGlyphRangesCyrillic();
    return io.Fonts->AddFontFromFileTTF(Utf8(file).c_str(), sizePx, &config) != nullptr;
}

// Adds Exo 2's Cyrillic letters to the face just added, so a Latin face renders Russian rather than boxes. False
// when the font is not next to the Latin one.
inline bool MergeCyrillic(ImGuiIO& io, std::filesystem::path const& dir, std::filesystem::path const& latinFile, float sizePx) {
    return MergeCyrillicFace(io, dir / Exo2For(latinFile), sizePx);
}

// A Latin face with Latin+Cyrillic ranges, with Exo 2's Cyrillic merged in: the one call the panels' fonts take.
inline ImFont* AddFace(ImGuiIO& io, std::filesystem::path const& file, float sizePx) {
    ImFontConfig config;
    config.GlyphRanges = LatinCyrillic(io);
    ImFont* const font = io.Fonts->AddFontFromFileTTF(Utf8(file).c_str(), sizePx, &config);
    if (font != nullptr) {
        MergeCyrillic(io, file.parent_path(), file.filename(), sizePx);
    }
    return font;
}

}  // namespace mevr::host::cyr
