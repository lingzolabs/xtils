#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
#include "xtils/net/http_client.h"
#include "xtils/net/http_common.h"
#include "xtils/net/http_multipart.h"
#include "xtils/net/http_router.h"
#include "xtils/net/http_server.h"
#include "xtils/net/ipc_channel.h"
#include "xtils/net/tcp_client.h"
#include "xtils/net/tcp_server.h"
#include "xtils/net/udp_client.h"
#include "xtils/net/udp_server.h"
#include "xtils/net/websocket_client.h"
#include "xtils/net/websocket_common.h"

TEST_CASE("net public headers can be included together") {
  xtils::HttpClient::Request client_request;
  xtils::HttpClient::Response client_response;
  xtils::HttpServer::Request server_request(nullptr);
  xtils::HttpRouter::Response router_response;
  xtils::RouteParams params;
  xtils::WebSocketFrame frame;

  params.Add("id", "42");
  router_response.Text("ok");

  CHECK(client_request.method == xtils::HttpMethod::kGet);
  CHECK(client_response.status_code == 0);
  CHECK(server_request.conn == nullptr);
  CHECK(params.Get("id") == "42");
  CHECK(frame.fin);
}
