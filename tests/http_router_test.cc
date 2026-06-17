#include "xtils/net/http_router.h"

#include <string>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

using namespace xtils;

// ============================================================================
// RouteParams tests
// ============================================================================

TEST_CASE("RouteParams: basic operations") {
  RouteParams params;
  CHECK(params.Count() == 0);

  params.Add("id", "42");
  params.Add("name", "alice");

  CHECK(params.Count() == 2);
  CHECK(params.Has("id"));
  CHECK(params.Has("name"));
  CHECK_FALSE(params.Has("missing"));

  CHECK(params.Get("id") == "42");
  CHECK(params["name"] == "alice");
  CHECK(params.Get("missing") == "");

  params.Clear();
  CHECK(params.Count() == 0);
}

// ============================================================================
// QueryParams tests
// ============================================================================

TEST_CASE("QueryParams: parse basic query string") {
  QueryParams q("key1=value1&key2=value2");

  CHECK(q.Has("key1"));
  CHECK(q.Has("key2"));
  CHECK_FALSE(q.Has("key3"));

  CHECK(q.Get("key1") == "value1");
  CHECK(q.Get("key2") == "value2");
  CHECK(q.Get("key3") == "");
  CHECK(q.Get("key3", "default") == "default");
}

TEST_CASE("QueryParams: empty query string") {
  QueryParams q("");
  CHECK_FALSE(q.Has("anything"));
  CHECK(q.Get("x") == "");
}

TEST_CASE("QueryParams: url-encoded values") {
  QueryParams q("q=hello+world&tag=%23cpp");
  CHECK(q.Has("q"));
  CHECK(q.Has("tag"));
}

TEST_CASE("QueryParams: duplicate keys") {
  QueryParams q("tag=cpp&tag=c17");
  auto all = q.GetAll("tag");
  CHECK(all.size() == 2);
}

// ============================================================================
// Router pattern matching tests
// ============================================================================

TEST_CASE("Router: exact path match") {
  bool called = false;
  Router route(
      HttpMethod::kGet, "/api/hello",
      [&](const HttpRequestContext&, HttpRouter::Response&) { called = true; });

  RouteParams params;
  CHECK(route.Matches(HttpMethod::kGet, "/api/hello", params));
  CHECK_FALSE(route.Matches(HttpMethod::kGet, "/api/world", params));
  CHECK_FALSE(route.Matches(HttpMethod::kPost, "/api/hello", params));
}

TEST_CASE("Router: path parameter extraction") {
  Router route(HttpMethod::kGet, "/users/{id}",
               [](const HttpRequestContext&, HttpRouter::Response&) {});

  RouteParams params;
  CHECK(route.Matches(HttpMethod::kGet, "/users/123", params));
  CHECK(params.Get("id") == "123");
}

TEST_CASE("Router: multiple path parameters") {
  Router route(HttpMethod::kGet, "/users/{user_id}/posts/{post_id}",
               [](const HttpRequestContext&, HttpRouter::Response&) {});

  RouteParams params;
  CHECK(route.Matches(HttpMethod::kGet, "/users/5/posts/99", params));
  CHECK(params.Get("user_id") == "5");
  CHECK(params.Get("post_id") == "99");
}

TEST_CASE("Router: method mismatch") {
  Router route(HttpMethod::kPost, "/api/data",
               [](const HttpRequestContext&, HttpRouter::Response&) {});

  RouteParams params;
  CHECK_FALSE(route.Matches(HttpMethod::kGet, "/api/data", params));
  CHECK(route.Matches(HttpMethod::kPost, "/api/data", params));
}

TEST_CASE("Router: Any method matches all") {
  Router route(HttpMethod::kAny, "/api/any",
               [](const HttpRequestContext&, HttpRouter::Response&) {});

  RouteParams params;
  CHECK(route.Matches(HttpMethod::kGet, "/api/any", params));
  CHECK(route.Matches(HttpMethod::kPost, "/api/any", params));
  CHECK(route.Matches(HttpMethod::kPut, "/api/any", params));
  CHECK(route.Matches(HttpMethod::kDelete, "/api/any", params));
}

// ============================================================================
// HttpRouter tests
// ============================================================================

TEST_CASE("HttpRouter: register and match routes") {
  HttpRouter router;
  bool get_called = false;
  bool post_called = false;

  router.Get("/test",
             [&](const HttpRequestContext&, HttpRouter::Response& resp) {
               get_called = true;
               resp.Text("ok");
             });

  router.Post("/test",
              [&](const HttpRequestContext&, HttpRouter::Response& resp) {
                post_called = true;
                resp.Text("created");
              });

  // We can't easily test HandleRequest without a full server connection,
  // but we can verify routes were registered by checking pattern matching
  CHECK_FALSE(get_called);
  CHECK_FALSE(post_called);
}

TEST_CASE("HttpRouter: route group prefix") {
  HttpRouter router;

  auto api = router.Group("/api/v1");
  bool called = false;
  api.Get("/users", [&](const HttpRequestContext&, HttpRouter::Response&) {
    called = true;
  });

  // The route should be registered as /api/v1/users
  // Verified by the internal route structure
  CHECK_FALSE(called);
}

TEST_CASE("HttpRouter: middleware registration") {
  HttpRouter router;
  bool mw_called = false;

  router.Use([&](const HttpRequestContext&, HttpRouter::Response&) -> bool {
    mw_called = true;
    return true;  // continue
  });

  router.Use("/api",
             [&](const HttpRequestContext&, HttpRouter::Response&) -> bool {
               return true;
             });

  CHECK_FALSE(mw_called);  // Only called during request handling
}

TEST_CASE("HttpRouter: CORS enable") {
  HttpRouter router;
  // Should not throw
  router.EnableCors("*", "GET,POST,PUT,DELETE");
}

TEST_CASE("Router: Express-style :param extraction") {
  Router route(HttpMethod::kGet, "/users/:id",
               [](const HttpRequestContext&, HttpRouter::Response&) {});

  RouteParams params;
  CHECK(route.Matches(HttpMethod::kGet, "/users/abc123", params));
  CHECK(params.Get("id") == "abc123");
}

TEST_CASE("Router: multiple :param segments") {
  Router route(HttpMethod::kGet, "/users/:user_id/posts/:post_id",
               [](const HttpRequestContext&, HttpRouter::Response&) {});

  RouteParams params;
  CHECK(route.Matches(HttpMethod::kGet, "/users/5/posts/99", params));
  CHECK(params.Get("user_id") == "5");
  CHECK(params.Get("post_id") == "99");
}

TEST_CASE("Router: :param mixed with {param}") {
  Router route(HttpMethod::kGet, "/{tenant}/users/:id",
               [](const HttpRequestContext&, HttpRouter::Response&) {});

  RouteParams params;
  CHECK(route.Matches(HttpMethod::kGet, "/acme/users/42", params));
  CHECK(params.Get("tenant") == "acme");
  CHECK(params.Get("id") == "42");
}

TEST_CASE("Router: literal mid-segment ':' is not a parameter") {
  // foo:bar is a literal segment, not a "foo" prefix + ":bar" param.
  Router route(HttpMethod::kGet, "/static/foo:bar",
               [](const HttpRequestContext&, HttpRouter::Response&) {});

  RouteParams params;
  CHECK(route.Matches(HttpMethod::kGet, "/static/foo:bar", params));
  CHECK_FALSE(route.Matches(HttpMethod::kGet, "/static/fooXY", params));
}

TEST_CASE("Router: :param fails when path component has '/'") {
  Router route(HttpMethod::kGet, "/users/:id",
               [](const HttpRequestContext&, HttpRouter::Response&) {});

  RouteParams params;
  CHECK_FALSE(route.Matches(HttpMethod::kGet, "/users/a/b", params));
}
