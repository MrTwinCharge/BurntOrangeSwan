#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <filesystem>
#include <vector>
#include <algorithm>

namespace fs = std::filesystem;

static std::string read_file(const std::string& path) {
    std::ifstream f(path);
    if (!f) return "";
    std::stringstream buf;
    buf << f.rdbuf();
    return buf.str();
}

static std::string extract_json_string(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\":\"";
    auto pos = json.find(needle);
    if (pos == std::string::npos) {
        needle = "\"" + key + "\":";
        pos = json.find(needle);
        if (pos == std::string::npos) return "";
        pos += needle.size();
        auto end = json.find_first_of(",}", pos);
        return json.substr(pos, end - pos);
    }

    pos += needle.size();
    std::string result;
    result.reserve(json.size() - pos);
    for (size_t i = pos; i < json.size(); i++) {
        if (json[i] == '\\' && i + 1 < json.size()) {
            char next = json[i + 1];
            if (next == 'n') { result += '\n'; i++; }
            else if (next == 't') { result += '\t'; i++; }
            else if (next == '"') { result += '"'; i++; }
            else if (next == '\\') { result += '\\'; i++; }
            else { result += next; i++; }
        }
        else if (json[i] == '"') {
            break;
        }
        else {
            result += json[i];
        }
    }
    return result;
}

bool parse_json_log(const std::string& path, const std::string& out_dir) {
    std::string content = read_file(path);
    if (content.empty()) return false;

    auto json_start = content.find('{');
    if (json_start == std::string::npos) return false;
    std::string json = content.substr(json_start);

    std::string activities = extract_json_string(json, "activitiesLog");
    if (activities.empty() || activities.find("day;timestamp;product") == std::string::npos) {
        return false;
    }

    std::string round_str = extract_json_string(json, "round");
    int round_num = 0;
    try { round_num = std::stoi(round_str); } catch (...) {}

    std::string profit_str = extract_json_string(json, "profit");

    auto first_nl = activities.find('\n');
    std::string day = "-1";
    if (first_nl != std::string::npos && first_nl + 1 < activities.size()) {
        auto semi = activities.find(';', first_nl + 1);
        if (semi != std::string::npos) {
            day = activities.substr(first_nl + 1, semi - first_nl - 1);
        }
    }

    int rows = 0;
    std::vector<std::string> prods;
    std::istringstream stream(activities);
    std::string line;
    std::getline(stream, line);
    while (std::getline(stream, line)) {
        if (line.empty()) continue;
        rows++;
        auto s1 = line.find(';');
        auto s2 = line.find(';', s1 + 1);
        auto s3 = line.find(';', s2 + 1);
        if (s2 != std::string::npos && s3 != std::string::npos) {
            std::string prod = line.substr(s2 + 1, s3 - s2 - 1);
            if (std::find(prods.begin(), prods.end(), prod) == prods.end())
                prods.push_back(prod);
        }
    }

    std::string products_found;
    for (size_t i = 0; i < prods.size(); i++) {
        if (i > 0) products_found += ", ";
        products_found += prods[i];
    }

    std::string base = fs::path(path).stem().string();
    std::string out_name = "prices_round_" + std::to_string(round_num) +
                           "_day_" + day + "_log_" + base + ".csv";
    std::string out_path = out_dir + "/" + out_name;

    std::ofstream out(out_path);
    if (!out) {
        std::cerr << "  [err] Could not write: " << out_path << std::endl;
        return false;
    }
    out << activities;
    out.close();

    std::cout << "  [ok] " << out_name << ": " << rows << " rows"
              << ", products=[" << products_found << "]"
              << ", profit=" << profit_str << std::endl;
    return true;
}

int main() {
    std::string logs_dir = "../data/logs";
    std::string raw_dir = "../data/raw";

    if (!fs::exists(logs_dir)) {
        std::cerr << "[parse_logs] No data/logs/ directory found.\n"
                  << "  Create it and drop your Prosperity .json/.log files there." << std::endl;
        return 1;
    }

    fs::create_directories(raw_dir);

    std::vector<std::string> to_parse;
    for (const auto& entry : fs::directory_iterator(logs_dir)) {
        if (!entry.is_regular_file()) continue;
        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == ".json" || ext == ".log") {
            to_parse.push_back(entry.path().string());
        }
    }

    if (to_parse.empty()) {
        std::cout << "[parse_logs] No .json or .log files in data/logs/" << std::endl;
        return 0;
    }

    std::sort(to_parse.begin(), to_parse.end());
    std::cout << "[parse_logs] Found " << to_parse.size() << " log file(s) in data/logs/\n" << std::endl;

    int parsed = 0;
    for (const auto& path : to_parse) {
        std::string name = fs::path(path).filename().string();
        std::cout << "Parsing: " << name << std::endl;
        if (parse_json_log(path, raw_dir)) {
            parsed++;
        } else {
            std::cerr << "  [skip] Could not extract data from " << name << std::endl;
        }
    }

    std::cout << "\n[parse_logs] Parsed " << parsed << "/" << to_parse.size()
              << " → CSVs written to data/raw/" << std::endl;
    if (parsed > 0) {
        std::cout << "Next: ./translator && ./sweep" << std::endl;
    }

    return 0;
}