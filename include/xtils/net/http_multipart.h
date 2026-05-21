#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace xtils {

// A parsed form field from multipart/form-data.
struct MultipartFormField {
  std::string name;
  std::string value;
};

// A parsed file upload from multipart/form-data.
struct MultipartFormFile {
  std::string field_name;     // The form field name.
  std::string filename;       // The original filename.
  std::string content_type;   // MIME type (e.g., "image/png").
  std::string content;        // Binary-safe file content in memory.
};

// Parses a multipart/form-data body from an in-memory buffer.
//
// Usage:
//   std::string boundary = MultipartParser::ExtractBoundary(content_type);
//   MultipartParser parser(body, boundary);
//   if (parser.Parse()) {
//     for (auto& field : parser.GetFields()) { ... }
//     for (auto& file : parser.GetFiles()) { ... }
//   }
class MultipartParser {
 public:
  // Construct with the full body and the boundary string.
  // The boundary should NOT include the leading "--" prefix.
  MultipartParser(std::string_view body, std::string_view boundary);

  // Run the parse. Returns true if at least one part was found.
  bool Parse();

  // Access parsed results (valid after successful Parse()).
  const std::vector<MultipartFormField>& GetFields() const { return fields_; }
  const std::vector<MultipartFormFile>& GetFiles() const { return files_; }

  // Extract the boundary value from a Content-Type header.
  // E.g., "multipart/form-data; boundary=----WebKitFormBoundary..."
  //       → "----WebKitFormBoundary..."
  // Returns empty string on failure.
  static std::string ExtractBoundary(std::string_view content_type);

 private:
  // Parse a single part between two boundary delimiters.
  bool ParsePart(std::string_view part);

  // Parse Content-Disposition header value to extract name and filename.
  // Returns true if "name" was found.
  static bool ParseContentDisposition(std::string_view value,
                                      std::string& name,
                                      std::string& filename);

  // Extract a parameter value from a header value string.
  // E.g., ExtractParam("form-data; name=\"field1\"", "name") → "field1"
  static std::string ExtractParam(std::string_view header_value,
                                  std::string_view param_name);

  std::string_view body_;
  std::string boundary_;
  std::string delimiter_;        // "--" + boundary
  std::string close_delimiter_;  // "--" + boundary + "--"

  std::vector<MultipartFormField> fields_;
  std::vector<MultipartFormFile> files_;
};

}  // namespace xtils
