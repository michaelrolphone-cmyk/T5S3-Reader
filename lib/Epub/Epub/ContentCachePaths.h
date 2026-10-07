#pragma once

// Derived artifacts from the old last-rootfile selection must never be reused
// after selecting the default rendition. Version only these disposable names;
// keep the book directory, progress.bin and unknown user files unchanged.
namespace EpubContentCache {
inline constexpr char sections[] = "/rendition_v1_sections";
inline constexpr char imagePrefix[] = "/rendition_v1_img_";
inline constexpr char coverPrefix[] = "/rendition_v1_cover";
inline constexpr char thumbPrefix[] = "/rendition_v1_thumb_";
inline constexpr char cssRules[] = "/rendition_v1_css_rules.cache";
}  // namespace EpubContentCache
