#pragma once
#include <string>

namespace UrlUtils {

/**
 * Check if URL uses HTTPS protocol
 */
bool isHttpsUrl(const std::string& url);

/**
 * Prepend http:// if no protocol specified (server will redirect to https if needed)
 */
std::string ensureProtocol(const std::string& url);

/**
 * Extract host with protocol from URL (e.g., "http://example.com" from "http://example.com/path")
 */
std::string extractHost(const std::string& url);

/**
 * Resolve a URI reference against a server/feed document URL (RFC 3986).
 * Relative paths use the containing directory unless the base ends in '/'.
 * Preserve query/fragment delimiters and escaped bytes; normalize path dots.
 * A server configured without a protocol retains the http:// default.
 */
std::string buildUrl(const std::string& serverUrl, const std::string& path);

}  // namespace UrlUtils
