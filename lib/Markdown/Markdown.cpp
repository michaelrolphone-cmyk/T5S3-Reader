#include "Markdown.h"

#include <cctype>

namespace Markdown {
namespace {

std::string_view ltrimView(std::string_view s) {
  size_t i = 0;
  while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) {
    ++i;
  }
  return s.substr(i);
}

std::string_view rtrimView(std::string_view s) {
  size_t n = s.size();
  while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r')) {
    --n;
  }
  return s.substr(0, n);
}

bool isFence(std::string_view trimmed) {
  return trimmed.size() >= 3 && (trimmed.substr(0, 3) == "```" || trimmed.substr(0, 3) == "~~~");
}

uint8_t headingLevelFrom(std::string_view trimmed) {
  size_t hashes = 0;
  while (hashes < trimmed.size() && hashes < 6 && trimmed[hashes] == '#') {
    ++hashes;
  }
  if (hashes == 0) {
    return 0;
  }
  if (hashes < trimmed.size() && trimmed[hashes] != ' ' && trimmed[hashes] != '\t') {
    return 0;
  }
  return static_cast<uint8_t>(hashes);
}

std::string headingTitle(std::string_view trimmed, uint8_t level) {
  std::string_view rest = trimmed.substr(level);
  rest = ltrimView(rest);
  rest = rtrimView(rest);
  while (!rest.empty() && rest.back() == '#') {
    rest.remove_suffix(1);
  }
  rest = rtrimView(rest);
  return std::string(rest);
}

bool isHorizontalRule(std::string_view trimmed) {
  if (trimmed.size() < 3) {
    return false;
  }
  char marker = 0;
  int count = 0;
  for (char c : trimmed) {
    if (c == ' ' || c == '\t') {
      continue;
    }
    if (c != '-' && c != '*' && c != '_') {
      return false;
    }
    if (marker == 0) {
      marker = c;
    } else if (c != marker) {
      return false;
    }
    ++count;
  }
  return count >= 3;
}

bool isListMarker(std::string_view trimmed, std::string& body) {
  if (trimmed.empty()) {
    return false;
  }
  if ((trimmed[0] == '-' || trimmed[0] == '*' || trimmed[0] == '+') &&
      trimmed.size() > 1 && (trimmed[1] == ' ' || trimmed[1] == '\t')) {
    body = std::string(ltrimView(trimmed.substr(2)));
    return true;
  }
  size_t i = 0;
  while (i < trimmed.size() && std::isdigit(static_cast<unsigned char>(trimmed[i]))) {
    ++i;
  }
  if (i > 0 && i + 1 < trimmed.size() && trimmed[i] == '.' && (trimmed[i + 1] == ' ' || trimmed[i + 1] == '\t')) {
    body = std::string(ltrimView(trimmed.substr(i + 2)));
    return true;
  }
  return false;
}

}  // namespace

uint8_t atxHeadingLevel(std::string_view raw) {
  std::string_view s = raw;
  size_t spaces = 0;
  while (spaces < s.size() && spaces < 4 && s[spaces] == ' ') {
    ++spaces;
  }
  if (spaces == 4) {
    return 0;
  }
  return headingLevelFrom(s.substr(spaces));
}

std::string stripInline(std::string_view raw) {
  std::string out;
  out.reserve(raw.size());
  const size_t n = raw.size();
  size_t i = 0;
  while (i < n) {
    const char c = raw[i];
    if (c == '\\' && i + 1 < n) {
      out.push_back(raw[i + 1]);
      i += 2;
      continue;
    }
    if (c == '`') {
      const size_t close = raw.find('`', i + 1);
      if (close != std::string_view::npos) {
        out.append(raw.substr(i + 1, close - i - 1));
        i = close + 1;
        continue;
      }
    }
    if (c == '*' || c == '_') {
      const char m = c;
      size_t run = 1;
      while (i + run < n && raw[i + run] == m && run < 3) {
        ++run;
      }
      const size_t close = raw.find(std::string(run, m), i + run);
      if (close != std::string_view::npos) {
        out.append(stripInline(raw.substr(i + run, close - (i + run))));
        i = close + run;
        continue;
      }
    }
    if (c == '[') {
      const size_t close = raw.find(']', i + 1);
      if (close != std::string_view::npos && close + 1 < n && raw[close + 1] == '(') {
        const size_t end = raw.find(')', close + 2);
        if (end != std::string_view::npos) {
          out.append(stripInline(raw.substr(i + 1, close - i - 1)));
          i = end + 1;
          continue;
        }
      }
    }
    if (c == '!' && i + 1 < n && raw[i + 1] == '[') {
      const size_t close = raw.find(']', i + 2);
      if (close != std::string_view::npos && close + 1 < n && raw[close + 1] == '(') {
        const size_t end = raw.find(')', close + 2);
        if (end != std::string_view::npos) {
          out.append(stripInline(raw.substr(i + 2, close - i - 2)));
          i = end + 1;
          continue;
        }
      }
    }
    out.push_back(c);
    ++i;
  }
  return out;
}

Line parseLine(std::string_view raw, bool& inFence) {
  Line line;
  std::string_view trimmedStart = ltrimView(rtrimView(raw));

  if (isFence(trimmedStart)) {
    inFence = !inFence;
    line.kind = BlockKind::Empty;
    return line;
  }

  if (inFence) {
    line.kind = BlockKind::Code;
    line.display = std::string(rtrimView(raw));
    return line;
  }

  if (trimmedStart.empty()) {
    line.kind = BlockKind::Empty;
    return line;
  }

  size_t indent = 0;
  while (indent < raw.size() && indent < 4 && raw[indent] == ' ') {
    ++indent;
  }
  std::string_view body = raw.substr(indent);
  body = rtrimView(body);

  const uint8_t level = headingLevelFrom(ltrimView(body));
  if (level > 0 && indent < 4) {
    line.kind = BlockKind::Heading;
    line.headingLevel = level;
    line.chapterBreak = (level == 1);
    line.display = stripInline(headingTitle(ltrimView(body), level));
    return line;
  }

  if (isHorizontalRule(trimmedStart)) {
    line.kind = BlockKind::HorizontalRule;
    line.display = "────────";
    return line;
  }

  if (!trimmedStart.empty() && trimmedStart[0] == '>') {
    std::string_view q = trimmedStart.substr(1);
    if (!q.empty() && (q[0] == ' ' || q[0] == '\t')) {
      q.remove_prefix(1);
    }
    line.kind = BlockKind::Quote;
    line.display = std::string("│ ") + stripInline(q);
    return line;
  }

  std::string listBody;
  if (isListMarker(trimmedStart, listBody)) {
    line.kind = BlockKind::ListItem;
    line.display = std::string("• ") + stripInline(listBody);
    return line;
  }

  line.kind = BlockKind::Paragraph;
  line.display = stripInline(rtrimView(raw));
  return line;
}

}  // namespace Markdown
