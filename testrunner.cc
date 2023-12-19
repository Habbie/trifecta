#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"
#include "support.hh"
#include "httplib.h"
#include "nlohmann/json.hpp"

using namespace std;

TEST_CASE("cookie test") {
  auto res = getCookies("test=1235; test2=9876");
  CHECK(res.size() == 2);
  CHECK(res["test"]=="1235");
  CHECK(res["test2"]=="9876");

  res = getCookies("user=ahu");
  CHECK(res.size() == 1);
  CHECK(res["user"]=="ahu");

  res = getCookies("test=1235; test2=9876; boeh=bah");
  CHECK(res.size() == 3);
  CHECK(res["test"]=="1235");
  CHECK(res["test2"]=="9876");
  CHECK(res["boeh"]=="bah");  
}

TEST_CASE("form parse test") {
  auto res = getFormFields("user=ahu&password=Super123Secret");
  CHECK(res.size() == 2);
  CHECK(res["user"]=="ahu");
  CHECK(res["password"]=="Super123Secret");
}

TEST_CASE("base64url id") {
  CHECK(makeShortID((1ULL<<62) - 132430)== "svr9_____z8");
  CHECK(makeShortID( 4529558240454539472)== "0Hi8lzg53D4");
  CHECK(makeShortID(2984614381840956837) == "pf0mlfd6ayk");
  CHECK(makeShortID(1) == "AQAAAAAAAAA");
}


namespace {
  struct TrifectaServer
  {
    TrifectaServer() {
      std::thread t1([]() {
        unlink("testrunner.sqlite");
        const char* argv[] = {"./trifecta", "testrunner.sqlite", "-p", "9999",
          "--admin-password=admin1234",
          "--local-address=127.0.0.1"};
        trifectaMain(6, argv);
      });
      d_t = std::move(t1);
      d_t.detach();
      usleep(250000);
    }

    httplib::Headers doLogin(const string& user = "admin", const string& password="admin1234")
    {
      httplib::Client cli("127.0.0.1", 9999);
      // yeah this is insecure, but it is also our test framework
      auto res = cli.Post("/login", "user="+ user +"&password="+password, "application/x-www-form-urlencoded");
      if(res == nullptr)
        throw std::runtime_error("Can't connect for login");
    
      nlohmann::json j= nlohmann::json::parse(res->body);
    
      if(j["ok"] != 1)
        throw std::runtime_error("Can't login");
    
      string cookieline= res->get_header_value("Set-Cookie");
      //session=c7XaOYsDOhYei09WzN_9hA; SameSite=Strict; Path=/; Max-Age=157680000
      auto pos = cookieline.find('=');
      if(pos == string::npos)
        throw std::runtime_error("Can't parse cookie line");
      auto pos2 = cookieline.find(';', pos);
      if(pos2 == string::npos)
        throw std::runtime_error("Can't parse cookie line 2");

      pos++;
      string session = cookieline.substr(pos, pos2-pos);
      cout<<"session is '"<<session<<"'\n";
      httplib::Headers headers = {
      { "Cookie", "session="+session }
      };
      
      return headers;
    }
    
    ~TrifectaServer()
    {
      usleep(1100000); // so the sqlite stuff get synched
      /*
      cout<<"Destructor called"<<endl;
      auto headers = doLogin();
      httplib::Client cli("127.0.0.1", 9999);
      auto res = cli.Post("/stop", headers);
      cli.stop();
      */
    }
    std::thread d_t;
  } g_tfs;
}

TEST_CASE("basic web tests") {

  httplib::Client cli("127.0.0.1", 9999);

  auto headers = g_tfs.doLogin();

  auto res = cli.Get("/status", headers);
  REQUIRE(res != 0);
  
  nlohmann::json j = nlohmann::json::parse(res->body);
  CHECK(j["admin"]==true);
  CHECK(j["user"]=="admin");
  CHECK(j["login"]==true);

  /////////////////////
  
  httplib::MultipartFormDataItems items = {
    { "file", "test content", "hello.png", "image/png" }
  };

  res = cli.Post("/upload", headers, items);
  REQUIRE(res != 0);

  j = nlohmann::json::parse(res->body);
  CHECK(j["postId"] != "");

  string upload1 = j["id"];
  string postId = j["postId"];

  ///////////

  httplib::MultipartFormDataItems items2 = {
    { "file", "test content extra", "hello2.png", "image/png" },
    { "postId", postId, "postId"}
  };

  res = cli.Post("/upload", headers, items2);
  REQUIRE(res != 0);
  
  j = nlohmann::json::parse(res->body);
  CHECK(j["postId"]==postId);
  string upload2 = j["id"];

  ///////////

  res = cli.Post("/set-post-title/"+postId, headers,
                 httplib::MultipartFormDataItems({{"title", "this is the title", "title"}}));

  REQUIRE(res);
  REQUIRE(res->status == 200);

  ///////////

  res = cli.Post("/set-image-caption/"+upload1, headers,
                 httplib::MultipartFormDataItems({{"caption", "this is a caption", "caption"}}));

  REQUIRE(res);
  REQUIRE(res->status == 200);

  ///////////

  res = cli.Post("/set-image-caption/"+upload2, headers,
                 httplib::MultipartFormDataItems({{"caption", "this is a second caption", "caption"}}));

  REQUIRE(res);
  REQUIRE(res->status == 200);

  
  ////
  res = cli.Get("/getPost/"+postId);
  REQUIRE(res);
  j = nlohmann::json::parse(res->body);
  CHECK(j["images"].size() == 2);
  
  CHECK(j["title"] == "this is the title");

  CHECK(j["images"][0]["id"]==upload1);
  CHECK(j["images"][0]["caption"] == "this is a caption");

  CHECK(j["images"][1]["id"]==upload2);
  CHECK(j["images"][1]["caption"] == "this is a second caption");
  
  ///////////
  
  res = cli.Post("/logout", headers);
  REQUIRE(res != 0);

  string cookieline = res->get_header_value("Set-Cookie");
  CHECK(cookieline.find("Max-Age=0") != string::npos);

  //////////////
  res = cli.Get("/status", headers);
  REQUIRE(res != 0);
  cout<< res->body << endl;
  cout<< res->status << endl;
  
  j = nlohmann::json::parse(res->body);
  CHECK(j["login"]==false);
}


TEST_CASE("web visibility tests") {
  httplib::Client cli("127.0.0.1", 9999);

  auto adminSession = g_tfs.doLogin();

  // create user piet
  httplib::MultipartFormDataItems items2 = {
    { "user", "piet", "user" },
    { "password1", "piet123piet", "password1" }
  };

  auto res = cli.Post("/create-user", adminSession, items2);
  REQUIRE(res != 0);

  auto pietSession = g_tfs.doLogin("piet", "piet123piet");
  res = cli.Get("/status", pietSession);
  REQUIRE(res != 0);
  
  nlohmann::json j = nlohmann::json::parse(res->body);
  CHECK(j["admin"]==false);
  CHECK(j["user"]=="piet");
  CHECK(j["login"]==true);

  
  httplib::MultipartFormDataItems items = {
    { "file", "test content 123", "hello2.png", "image/png" }
  };

  res = cli.Post("/upload", pietSession, items);
  REQUIRE(res != 0);
  j = nlohmann::json::parse(res->body);
  CHECK(j["postId"] != "");

  string upload1 = j["id"];
  string postId = j["postId"];

  res = cli.Get("/i/"+upload1); // anon access
  REQUIRE(res != 0); CHECK(res->body ==items[0].content);

  res = cli.Get("/i/"+upload1, pietSession); // with cookie
  REQUIRE(res != 0); CHECK(res->body ==items[0].content);

  res = cli.Get("/i/"+upload1, adminSession); // with admin cookie
  REQUIRE(res != 0); CHECK(res->body ==items[0].content);

  // make post non-public
  res = cli.Post("/set-post-public/"+postId+"/0", pietSession);
  REQUIRE(res != 0);

  res = cli.Get("/i/"+upload1); // no cookie
  REQUIRE(res != 0); CHECK(res->status == 404);
  
  res = cli.Get("/i/"+upload1, pietSession); // with cookie
  REQUIRE(res != 0); CHECK(res->body ==items[0].content);


  res = cli.Get("/i/"+upload1, adminSession); // admin
  REQUIRE(res != 0); CHECK(res->body == items[0].content);


  // admin makes post public
  res = cli.Post("/set-post-public/"+postId+"/1", adminSession);
  REQUIRE(res != 0);

  res = cli.Get("/i/"+upload1); // no cookie
  REQUIRE(res != 0); CHECK(res->body == items[0].content);

  
}
