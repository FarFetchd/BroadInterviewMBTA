#include <fstream>
#include <iostream>
#include <queue>
#include <set>
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
  try
  {
    if (key->empty())
      *key = readStringFile("api_key.txt");
  }
  catch(const std::exception& e) {} // should still work even without a key
  if (key->empty())
    std::cerr << "Couldn't read api_key.txt. That's ok; we'll just be rate limited." << std::endl;
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
  if (!apiKey().empty())
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

class RoutePlanner
{
public:
  RoutePlanner(std::unordered_map<std::string, std::vector<std::string>> adjacency_lists,
               std::unordered_map<std::string, std::set<std::string>> routes_of_stop)
  : adjacency_lists_(adjacency_lists),
    routes_of_stop_(routes_of_stop) {}

  // Returns the list of line names (e.g. Red, Orange) you should take to get
  // from the station 'src' to 'dst'.
  std::vector<std::string> plotRouteFromTo(std::string src, std::string dst)
  {
    if (!routes_of_stop_.contains(src))
      crash(src + ": no such stop.");
    if (!routes_of_stop_.contains(dst))
      crash(dst + ": no such stop.");

    std::unordered_map<std::string, std::string> backlinks =
        backlinksBFS(adjacency_lists_, src, dst);

    // assemble path from backlinks
    std::vector<std::string> our_path;
    std::string cur_hop = dst;
    do
    {
      our_path.push_back(cur_hop);
      cur_hop = backlinks[cur_hop];
    } while (cur_hop != src);
    our_path.push_back(cur_hop);
    std::reverse(our_path.begin(), our_path.end());

    // We have our_path in stops. Now, to convert stops to routes, let's greedily
    // stay on the same starting route as long as possible. Set intersection will
    // tell us what routes are viable, as well as when we are forced to switch.
    int station_index = 0;
    std::vector<std::string> routes_to_travel;
    while (station_index < our_path.size())
    {
      auto [route_name, next_stop_ind] = greedilyStayOnRoute(our_path, station_index);
      station_index = next_stop_ind;
      routes_to_travel.push_back(route_name);
    }
    return routes_to_travel;
  }

private:

  // BFS, tracking backlinks. Returns a map of backlinks:
  // backlinks[backlinks[...[dst]...]] gets you back to src.
  std::unordered_map<std::string, std::string> backlinksBFS(
      std::unordered_map<std::string, std::vector<std::string>>& adjacency_lists,
      std::string src, std::string dst)
  {
    std::unordered_map<std::string, std::string> backlinks;
    std::unordered_set<std::string> visited;
    std::queue<std::string> to_visit;
    to_visit.push(src);
    while (!to_visit.empty() && to_visit.front() != dst)
    {
      std::string cur = to_visit.front();
      to_visit.pop();
      visited.insert(cur);
      for (std::string const& neighbor : adjacency_lists[cur])
      {
        if (visited.contains(neighbor))
          continue;
        to_visit.push(neighbor);
        backlinks[neighbor] = cur;
      }
    }
    if (to_visit.empty())
      crash("Can't get to "+src+" from "+dst);
    return backlinks;
  }

  // Starting from path[station_index], return the name of the line that you can
  // stay on for the most stations in this path. Also returns the index where
  // you have to switch to a new line - meaning you should call this function
  // again on that index.
  std::pair<std::string, int> greedilyStayOnRoute(std::vector<std::string> const& path,
                                                  int station_index)
  {
    std::set<std::string> candidates = routes_of_stop_[path[station_index++]];
    while (true)
    {
      std::set<std::string> new_candidates;
      std::set_intersection(candidates.begin(), candidates.end(),
                            routes_of_stop_[path[station_index]].begin(),
                            routes_of_stop_[path[station_index]].end(),
                            std::inserter(new_candidates, new_candidates.begin()));
      if (new_candidates.empty())
        return std::make_pair(*candidates.begin(), station_index);
      station_index++;
      candidates = new_candidates;
      if (station_index >= path.size())
        return std::make_pair(*candidates.begin(), station_index);
    }
  }

  // the edges of the MBTA graph (stops being nodes), as adjacency list.
  std::unordered_map<std::string, std::vector<std::string>> adjacency_lists_;
  // which routes does this stop appear in? e.g. Downtown Crossing maps to {red, orange}.
  std::unordered_map<std::string, std::set<std::string>> routes_of_stop_;
};

int main(int argc, char** argv)
{
  nlohmann::json routes_json = queryAndParse("https://api-v3.mbta.com/routes?filter[type]=0,1");

  // gathering and structuring data for questions 2 and 3
  std::vector<std::string> route_ids = getRouteIDs(routes_json);
  std::string route_query_prefix = "https://api-v3.mbta.com/stops?filter[route]=";

  std::unordered_map<std::string, std::vector<std::string>> adjacency_lists;
  std::unordered_map<std::string, std::set<std::string>> routes_of_stop;

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
      routes_of_stop[stop_name].insert(route_name);

    // build adjacency lists
    for (int i = 0; i < cur_route_stops.size(); i++)
    {
      if (i > 0)
        adjacency_lists[cur_route_stops[i]].push_back(cur_route_stops[i-1]);
      if (i < cur_route_stops.size() - 1)
        adjacency_lists[cur_route_stops[i]].push_back(cur_route_stops[i+1]);
    }
  }

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

  // question 3
  RoutePlanner planner(adjacency_lists, routes_of_stop);
  std::cout << "================================================\n\n"
            << "Now we'll plan some routes!\n";
  while (true)
  {
    std::cout << "Enter 'from' station: " << std::flush;
    std::string from_stop;
    std::getline(std::cin, from_stop);
    std::cout << "Enter 'to' station: " << std::flush;
    std::string to_stop;
    std::getline(std::cin, to_stop);

    if (from_stop == to_stop)
    {
      std::cout << "If you're already there, then there's nowhere to go!" << std::endl;
      continue;
    }

    std::vector<std::string> routes_to_travel = planner.plotRouteFromTo(from_stop, to_stop);
    std::cout << from_stop << " to " << to_stop << " -> ";
    for (std::string route : routes_to_travel)
      std::cout << route << ", ";
    std::cout << std::endl;
  }
  return 0;
}
