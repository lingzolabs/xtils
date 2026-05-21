#include "xtils/net/http_multipart.h"

#include <string>
#include <vector>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

using namespace xtils;

// Helper: build a multipart body.
static std::string BuildMultipartBody(
    const std::string& boundary,
    const std::vector<std::pair<std::string, std::string>>& fields,
    const std::vector<
        std::tuple<std::string, std::string, std::string, std::string>>&
        files) {
  std::string body;
  for (const auto& [name, value] : fields) {
    body += "--" + boundary + "\r\n";
    body += "Content-Disposition: form-data; name=\"" + name + "\"\r\n";
    body += "\r\n";
    body += value + "\r\n";
  }
  for (const auto& [field_name, filename, content_type, content] : files) {
    body += "--" + boundary + "\r\n";
    body += "Content-Disposition: form-data; name=\"" + field_name +
            "\"; filename=\"" + filename + "\"\r\n";
    body += "Content-Type: " + content_type + "\r\n";
    body += "\r\n";
    body += content + "\r\n";
  }
  body += "--" + boundary + "--\r\n";
  return body;
}

// --- ExtractBoundary tests ---

TEST_CASE("ExtractBoundary: standard Content-Type") {
  std::string ct =
      "multipart/form-data; boundary=----WebKitFormBoundaryABC123";
  std::string boundary = MultipartParser::ExtractBoundary(ct);
  CHECK(boundary == "----WebKitFormBoundaryABC123");
}

TEST_CASE("ExtractBoundary: quoted boundary") {
  std::string ct =
      "multipart/form-data; boundary=\"----WebKitFormBoundaryXYZ\"";
  std::string boundary = MultipartParser::ExtractBoundary(ct);
  CHECK(boundary == "----WebKitFormBoundaryXYZ");
}

TEST_CASE("ExtractBoundary: no boundary param") {
  std::string ct = "multipart/form-data";
  std::string boundary = MultipartParser::ExtractBoundary(ct);
  CHECK(boundary.empty());
}

TEST_CASE("ExtractBoundary: empty string") {
  CHECK(MultipartParser::ExtractBoundary("").empty());
}

TEST_CASE("ExtractBoundary: case insensitive") {
  std::string ct = "multipart/form-data; BOUNDARY=myboundary";
  std::string boundary = MultipartParser::ExtractBoundary(ct);
  CHECK(boundary == "myboundary");
}

TEST_CASE("ExtractBoundary: boundary with extra params after") {
  std::string ct =
      "multipart/form-data; boundary=abc123; charset=utf-8";
  std::string boundary = MultipartParser::ExtractBoundary(ct);
  CHECK(boundary == "abc123");
}

// --- Single form field ---

TEST_CASE("Parse: single form field") {
  std::string boundary = "boundary123";
  std::string body = BuildMultipartBody(boundary, {{"name", "John"}}, {});

  MultipartParser parser(body, boundary);
  REQUIRE(parser.Parse());
  REQUIRE(parser.GetFields().size() == 1);
  CHECK(parser.GetFields()[0].name == "name");
  CHECK(parser.GetFields()[0].value == "John");
  CHECK(parser.GetFiles().empty());
}

// --- Multiple form fields ---

TEST_CASE("Parse: multiple form fields") {
  std::string boundary = "---bound---";
  std::string body = BuildMultipartBody(
      boundary,
      {{"first", "Alice"}, {"last", "Smith"}, {"age", "30"}},
      {});

  MultipartParser parser(body, boundary);
  REQUIRE(parser.Parse());
  REQUIRE(parser.GetFields().size() == 3);
  CHECK(parser.GetFields()[0].name == "first");
  CHECK(parser.GetFields()[0].value == "Alice");
  CHECK(parser.GetFields()[1].name == "last");
  CHECK(parser.GetFields()[1].value == "Smith");
  CHECK(parser.GetFields()[2].name == "age");
  CHECK(parser.GetFields()[2].value == "30");
}

// --- Single file upload ---

TEST_CASE("Parse: single file upload") {
  std::string boundary = "fileboundary";
  std::string file_content = "Hello, this is file content!";
  std::string body = BuildMultipartBody(
      boundary, {},
      {{"upload", "test.txt", "text/plain", file_content}});

  MultipartParser parser(body, boundary);
  REQUIRE(parser.Parse());
  CHECK(parser.GetFields().empty());
  REQUIRE(parser.GetFiles().size() == 1);
  CHECK(parser.GetFiles()[0].field_name == "upload");
  CHECK(parser.GetFiles()[0].filename == "test.txt");
  CHECK(parser.GetFiles()[0].content_type == "text/plain");
  CHECK(parser.GetFiles()[0].content == file_content);
}

// --- Multiple files ---

TEST_CASE("Parse: multiple file uploads") {
  std::string boundary = "multifilebnd";
  std::string body = BuildMultipartBody(
      boundary, {},
      {{"file1", "a.png", "image/png", "PNG_DATA_HERE"},
       {"file2", "b.jpg", "image/jpeg", "JPEG_DATA_HERE"}});

  MultipartParser parser(body, boundary);
  REQUIRE(parser.Parse());
  REQUIRE(parser.GetFiles().size() == 2);
  CHECK(parser.GetFiles()[0].field_name == "file1");
  CHECK(parser.GetFiles()[0].filename == "a.png");
  CHECK(parser.GetFiles()[0].content_type == "image/png");
  CHECK(parser.GetFiles()[0].content == "PNG_DATA_HERE");
  CHECK(parser.GetFiles()[1].field_name == "file2");
  CHECK(parser.GetFiles()[1].filename == "b.jpg");
  CHECK(parser.GetFiles()[1].content_type == "image/jpeg");
  CHECK(parser.GetFiles()[1].content == "JPEG_DATA_HERE");
}

// --- Mixed fields and files ---

TEST_CASE("Parse: mixed fields and files") {
  std::string boundary = "mixedbnd";
  std::string body = BuildMultipartBody(
      boundary,
      {{"username", "admin"}, {"description", "My avatar"}},
      {{"avatar", "photo.jpg", "image/jpeg", "\xFF\xD8\xFF\xE0"}});

  MultipartParser parser(body, boundary);
  REQUIRE(parser.Parse());
  REQUIRE(parser.GetFields().size() == 2);
  CHECK(parser.GetFields()[0].name == "username");
  CHECK(parser.GetFields()[0].value == "admin");
  CHECK(parser.GetFields()[1].name == "description");
  CHECK(parser.GetFields()[1].value == "My avatar");
  REQUIRE(parser.GetFiles().size() == 1);
  CHECK(parser.GetFiles()[0].field_name == "avatar");
  CHECK(parser.GetFiles()[0].filename == "photo.jpg");
}

// --- Empty field value ---

TEST_CASE("Parse: empty field value") {
  std::string boundary = "emptybnd";
  std::string body = BuildMultipartBody(boundary, {{"empty", ""}}, {});

  MultipartParser parser(body, boundary);
  REQUIRE(parser.Parse());
  REQUIRE(parser.GetFields().size() == 1);
  CHECK(parser.GetFields()[0].name == "empty");
  CHECK(parser.GetFields()[0].value == "");
}

// --- Binary file content with null bytes ---

TEST_CASE("Parse: binary file content with null bytes") {
  std::string boundary = "binbnd";
  std::string binary_content;
  binary_content += '\x00';
  binary_content += '\x01';
  binary_content += '\x02';
  binary_content += '\xFF';
  binary_content += '\x00';
  binary_content += '\xFE';

  std::string body = BuildMultipartBody(
      boundary, {},
      {{"bin", "data.bin", "application/octet-stream", binary_content}});

  MultipartParser parser(body, boundary);
  REQUIRE(parser.Parse());
  REQUIRE(parser.GetFiles().size() == 1);
  CHECK(parser.GetFiles()[0].content.size() == 6);
  CHECK(parser.GetFiles()[0].content == binary_content);
}

// --- LF-only line endings ---

TEST_CASE("Parse: LF-only line endings") {
  std::string boundary = "lfbnd";
  // Manually build with \n instead of \r\n.
  std::string body;
  body += "--" + boundary + "\n";
  body += "Content-Disposition: form-data; name=\"field1\"\n";
  body += "\n";
  body += "value1\n";
  body += "--" + boundary + "\n";
  body += "Content-Disposition: form-data; name=\"field2\"\n";
  body += "\n";
  body += "value2\n";
  body += "--" + boundary + "--\n";

  MultipartParser parser(body, boundary);
  REQUIRE(parser.Parse());
  REQUIRE(parser.GetFields().size() == 2);
  CHECK(parser.GetFields()[0].name == "field1");
  CHECK(parser.GetFields()[0].value == "value1");
  CHECK(parser.GetFields()[1].name == "field2");
  CHECK(parser.GetFields()[1].value == "value2");
}

// --- Missing boundary in body ---

TEST_CASE("Parse: missing boundary returns false") {
  std::string body = "just random text with no boundary";
  MultipartParser parser(body, "nonexistent");
  CHECK_FALSE(parser.Parse());
}

// --- Empty body ---

TEST_CASE("Parse: empty body returns false") {
  MultipartParser parser("", "someboundary");
  CHECK_FALSE(parser.Parse());
}

// --- Empty boundary ---

TEST_CASE("Parse: empty boundary returns false") {
  std::string body = "--boundary\r\nContent-Disposition: form-data; "
                     "name=\"x\"\r\n\r\nval\r\n--boundary--\r\n";
  MultipartParser parser(body, "");
  CHECK_FALSE(parser.Parse());
}

// --- Close delimiter only (no parts) ---

TEST_CASE("Parse: only close delimiter, no parts") {
  std::string boundary = "closebnd";
  std::string body = "--" + boundary + "--\r\n";

  MultipartParser parser(body, boundary);
  // The first delimiter IS the close delimiter, so no parts found.
  CHECK_FALSE(parser.Parse());
}

// --- Custom Content-Type on file part ---

TEST_CASE("Parse: custom content type on file part") {
  std::string boundary = "ctbnd";
  std::string body = BuildMultipartBody(
      boundary, {},
      {{"doc", "report.pdf", "application/pdf", "PDF_CONTENT"}});

  MultipartParser parser(body, boundary);
  REQUIRE(parser.Parse());
  REQUIRE(parser.GetFiles().size() == 1);
  CHECK(parser.GetFiles()[0].content_type == "application/pdf");
}

// --- File without Content-Type header defaults to octet-stream ---

TEST_CASE("Parse: file without content-type gets default") {
  std::string boundary = "noctbnd";
  // Manually build without Content-Type.
  std::string body;
  body += "--" + boundary + "\r\n";
  body += "Content-Disposition: form-data; name=\"file\"; "
          "filename=\"data.bin\"\r\n";
  body += "\r\n";
  body += "rawdata\r\n";
  body += "--" + boundary + "--\r\n";

  MultipartParser parser(body, boundary);
  REQUIRE(parser.Parse());
  REQUIRE(parser.GetFiles().size() == 1);
  CHECK(parser.GetFiles()[0].content_type == "application/octet-stream");
  CHECK(parser.GetFiles()[0].content == "rawdata");
}

// --- Unquoted name parameter ---

TEST_CASE("Parse: unquoted name parameter") {
  std::string boundary = "uqbnd";
  std::string body;
  body += "--" + boundary + "\r\n";
  body += "Content-Disposition: form-data; name=myfield\r\n";
  body += "\r\n";
  body += "myvalue\r\n";
  body += "--" + boundary + "--\r\n";

  MultipartParser parser(body, boundary);
  REQUIRE(parser.Parse());
  REQUIRE(parser.GetFields().size() == 1);
  CHECK(parser.GetFields()[0].name == "myfield");
  CHECK(parser.GetFields()[0].value == "myvalue");
}

// --- Large number of fields ---

TEST_CASE("Parse: many fields") {
  std::string boundary = "manybnd";
  std::vector<std::pair<std::string, std::string>> fields;
  for (int i = 0; i < 50; ++i) {
    fields.push_back({"field" + std::to_string(i),
                      "value" + std::to_string(i)});
  }
  std::string body = BuildMultipartBody(boundary, fields, {});

  MultipartParser parser(body, boundary);
  REQUIRE(parser.Parse());
  REQUIRE(parser.GetFields().size() == 50);
  CHECK(parser.GetFields()[0].name == "field0");
  CHECK(parser.GetFields()[49].name == "field49");
  CHECK(parser.GetFields()[49].value == "value49");
}

// --- Multiline field value ---

TEST_CASE("Parse: multiline field value") {
  std::string boundary = "mlbnd";
  std::string body;
  body += "--" + boundary + "\r\n";
  body += "Content-Disposition: form-data; name=\"message\"\r\n";
  body += "\r\n";
  body += "Line 1\r\nLine 2\r\nLine 3\r\n";
  body += "--" + boundary + "--\r\n";

  MultipartParser parser(body, boundary);
  REQUIRE(parser.Parse());
  REQUIRE(parser.GetFields().size() == 1);
  CHECK(parser.GetFields()[0].name == "message");
  CHECK(parser.GetFields()[0].value == "Line 1\r\nLine 2\r\nLine 3");
}
