
#include <assert.h>

#include <regex>
#include <filesystem>

#include "configuration.h"

namespace conf {

typedef std::unordered_map<string, string> IniSection;

std::unordered_map<string, IniSection> sections;

void insert_item(string sec, string name, string value) {
    auto res1 = sections.find(sec);
    if(res1 == sections.end()) {
        sections.insert(std::make_pair(sec, IniSection()));
        res1 = sections.find(sec);
    }
    IniSection &section = res1->second;
    auto res2 = section.find(name);
    if(res2 == section.end()) {
        section.insert(std::make_pair(name, value));
    }
    else {
        res2->second = value;
    }
}

string remove_space(string s) {
    uint64_t i = 0;
    while(i < s.length() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n')) {
        i++;
    }
    if(i == s.length()) return "";
    s = s.substr(i, s.length() - i);
    while(s.length() > 0 && (s.back() == ' ' || s.back() == '\t' || s.back() == '\n')) {
        s.pop_back();
    }
    return s;
}

void load_ini_file(string filepath) {
    std::filesystem::path fpath(filepath);
    if(fpath.is_relative()) {
        fpath = std::filesystem::canonical(fpath);
    }
    std::filesystem::path basedir = fpath.parent_path();

    std::cout << "Load config file: " << fpath.string() << std::endl;

    std::ifstream ifile(filepath);
    string line = "";
    string current_section = "";
    std::regex reg_is_empty("[ \t\n]*");
    std::regex reg_is_include("#include[ \t]+\".*\"");
    std::regex reg_is_comment(";.*");
    std::regex reg_is_sextion(" *\\[\\w*\\] *");
    std::regex reg_is_item(".*=.*");
    while(std::getline(ifile, line)) {
        if(line.length() < 3 || std::regex_match(line, reg_is_empty)) continue;
        else if(std::regex_match(line, reg_is_include)) {
            uint64_t pos1 = line.find('\"');
            uint64_t pos2 = line.find('\"', pos1 + 1);
            std::filesystem::path fpath2(line.substr(pos1 + 1, pos2 - pos1 - 1));
            if(fpath2.is_relative()) {
                fpath2 = basedir / fpath2;
            }
            load_ini_file(fpath2.c_str());
        }
        else if(std::regex_match(line, reg_is_comment)) continue;
        else if(std::regex_match(line, reg_is_sextion)) {
            uint64_t pos1 = line.find('[');
            uint64_t pos2 = line.find(']');
            current_section = remove_space(line.substr(pos1 + 1, pos2 - pos1 - 1));
        }
        else if(std::regex_match(line, reg_is_item)) {
            uint64_t pos1 = line.find('=');
            string key = remove_space(line.substr(0, pos1));
            string value = remove_space(line.substr(pos1 + 1));
            insert_item(current_section, key, value);
        }
        else {
            LOG(ERROR) << "Failed to parse ini file " << filepath;
            LOG(ERROR) << "Line: " << line;
            exit(0);
        }
    }
}

int64_t get_int(string sec, string name, int64_t def) {
    string ret = get_str(sec, name, "x");
    try {
        return std::stoll(ret);
    } catch(...) {
        return def;
    }
}

float get_float(string sec, string name, float def) {
    string ret = get_str(sec, name, "x");
    try {
        return std::stof(ret);
    } catch(...) {
        return def;
    }
}

string get_str(string sec, string name, string def) {
    auto res1 = sections.find(sec);
    if(res1 == sections.end()) {
        return def;
    }
    IniSection &section = res1->second;
    auto res2 = section.find(name);
    if(res2 == section.end()) {
        return def;
    }
    return res2->second;
}

}

namespace test {

bool test_ini_file() {
    conf::load_ini_file("../conf/default.ini");
    assert(conf::get_int("cpu", "icache_way_count", 0));
    assert(conf::get_int("cpu", "unknown_item", 12345) == 12345);
    assert(conf::get_int("bus", "log_info_to_stdout", 2) != 2);
    assert(conf::get_int("sys", "log_info_to_stdout", 2) != 2);
    printf("Pass\n");
    return true;
}

}
