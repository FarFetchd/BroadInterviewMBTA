#include <fstream>
#include <iostream>

#include <curl/curl.h>

#include "json.hpp"

void crash(const char* s)
{
  std::cerr << s << std::endl;
  exit(1);
}

// trims any trailing newlines (does include other trailing whitespace).
std::string readStringFile(std::string path)
{
  std::ifstream t(path);
  std::string s((std::istreambuf_iterator<char>(t)),
                     std::istreambuf_iterator<char>());
  while (!s.empty() && s.back() == '\n')
    s.pop_back();
  return s;
}

std::string apiKey()
{
  // to avoid static init order fiasco - obviously overkill here, but it's the
  // type of thing you want to just have a blanket policy for.
  static std::string* key = new std::string;
  if (key->empty())
    *key = readStringFile("api_key.txt");
  return *key;
}

static size_t WriteCallback(void* data, size_t size, size_t nmemb, void* usr)
{
  ((std::string*)usr)->append((char*)data, size * nmemb);
  return size * nmemb;
}

std::string curlMBTA(std::string url)
{
  CURL* curl = curl_easy_init();
  if (!curl)
    crash("curl_easy_init() in curlMBTA() failed!");

  std::string auth_header = "Authorization: " + apiKey();
  struct curl_slist* list = NULL;
  list = curl_slist_append(list, "Accept: application/json");
  list = curl_slist_append(list, auth_header.c_str());
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
  std::string response_body;
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
  CURLcode res = curl_easy_perform(curl);
  curl_easy_cleanup(curl);
  curl_slist_free_all(list);

  return response_body;
}

void printRouteLongNames(nlohmann::json routes_json)
{
  for (auto& item : routes_json["data"])
    std::cout << item["attributes"]["long_name"].get<std::string>() << std::endl;
}

nlohmann::json queryRoutes()
{
  std::string response_body = curlMBTA("https://api-v3.mbta.com/routes?filter[type]=0,1&include=line");
  nlohmann::json ret;
  try
  {
    ret = nlohmann::json::parse(response_body);
  }
  catch(const std::exception& e)
  {
    std::cerr << "failed to parse queryRoutes() JSON response; its raw text: "
              << response_body << std::endl;
    crash("failed to parse JSON response");
  }
  return ret;
}

int main(int argc, char** argv)
{
  nlohmann::json routes_json = queryRoutes();
  printRouteLongNames(routes_json);
//   std::cout << routes_json.dump() << std::endl;

  std::cout << curlMBTA("https://api-v3.mbta.com/stops?filter[route]=Blue") << std::endl;
  return 0;
}
