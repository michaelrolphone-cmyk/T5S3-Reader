#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Markdown {

enum class BlockKind : uint8_t {
  Empty = 0,
  Paragraph,
  Heading,
  ListItem,
  Quote,
  Code,
  HorizontalRule,
};

struct Line {
  BlockKind kind = BlockKind::Paragraph;
  uint8_t headingLevel = 0;
  bool chapterBreak = false;
  std::string display;
};

struct Chapter {
  std::string title;
  size_t fileOffset = 0;
  int startPage = 0;
};

Line parseLine(std::string_view raw, bool& inFence);
uint8_t atxHeadingLevel(std::string_view raw);
std::string stripInline(std::string_view raw);

}  // namespace Markdown
