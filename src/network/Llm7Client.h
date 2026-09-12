#pragma once

#include <string>
#include <vector>

struct LlmChatMessage {
  const char* role;
  std::string content;
};

struct LlmChatResult {
  bool ok = false;
  std::string text;
  std::string error;
};

class Llm7Client {
 public:
  static LlmChatResult complete(const std::vector<LlmChatMessage>& messages);
};
