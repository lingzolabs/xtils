#include "xtils/net/http_multipart.h"

#include <algorithm>
#include <cctype>

namespace xtils {

namespace {

// Trim leading/trailing whitespace from a string_view.
std::string_view Trim(std::string_view sv) {
  while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front())))
    sv.remove_prefix(1);
  while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.back())))
    sv.remove_suffix(1);
  return sv;
}

// Case-insensitive string_view comparison.
bool IEquals(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) !=
        std::tolower(static_cast<unsigned char>(b[i])))
      return false;
  }
  return true;
}

// Find a substring in a string_view (not using std::search for clarity).
size_t FindSubstring(std::string_view haystack, std::string_view needle,
                     size_t start = 0) {
  if (needle.empty()) return start;
  if (start >= haystack.size()) return std::string_view::npos;
  auto pos = haystack.find(needle, start);
  return pos;
}

}  // namespace

MultipartParser::MultipartParser(std::string_view body,
                                 std::string_view boundary)
    : body_(body), boundary_(boundary) {
  delimiter_ = "--" + boundary_;
  close_delimiter_ = "--" + boundary_ + "--";
}

bool MultipartParser::Parse() {
  if (body_.empty() || boundary_.empty()) return false;

  // Find the first delimiter.
  size_t pos = FindSubstring(body_, delimiter_);
  if (pos == std::string_view::npos) return false;

  // Move past the first delimiter line.
  pos += delimiter_.size();
  // Skip the line ending after the delimiter (\r\n or \n).
  if (pos < body_.size() && body_[pos] == '\r') ++pos;
  if (pos < body_.size() && body_[pos] == '\n') ++pos;

  bool found_any = false;

  while (pos < body_.size()) {
    // Find the next delimiter.
    size_t next = FindSubstring(body_, delimiter_, pos);
    if (next == std::string_view::npos) break;

    // The part content is between pos and next, minus the preceding \r\n.
    size_t part_end = next;
    // The delimiter is preceded by \r\n (or \n).
    if (part_end >= 2 && body_[part_end - 2] == '\r' &&
        body_[part_end - 1] == '\n') {
      part_end -= 2;
    } else if (part_end >= 1 && body_[part_end - 1] == '\n') {
      part_end -= 1;
    }

    std::string_view part = body_.substr(pos, part_end - pos);
    if (!part.empty() || pos != part_end) {
      ParsePart(part);
      found_any = true;
    }

    // Move past this delimiter.
    pos = next + delimiter_.size();

    // Check if the text at `next` is the close delimiter.
    if (body_.substr(next, close_delimiter_.size()) == close_delimiter_) {
      break;
    }

    // Skip line ending after delimiter.
    if (pos < body_.size() && body_[pos] == '\r') ++pos;
    if (pos < body_.size() && body_[pos] == '\n') ++pos;
  }

  return found_any;
}

bool MultipartParser::ParsePart(std::string_view part) {
  // Split headers from body at the first \r\n\r\n or \n\n.
  size_t header_end = std::string_view::npos;
  size_t body_start = 0;

  size_t crlf_pos = FindSubstring(part, "\r\n\r\n");
  size_t lf_pos = FindSubstring(part, "\n\n");

  if (crlf_pos != std::string_view::npos && lf_pos != std::string_view::npos) {
    if (crlf_pos <= lf_pos) {
      header_end = crlf_pos;
      body_start = crlf_pos + 4;
    } else {
      header_end = lf_pos;
      body_start = lf_pos + 2;
    }
  } else if (crlf_pos != std::string_view::npos) {
    header_end = crlf_pos;
    body_start = crlf_pos + 4;
  } else if (lf_pos != std::string_view::npos) {
    header_end = lf_pos;
    body_start = lf_pos + 2;
  } else {
    // No header/body separator found. Treat entire part as body with no
    // headers. This is malformed but we handle it gracefully.
    return false;
  }

  std::string_view headers_section = part.substr(0, header_end);
  std::string_view body_section = part.substr(body_start);

  // Parse headers into a map.
  std::map<std::string, std::string> headers;
  size_t line_start = 0;
  while (line_start < headers_section.size()) {
    size_t line_end = headers_section.find('\n', line_start);
    if (line_end == std::string_view::npos) line_end = headers_section.size();

    std::string_view line = headers_section.substr(
        line_start, line_end - line_start);
    // Remove trailing \r.
    if (!line.empty() && line.back() == '\r') line.remove_suffix(1);

    size_t colon = line.find(':');
    if (colon != std::string_view::npos) {
      std::string_view name = Trim(line.substr(0, colon));
      std::string_view value = Trim(line.substr(colon + 1));
      // Store header name in lowercase for easy lookup.
      std::string lower_name(name);
      std::transform(lower_name.begin(), lower_name.end(),
                     lower_name.begin(), ::tolower);
      headers[lower_name] = std::string(value);
    }

    line_start = line_end + 1;
  }

  // Extract Content-Disposition.
  auto disp_it = headers.find("content-disposition");
  if (disp_it == headers.end()) return false;

  std::string name, filename;
  if (!ParseContentDisposition(disp_it->second, name, filename))
    return false;

  if (!filename.empty()) {
    // This is a file upload.
    MultipartFormFile file;
    file.field_name = name;
    file.filename = filename;

    auto ct_it = headers.find("content-type");
    file.content_type =
        (ct_it != headers.end()) ? ct_it->second : "application/octet-stream";
    file.content = std::string(body_section);
    files_.push_back(std::move(file));
  } else {
    // This is a form field.
    MultipartFormField field;
    field.name = name;
    field.value = std::string(body_section);
    fields_.push_back(std::move(field));
  }

  return true;
}

bool MultipartParser::ParseContentDisposition(std::string_view value,
                                              std::string& name,
                                              std::string& filename) {
  name = ExtractParam(value, "name");
  filename = ExtractParam(value, "filename");
  return !name.empty();
}

std::string MultipartParser::ExtractParam(std::string_view header_value,
                                          std::string_view param_name) {
  // Search for param_name= (case-insensitive for the param name).
  std::string search_lower;
  search_lower.reserve(param_name.size() + 1);
  for (char c : param_name)
    search_lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  search_lower += '=';

  // Make a lowercase copy of header_value for searching.
  std::string lower_value(header_value);
  std::transform(lower_value.begin(), lower_value.end(),
                 lower_value.begin(), ::tolower);

  size_t pos = lower_value.find(search_lower);
  if (pos == std::string::npos) return "";

  // Use the original header_value to extract the actual value.
  size_t value_start = pos + search_lower.size();
  if (value_start >= header_value.size()) return "";

  if (header_value[value_start] == '"') {
    // Quoted value: find the closing quote.
    ++value_start;
    size_t quote_end = header_value.find('"', value_start);
    if (quote_end == std::string_view::npos) {
      // No closing quote, take everything.
      return std::string(header_value.substr(value_start));
    }
    return std::string(header_value.substr(value_start,
                                           quote_end - value_start));
  } else {
    // Unquoted value: take until semicolon or end.
    size_t end = header_value.find(';', value_start);
    if (end == std::string_view::npos) end = header_value.size();
    std::string_view result = Trim(header_value.substr(
        value_start, end - value_start));
    return std::string(result);
  }
}

std::string MultipartParser::ExtractBoundary(std::string_view content_type) {
  // Look for "boundary=" in the Content-Type header.
  std::string lower_ct(content_type);
  std::transform(lower_ct.begin(), lower_ct.end(), lower_ct.begin(),
                 ::tolower);

  size_t pos = lower_ct.find("boundary=");
  if (pos == std::string::npos) return "";

  size_t value_start = pos + 9;  // strlen("boundary=")
  if (value_start >= content_type.size()) return "";

  // Use original content_type for the value.
  if (content_type[value_start] == '"') {
    // Quoted boundary.
    ++value_start;
    size_t quote_end = content_type.find('"', value_start);
    if (quote_end == std::string_view::npos) {
      return std::string(Trim(content_type.substr(value_start)));
    }
    return std::string(content_type.substr(value_start,
                                           quote_end - value_start));
  } else {
    // Unquoted: take until semicolon, space, or end.
    size_t end = content_type.find_first_of("; \t", value_start);
    if (end == std::string_view::npos) end = content_type.size();
    return std::string(Trim(content_type.substr(value_start,
                                                end - value_start)));
  }
}

}  // namespace xtils
