#include "UrlUtils.h"

#include <string_view>

namespace {

struct UriReference {
  std::string_view scheme, authority, path, query, fragment;
  bool hasAuthority = false;
  bool hasQuery = false;
  bool hasFragment = false;
};

bool isScheme(std::string_view value) {
  const auto isAlpha = [](char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); };
  if (value.empty() || !isAlpha(value.front())) return false;
  for (char c : value) {
    if (!isAlpha(c) && !(c >= '0' && c <= '9') && c != '+' && c != '-' && c != '.') return false;
  }
  return true;
}

UriReference parseReference(std::string_view value) {
  UriReference result;
  const auto fragment = value.find('#');
  if (fragment != std::string_view::npos) {
    result.hasFragment = true;
    result.fragment = value.substr(fragment + 1);
    value = value.substr(0, fragment);
  }
  const auto query = value.find('?');
  if (query != std::string_view::npos) {
    result.hasQuery = true;
    result.query = value.substr(query + 1);
    value = value.substr(0, query);
  }
  const auto colon = value.find(':');
  if (colon != std::string_view::npos && isScheme(value.substr(0, colon))) {
    result.scheme = value.substr(0, colon);
    value.remove_prefix(colon + 1);
  }
  if (value.substr(0, 2) == "//") {
    result.hasAuthority = true;
    value.remove_prefix(2);
    const auto slash = value.find('/');
    result.authority = value.substr(0, slash);
    value = slash == std::string_view::npos ? std::string_view{} : value.substr(slash);
  }
  result.path = value;
  return result;
}

// RFC 3986 section 5.2.4. Consume the input once; never decode escaped separators,
// collapse repeated slashes, or treat query/fragment text as path segments.
std::string removeDotSegments(std::string_view input) {
  std::string output;
  output.reserve(input.size());
  while (!input.empty()) {
    if (input.substr(0, 3) == "../") {
      input.remove_prefix(3);
    } else if (input.substr(0, 2) == "./") {
      input.remove_prefix(2);
    } else if (input.substr(0, 3) == "/./") {
      input.remove_prefix(2);
    } else if (input == "/.") {
      output += '/';
      break;
    } else if (input.substr(0, 4) == "/../" || input == "/..") {
      const auto slash = output.rfind('/');
      output.resize(slash == std::string::npos ? 0 : slash);
      if (input == "/..") {
        output += '/';
        break;
      }
      input.remove_prefix(3);
    } else if (input == "." || input == "..") {
      break;
    } else {
      const auto slash = input.find('/', input.front() == '/' ? 1 : 0);
      const auto count = slash == std::string_view::npos ? input.size() : slash;
      output.append(input.data(), count);
      input.remove_prefix(count);
    }
  }
  return output;
}

}  // namespace

namespace UrlUtils {

bool isHttpsUrl(const std::string& url) { return url.rfind("https://", 0) == 0; }

std::string ensureProtocol(const std::string& url) {
  if (url.find("://") == std::string::npos) {
    return "http://" + url;
  }
  return url;
}

std::string extractHost(const std::string& url) {
  const size_t protocolEnd = url.find("://");
  if (protocolEnd == std::string::npos) {
    // No protocol, find first slash
    const size_t firstSlash = url.find('/');
    return firstSlash == std::string::npos ? url : url.substr(0, firstSlash);
  }
  // Find the first slash after the protocol
  const size_t hostStart = protocolEnd + 3;
  const size_t pathStart = url.find('/', hostStart);
  return pathStart == std::string::npos ? url : url.substr(0, pathStart);
}

std::string buildUrl(const std::string& serverUrl, const std::string& path) {
  // Keep bare-host server configuration support while resolving feed references
  // against a document URI, per RFC 3986 section 5.2.
  const std::string urlWithProtocol = ensureProtocol(serverUrl);
  const auto base = parseReference(urlWithProtocol);
  auto target = parseReference(path);
  std::string mergedPath;
  bool normalizePath = true;
  if (target.scheme.empty()) {
    target.scheme = base.scheme;
    if (!target.hasAuthority) {
      target.hasAuthority = base.hasAuthority;
      target.authority = base.authority;
      if (target.path.empty()) {
        target.path = base.path;
        normalizePath = false;
        if (!target.hasQuery) {
          target.hasQuery = base.hasQuery;
          target.query = base.query;
        }
      } else if (target.path.front() != '/') {
        const auto slash = base.path.rfind('/');
        if (base.hasAuthority && base.path.empty()) {
          mergedPath = "/";
        } else if (slash != std::string_view::npos) {
          mergedPath = std::string(base.path.substr(0, slash + 1));
        }
        mergedPath.append(target.path.data(), target.path.size());
        target.path = mergedPath;
      }
    }
  }

  std::string result;
  if (!target.scheme.empty()) result = std::string(target.scheme) + ':';
  if (target.hasAuthority) result += "//" + std::string(target.authority);
  result += normalizePath ? removeDotSegments(target.path) : std::string(target.path);
  if (target.hasQuery) result += '?' + std::string(target.query);
  if (target.hasFragment) result += '#' + std::string(target.fragment);
  return result;
}

}  // namespace UrlUtils
