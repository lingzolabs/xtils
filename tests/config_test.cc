#include "xtils/config/config.h"

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

using namespace xtils;

// Helper function to create test files
static void create_test_file(const std::string& filename,
                             const std::string& content) {
  std::ofstream file(filename);
  file << content;
  file.close();
}

TEST_CASE("Basic Option Definition") {
  Config config;

  config.Define("string_opt", "String option", std::string("default"))
      .Define("int_opt", "Integer option", 42)
      .Define("double_opt", "Double option", 3.14)
      .Define("bool_opt", "Boolean option", true)
      .Define("required_opt", "Required option", "must_set", true);

  CHECK(config.GetString("string_opt").value() == "default");
  CHECK(config.GetInt("int_opt").value() == 42);
  CHECK(config.GetDouble("double_opt").value() == 3.14);
  CHECK(config.GetBool("bool_opt").value() == true);
  CHECK_FALSE(config.GetString("required_opt").has_value());
  config.Set("required_opt", "must_set");
  CHECK(config.GetString("required_opt").value() == "must_set");
}

TEST_CASE("Template Define") {
  Config config;

  config.Define<std::string>("str", "String", "hello")
      .Define<int>("num", "Number", 123)
      .Define<double>("pi", "Pi", 3.14159)
      .Define<bool>("flag", "Flag", false)
      .Define<const char*>("cstr", "C-String", "world");

  CHECK(config.GetString("str").value() == "hello");
  CHECK(config.GetInt("num").value() == 123);
  CHECK(config.GetDouble("pi").value() == 3.14159);
  CHECK(config.GetBool("flag").value() == false);
  CHECK(config.GetString("cstr").value() == "world");
}

TEST_CASE("Basic Getters") {
  Config config;

  config.Set("test.string", "hello world");
  config.Set("test.integer", 42);
  config.Set("test.double", 3.14);
  config.Set("test.bool", true);

  SUBCASE("Specialized getters") {
    CHECK(config.GetString("test.string").value() == "hello world");
    CHECK(config.GetInt("test.integer").value() == 42);
    CHECK(config.GetDouble("test.double").value() == 3.14);
    CHECK(config.GetBool("test.bool").value() == true);
  }

  SUBCASE("Default values for nonexistent keys") {
    CHECK(config.GetString("nonexistent").value_or("default") == "default");
    CHECK(config.GetInt("nonexistent").value_or(999) == 999);
    CHECK(config.GetDouble("nonexistent").value_or(1.23) == 1.23);
    CHECK(config.GetBool("nonexistent").value_or(false) == false);
  }
}

TEST_CASE("Template Getters") {
  Config config;

  config.Set("val1", 100);
  config.Set("val2", 2.5);
  config.Set("val3", "test");
  config.Set("val4", true);

  CHECK(config.Get<int>("val1").value() == 100);
  CHECK(config.Get<double>("val2").value() == 2.5);
  CHECK(config.Get<std::string>("val3").value() == "test");
  CHECK(config.Get<bool>("val4").value() == true);

  // int to double conversion
  CHECK(config.Get<double>("val1").value() == 100.0);
}

TEST_CASE("Template Setters") {
  Config config;

  config.Set<std::string>("str", "hello");
  config.Set<int>("int", 42);
  config.Set<double>("dbl", 3.14);
  config.Set<bool>("bool", true);
  config.Set<const char*>("cstr", "world");

  CHECK(config.GetString("str").value() == "hello");
  CHECK(config.GetInt("int").value() == 42);
  CHECK(config.GetDouble("dbl").value() == 3.14);
  CHECK(config.GetBool("bool").value() == true);
  CHECK(config.GetString("cstr").value() == "world");
}

TEST_CASE("Dot Notation Access") {
  Config config;

  config.Set("server.host", "localhost");
  config.Set("server.port", 8080);
  config.Set("server.ssl.enabled", true);
  config.Set("server.ssl.cert", "/path/to/cert");
  config.Set("database.connection.timeout", 30.5);

  CHECK(config.GetString("server.host").value() == "localhost");
  CHECK(config.GetInt("server.port").value() == 8080);
  CHECK(config.GetBool("server.ssl.enabled").value() == true);
  CHECK(config.GetString("server.ssl.cert").value() == "/path/to/cert");
  CHECK(config.GetDouble("database.connection.timeout").value() == 30.5);

  CHECK(config.Has("server.host"));
  CHECK(config.Has("server.ssl.enabled"));
  CHECK_FALSE(config.Has("server.nonexistent"));
}

TEST_CASE("Array Support") {
  Config config;

  std::vector<int64_t> int_vec = {1, 2, 3, 4, 5};
  std::vector<double> double_vec = {1.1, 2.2, 3.3};
  std::vector<std::string> string_vec = {"one", "two", "three"};

  config.Set("arrays.integers", int_vec);
  config.Set("arrays.doubles", double_vec);
  config.Set("arrays.strings", string_vec);

  auto retrieved_ints =
      config.Get<std::vector<int64_t>>("arrays.integers").value();
  auto retrieved_doubles =
      config.Get<std::vector<double>>("arrays.doubles").value();
  auto retrieved_strings =
      config.Get<std::vector<std::string>>("arrays.strings").value();

  REQUIRE(retrieved_ints.size() == 5);
  CHECK(retrieved_ints[0] == 1);
  CHECK(retrieved_ints[4] == 5);

  REQUIRE(retrieved_doubles.size() == 3);
  CHECK(retrieved_doubles[0] == 1.1);

  REQUIRE(retrieved_strings.size() == 3);
  CHECK(retrieved_strings[0] == "one");
}

TEST_CASE("JSON Parsing") {
  Config config;

  std::string json_content = R"({
    "server": {
      "port": 9090,
      "host": "0.0.0.0",
      "enabled": true,
      "timeout": 30.5
    },
    "database": {
      "connections": [10, 20, 30, 40],
      "hosts": ["db1.example.com", "db2.example.com"],
      "weights": [0.6, 0.4]
    },
    "features": {
      "auth": true,
      "cache": false,
      "debug": true
    }
  })";

  REQUIRE(config.Parse(json_content));

  CHECK(config.GetInt("server.port").value() == 9090);
  CHECK(config.GetString("server.host").value() == "0.0.0.0");
  CHECK(config.GetBool("server.enabled").value() == true);
  CHECK(config.GetDouble("server.timeout").value() == 30.5);

  auto connections =
      config.Get<std::vector<int64_t>>("database.connections").value();
  REQUIRE(connections.size() == 4);
  CHECK(connections[0] == 10);
  CHECK(connections[3] == 40);

  auto hosts = config.Get<std::vector<std::string>>("database.hosts").value();
  REQUIRE(hosts.size() == 2);
  CHECK(hosts[0] == "db1.example.com");

  auto weights = config.Get<std::vector<double>>("database.weights").value();
  REQUIRE(weights.size() == 2);
  CHECK(weights[0] == 0.6);
}

TEST_CASE("File Operations") {
  Config config;

  config.Define("app.name", "Application name", "TestApp")
      .Define("app.version", "Version", "1.0.0")
      .Define("server.port", "Server port", 8080)
      .Define("debug", "Debug mode", false);

  config.Set("app.name", "MyTestApp");
  config.Set("server.port", 9090);
  config.Set("debug", true);

  std::string filename = "test_config_doctest.json";

  REQUIRE(config.Save(filename));

  Config new_config;
  new_config.Define("app.name", "Application name", "DefaultApp")
      .Define("app.version", "Version", "0.0.0")
      .Define("server.port", "Server port", 80)
      .Define("debug", "Debug mode", false);

  REQUIRE(new_config.LoadFile(filename));

  CHECK(new_config.GetString("app.name").value() == "MyTestApp");
  CHECK(new_config.GetString("app.version").value() == "1.0.0");
  CHECK(new_config.GetInt("server.port").value() == 9090);
  CHECK(new_config.GetBool("debug").value() == true);

  std::remove(filename.c_str());
}

TEST_CASE("Command Line Parsing") {
  Config config;

  config.Define("port", "Server port", 8080)
      .Define("host", "Server host", "localhost")
      .Define("debug", "Debug mode", false)
      .Define("config-file", "Configuration file", "");

  const char* argv[] = {"program", "--port",  "9090", "--host",
                        "0.0.0.0", "--debug", "true"};
  int argc = sizeof(argv) / sizeof(argv[0]);

  REQUIRE(config.ParseArgs(argc, argv));

  CHECK(config.GetInt("port").value() == 9090);
  CHECK(config.GetString("host").value() == "0.0.0.0");
  CHECK(config.GetBool("debug").value() == true);
}

TEST_CASE("Validation") {
  Config config;

  config.Define("required_str", "Required string", "", true)
      .Define("optional_str", "Optional string", "default", false)
      .Define("required_int", "Required integer", 0, true);

  CHECK_FALSE(config.Validate());
  auto missing = config.MissingRequired();
  CHECK(missing.size() == 2);

  config.Set("required_str", "value");
  config.Set("required_int", 7);
  CHECK(config.Validate());
  CHECK(config.MissingRequired().empty());

  std::string help = config.Help();
  CHECK_FALSE(help.empty());
}

TEST_CASE("Help Generation") {
  Config config;

  config.Define("port", "Server listening port", 8080)
      .Define("host", "Server host address", "localhost")
      .Define("debug", "Enable debug logging", false, true)
      .Define("timeout", "Connection timeout in seconds", 30.0);

  std::string help = config.Help();

  CHECK(help.find("port") != std::string::npos);
  CHECK(help.find("Server listening port") != std::string::npos);
  CHECK(help.find("debug") != std::string::npos);
  CHECK(help.find("required") != std::string::npos);
}

TEST_CASE("Serialization") {
  Config config;

  config.Set("app.name", "TestApp");
  config.Set("app.version", "1.2.3");
  config.Set("server.port", 8080);
  config.Set("server.enabled", true);
  config.Set("limits.timeout", 30.5);

  std::vector<int> numbers = {1, 2, 3, 4, 5};
  config.Set("data.numbers", numbers);

  std::string str = config.ToString();
  CHECK_FALSE(str.empty());
  CHECK(str.find("TestApp") != std::string::npos);

  Json json = config.ToJson();
  CHECK(json.is_object());
  CHECK(json["app"]["name"].as_string() == "TestApp");
  CHECK(json["server"]["port"].as_integer() == 8080);
}

TEST_CASE("Config File with CLI Override") {
  std::string config_content = R"({
    "server": {
      "port": 8080,
      "host": "localhost"
    },
    "debug": false
  })";

  create_test_file("test_override_doctest.json", config_content);

  Config config;
  config.Define("server.port", "Port", 80)
      .Define("server.host", "Host", "127.0.0.1")
      .Define("debug", "Debug", false)
      .Define("config-file", "Config file", "");

  const char* argv[] = {
      "program",       "--config-file", "test_override_doctest.json",
      "--server.port", "9090",          "--debug",
      "true"};
  int argc = sizeof(argv) / sizeof(argv[0]);

  REQUIRE(config.ParseArgs(argc, argv));

  CHECK(config.GetInt("server.port").value() == 9090);
  CHECK(config.GetString("server.host").value() == "localhost");
  CHECK(config.GetBool("debug").value() == true);

  std::remove("test_override_doctest.json");
}

TEST_CASE("Edge Cases") {
  Config config;

  SUBCASE("Empty strings") {
    config.Set("empty", "");
    CHECK(config.GetString("empty").value() == "");
  }

  SUBCASE("Zero values") {
    config.Set("zero_int", 0);
    config.Set("zero_double", 0.0);
    config.Set("false_bool", false);

    CHECK(config.GetInt("zero_int").value() == 0);
    CHECK(config.GetDouble("zero_double").value() == 0.0);
    CHECK(config.GetBool("false_bool").value() == false);
  }

  SUBCASE("Large numbers") {
    config.Set("large_int", 9223372036854775807LL);
    config.Set("large_double", 1.7976931348623157e+308);

    CHECK(config.GetInt("large_int").value() == 9223372036854775807LL);
    CHECK(config.GetDouble("large_double").value() > 1e308);
  }

  SUBCASE("Negative numbers") {
    config.Set("neg_int", -12345);
    config.Set("neg_double", -3.14159);

    CHECK(config.GetInt("neg_int").value() == -12345);
    CHECK(config.GetDouble("neg_double").value() == -3.14159);
  }

  SUBCASE("Special double values") {
    config.Set("small_double", 1e-100);
    config.Set("precise_double", 0.123456789012345);

    CHECK(config.GetDouble("small_double").value() == 1e-100);
    CHECK(config.GetDouble("precise_double").value() == 0.123456789012345);
  }
}

TEST_CASE("Type Conversions") {
  Config config;

  config.Set("str_num", "42");
  config.Set("str_double", "3.14");
  config.Set("str_bool", "true");
  config.Set("int_to_double", 100);
  config.Set("double_to_int", 99.9);

  CHECK(config.GetInt("int_to_double").value() == 100);
  CHECK(config.Get<double>("int_to_double").value() == 100.0);
  CHECK(config.GetDouble("double_to_int").value() == 99.9);

  // Float stored value won't convert to int
  CHECK(config.GetInt("double_to_int").value_or(-1) == -1);

  config.Set("pure_int", 42);
  CHECK(config.Get<int>("pure_int").value() == 42);
  CHECK(config.Get<double>("pure_int").value() == 42.0);
}

TEST_CASE("Complex Nested Structures") {
  Config config;

  std::string complex_json = R"({
    "database": {
      "primary": {
        "host": "db1.example.com",
        "port": 5432,
        "credentials": {
          "username": "app_user",
          "password": "secret123"
        },
        "pools": {
          "read": {
            "min": 5,
            "max": 20,
            "timeouts": [10, 30, 60]
          },
          "write": {
            "min": 2,
            "max": 10,
            "timeouts": [5, 15, 30]
          }
        }
      },
      "replicas": [
        {
          "host": "db2.example.com",
          "port": 5432,
          "weight": 0.7
        },
        {
          "host": "db3.example.com",
          "port": 5432,
          "weight": 0.3
        }
      ]
    }
  })";

  REQUIRE(config.Parse(complex_json));

  CHECK(config.GetString("database.primary.host").value() == "db1.example.com");
  CHECK(config.GetInt("database.primary.port").value() == 5432);
  CHECK(config.GetString("database.primary.credentials.username").value() ==
        "app_user");
  CHECK(config.GetInt("database.primary.pools.read.min").value() == 5);
  CHECK(config.GetInt("database.primary.pools.write.max").value() == 10);

  auto read_timeouts =
      config.Get<std::vector<int64_t>>("database.primary.pools.read.timeouts")
          .value();
  REQUIRE(read_timeouts.size() == 3);
  CHECK(read_timeouts[0] == 10);
  CHECK(read_timeouts[2] == 60);
}

TEST_CASE("Error Handling") {
  Config config;

  CHECK_FALSE(config.Parse("{ invalid json }"));
  CHECK_FALSE(config.Parse("{ \"key\": }"));
  CHECK_FALSE(config.LoadFile("non_existent_file.json"));

  auto optional_val = config.Get("non.existent.path");
  CHECK_FALSE(optional_val.has_value());

  CHECK(config.GetString("missing.key").value_or("default") == "default");
  CHECK(config.GetInt("missing.key").value_or(42) == 42);
  CHECK(config.GetBool("missing.key").value_or(true) == true);
}

TEST_CASE("Comprehensive JSON Compatibility") {
  Config config;

  std::string comprehensive_json = R"({
    "null_value": null,
    "bool_true": true,
    "bool_false": false,
    "integer": 42,
    "negative_int": -123,
    "float_value": 3.14159,
    "string_value": "hello world",
    "empty_string": "",
    "array_empty": [],
    "array_mixed": [1, "two", 3.0, true, null],
    "object_empty": {},
    "object_nested": {
      "level1": {
        "level2": {
          "deep_value": "found"
        }
      }
    }
  })";

  REQUIRE(config.Parse(comprehensive_json));

  CHECK((!config.Has("null_value") || config.Get("null_value")->is_null()));
  CHECK(config.GetBool("bool_true").value() == true);
  CHECK(config.GetBool("bool_false").value() == false);
  CHECK(config.GetInt("integer").value() == 42);
  CHECK(config.GetInt("negative_int").value() == -123);
  CHECK(config.GetDouble("float_value").value() == 3.14159);
  CHECK(config.GetString("string_value").value() == "hello world");
  CHECK(config.GetString("empty_string").value() == "");
  CHECK(config.GetString("object_nested.level1.level2.deep_value").value() ==
        "found");
}

TEST_CASE("Skip config-file reload on second ParseArgs") {
  // Create a config file
  std::string config_content = R"({
    "server": {
      "port": 8080
    }
  })";
  create_test_file("test_reload_skip.json", config_content);

  Config config;
  config.Define("server.port", "Port", 80)
      .Define("server.host", "Host", "localhost")
      .Define("config-file", "Config file", "");

  // First parse loads the config file
  const char* argv1[] = {"program", "--config-file", "test_reload_skip.json"};
  REQUIRE(config.ParseArgs(3, argv1));
  CHECK(config.GetInt("server.port").value() == 8080);

  // Modify the file to a different value
  create_test_file("test_reload_skip.json", R"({"server":{"port":9999}})");

  // Second parse should NOT reload the file
  config.Define("server.host", "Host", "localhost");
  const char* argv2[] = {"program", "--config-file", "test_reload_skip.json",
                         "--server.host", "0.0.0.0"};
  REQUIRE(config.ParseArgs(5, argv2));

  // port should still be 8080 (not reloaded), host should be from CLI
  CHECK(config.GetInt("server.port").value() == 8080);
  CHECK(config.GetString("server.host").value() == "0.0.0.0");

  std::remove("test_reload_skip.json");
}

// ─── Short option aliases ───────────────────────────────────────────────

TEST_CASE("Config short alias: -x value") {
  Config config;
  config.Define("port", "Server port", 80); config.Short("port", "p");

  const char* argv[] = {"prog", "-p", "9090"};
  REQUIRE(config.ParseArgs(3, argv));
  CHECK(config.GetInt("port").value() == 9090);
}

TEST_CASE("Config short alias: -x=value") {
  Config config;
  config.Define("port", "Server port", 80); config.Short("port", "p");

  const char* argv[] = {"prog", "-p=8081"};
  REQUIRE(config.ParseArgs(2, argv));
  CHECK(config.GetInt("port").value() == 8081);
}

TEST_CASE("Config short alias: compact -xvalue") {
  Config config;
  config.Define("config-path", "Path to file", std::string("/tmp/x")); config.Short("config-path", "c");

  const char* argv[] = {"prog", "-c/etc/foo.json"};
  REQUIRE(config.ParseArgs(2, argv));
  CHECK(config.GetString("config-path").value() == "/etc/foo.json");
}

TEST_CASE("Config short alias: bare -x for boolean is true") {
  Config config;
  config.Define("verbose", "Verbose mode", false); config.Short("verbose", "v");

  const char* argv[] = {"prog", "-v"};
  REQUIRE(config.ParseArgs(2, argv));
  CHECK(config.GetBool("verbose").value() == true);
}

TEST_CASE("Config short alias: chained booleans -vqf") {
  Config config;
  config.Define("verbose", "Verbose mode", false); config.Short("verbose", "v");
  config.Define("quiet", "Quiet mode", false); config.Short("quiet", "q");
  config.Define("force", "Force", false); config.Short("force", "f");

  const char* argv[] = {"prog", "-vqf"};
  REQUIRE(config.ParseArgs(2, argv));
  CHECK(config.GetBool("verbose").value() == true);
  CHECK(config.GetBool("quiet").value() == true);
  CHECK(config.GetBool("force").value() == true);
}

TEST_CASE("Config short alias: unknown short flag goes to no_parsed_") {
  Config config;
  config.Define("verbose", "Verbose", false); config.Short("verbose", "v");

  const char* argv[] = {"prog", "-z"};
  REQUIRE(config.ParseArgs(2, argv));
  auto unparsed = config.NoParsed();
  bool found = false;
  for (const auto& s : unparsed) {
    if (s == "-z") {
      found = true;
      break;
    }
  }
  CHECK(found);
}

TEST_CASE("Config short alias: long form still works after Define with short") {
  Config config;
  config.Define("port", "Server port", 80); config.Short("port", "p");

  const char* argv[] = {"prog", "--port=7777"};
  REQUIRE(config.ParseArgs(2, argv));
  CHECK(config.GetInt("port").value() == 7777);
}

TEST_CASE("Config short alias: Help() shows both forms") {
  Config config;
  config.Define("port", "Server port", 80); config.Short("port", "p");
  std::string h = config.Help();
  CHECK(h.find("--port, -p") != std::string::npos);
}
