#pragma once
struct ReaderSettings {
    int font=12; float spacing=1.0f;
    int getReaderFontId() const { return font; }
    float getReaderLineCompression() const { return spacing; }
};
inline ReaderSettings SETTINGS;
