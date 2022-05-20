#include <fstream>
#include <iostream>
#include <unordered_map>
#include <unordered_set>

#include <curl/curl.h>

#include "json.hpp"

// ================= BEGIN boring mechanical stuff ============================

void crash(std::string s)
{
  std::cerr << s << std::endl;
  exit(1);
}

// (from my file_io.cc bag of tricks; probably came from stackoverflow years ago)
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

static size_t curlWriteCallback(void* data, size_t size, size_t nmemb, void* usr)
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
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
  std::string response_body;
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
  CURLcode res = curl_easy_perform(curl);
  curl_easy_cleanup(curl);
  curl_slist_free_all(list);

  return response_body;
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
    crash("failed to parse queryAndParse() JSON response: " + response_body);
  }
  return ret;
}

// ================= END boring mechanical stuff =============================

void printRouteLongNames(nlohmann::json routes_json)
{
  std::cout << "The 'long name' of each route in the MBTA subway system:" << std::endl;
  for (auto const& item : routes_json["data"])
    std::cout << item["attributes"]["long_name"].get<std::string>() << std::endl;
  std::cout << std::endl;
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

// This would become a method of a class that loads in adjacency_lists and
// routes_of_stop at construction, if this was going to evolve beyond a job
// interview code sample thingy.
void plotRouteFromTo(
    std::string from, std::string to,
    std::unordered_map<std::string, std::vector<std::string>> const& adjacency_lists,
    std::unordered_map<std::string, std::vector<std::string>> const& routes_of_stop)
{
  if (routes_of_stop.find(from) == routes_of_stop.end())
    crash(from + ": no such stop.");
  if (routes_of_stop.find(to) == routes_of_stop.end())
    crash(to + ": no such stop.");
}

int main(int argc, char** argv)
{
  // There's a great library called structopt I would use here if it weren't
  // overkill that would actually make the code (and usage) overall more complex.
  if (argc != 1 && argc != 3)
  {
    crash("Supply 0 arguments to get an MBTA overview, or 2 arguments (station names) "
          "to get the fewest-stop path from A to B. Enclose station names containing "
          "spaces in double quotes.");
  }
  bool show_question_1_and_2 = (argc == 1);
  std::string from_stop, to_stop;
  if (argc == 3)
  {
    from_stop = argv[1];
    to_stop = argv[2];
  }

  nlohmann::json routes_json = queryAndParse("https://api-v3.mbta.com/routes?filter[type]=0,1");

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

    // track the min/max counts for question 2
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

    // track multi-route stops for question 2
    std::vector<std::string> cur_route_stops = getStops(stops_json);
    for (std::string stop_name : cur_route_stops)
      routes_of_stop[stop_name].push_back(route_name);

    // build adjacency lists
    for (int i = 0; i < cur_route_stops.size(); i++)
    {
      if (i > 0)
        adjacency_lists[cur_route_stops[i]].push_back(cur_route_stops[i-1]);
      if (i < cur_route_stops.size() - 1)
        adjacency_lists[cur_route_stops[i]].push_back(cur_route_stops[i+1]);
    }
  }

  if (show_question_1_and_2)
  {
    std::cout << "\n First, here's a list of all stop names, to help with "
              << "getting the input for the shortest-path query mode right:\n";
    for (auto const& [stop, junk] : routes_of_stop)
      std::cout << stop << std::endl;
    std::cout << "\n(end of list of all stop names)\n\n";

    // question 1
    printRouteLongNames(routes_json);

    // question 2
    std::cout << most_stops_route << " has the most stops: " << most_stops_count << std::endl;
    std::cout << fewest_stops_route << " has the fewest stops: " << fewest_stops_count << std::endl;
    // NOTE: I'm using an algorithmic interpretations of "connects the routes" rather
    // than human/intuitive, meaning that any green line stop that multiple sub-lines
    // flow through is considered a "connection" of all of them, rather than just
    // "Copley, Arlington etc are on the trunk of the green line."
    std::cout << "\nHere are all stops that connect routes:\n"
              << "================================================\n";
    for (auto const& [stop, routes] : routes_of_stop)
    {
      if (routes.size() > 1)
      {
        std::cout << stop << " connects: ";
        for (std::string route : routes)
          std::cout << route << ", ";
        std::cout << std::endl;
      }
    }
  }
  else
    plotRouteFromTo(from_stop, to_stop, adjacency_lists, routes_of_stop);

  return 0;
}
