#include <fstream>
#include <iostream>
#include <unordered_map>
#include <unordered_set>

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
  std::cout << "The 'long name' of each route in the MBTA subway system:" << std::endl;
  for (auto const& item : routes_json["data"])
    std::cout << item["attributes"]["long_name"].get<std::string>() << std::endl;
  std::cout << std::endl;
}

nlohmann::json queryAndParse(std::string url)
{
  std::string response_body = curlMBTA(url);
  nlohmann::json ret;
  try
  {
    ret = nlohmann::json::parse(response_body);
  }
  catch(const std::exception& e)
  {
    std::cerr << "failed to parse queryAndParse() JSON response; its raw text: "
              << response_body << std::endl;
    crash("failed to parse JSON response");
  }
  return ret;
}

std::vector<std::string> getRouteIDs(nlohmann::json routes_json)
{
  std::vector<std::string> ret;
  for (auto const& item : routes_json["data"])
    ret.push_back(item["id"].get<std::string>());
  return ret;
}

// NOTE: in addition to item["attributes"]["name"], there is also item["id"],
// which looks like e.g. place-dwnxg for Downtown Crossing. I assume it is the
// MBTA's canonical unique ID for these stops. So, that would be the best thing
// to track throughout the code, in case you wanted to further interact with
// the MBTA's system. However, for our purposes the display name actually works
// fine, and keeping track of both would make the code uglier (since the human
// input will use the display names). So I'm using display names for everything.
// (I tested that stops always keep the same display name when they appear in
// multiple routes).
std::vector<std::string> getStops(nlohmann::json stops_json)
{
  std::vector<std::string> ret;
  for (auto const& item : stops_json["data"])
    ret.push_back(item["attributes"]["name"].get<std::string>());
  return ret;
}

int main(int argc, char** argv)
{
  // question 1
  nlohmann::json routes_json = queryAndParse("https://api-v3.mbta.com/routes?filter[type]=0,1");
  printRouteLongNames(routes_json);

  // gathering and structuring data for questions 2 and 3
  std::vector<std::string> route_ids = getRouteIDs(routes_json);
  std::string route_query_prefix = "https://api-v3.mbta.com/stops?filter[route]=";

  // the edges of the MBTA graph (stops being nodes), as adjacency list.
  std::unordered_map<std::string, std::vector<std::string>> adjacency_lists;
  // which routes does this stop appear in? e.g. Downtown Crossing maps to {red, orange}.
  std::unordered_map<std::string, std::vector<std::string>> routes_of_stop;

  int most_stops_count = 0;
  std::string most_stops_route;
  int fewest_stops_count = INT_MAX;
  std::string fewest_stops_route;
  for (std::string route_name : route_ids)
  {
    nlohmann::json stops_json = queryAndParse(route_query_prefix + route_name);
    int num_stops = stops_json["data"].size();
    if (num_stops < fewest_stops_count)
    {
      fewest_stops_count = num_stops;
      fewest_stops_route = route_name;
    }
    if (num_stops > most_stops_count)
    {
      most_stops_count = num_stops;
      most_stops_route = route_name;
    }

    std::vector<std::string> cur_route_stops = getStops(stops_json);
    for (std::string stop_name : cur_route_stops)
      routes_of_stop[stop_name].push_back(route_name);
    for (int i = 0; i < cur_route_stops.size(); i++)
    {
      if (i > 0)
        adjacency_lists[cur_route_stops[i]].push_back(cur_route_stops[i-1]);
      if (i < cur_route_stops.size() - 1)
        adjacency_lists[cur_route_stops[i]].push_back(cur_route_stops[i+1]);
    }
  }

  // question 2
  // NOTE: I'm using an algorithmic interpretations of "connects the routes" rather
  // than human/intuitive, meaning that any green line stop that multiple sub-lines
  // flow through is considered a "connection" of all of them, rather than just
  // "Copley, Arlington etc are on the trunk of the green line."
  std::cout << most_stops_route << " has the most stops: " << most_stops_count << std::endl;
  std::cout << fewest_stops_route << " has the fewest stops: " << fewest_stops_count << std::endl;
  std::cout << std::endl << "Here are all stops that connect routes:\n"
            << "================================================" << std::endl;
  for (auto const& [stop, routes] : routes_of_stop)
  {
    if (routes.size() > 1)
    {
      std::cout << stop << " connects the following routes: ";
      for (std::string route : routes)
        std::cout << route << ", ";
      std::cout << std::endl;
    }
  }

  return 0;
}
