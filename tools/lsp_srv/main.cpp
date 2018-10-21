//
// Created by sekiguchi nagisa on 2018/10/09.
//
#include "json.h"

template <typename T>
void showSize(const char *name) {
    printf("sizeof `%s' %lu\n", name, sizeof(T));
}

struct Info {
    unsigned int a;
    unsigned int b;
    unsigned int c;
};

struct Info2 {
    Info info;
    unsigned int k;
};

struct Info3 : public Info {
    unsigned int t;
};

struct Info4 {
    unsigned char c;
    Info info;
};


int main(void) {
    showSize<json::JSON>("JSON");
    showSize<json::Array>("Array");
    showSize<json::Object>("Object");
    showSize<std::string>("std::string");
    showSize<json::String>("String");
    showSize<Info>("Info");
    showSize<Info2>("Info2");
    showSize<Info3>("Info3");
    showSize<Info4>("Info4");
    showSize<std::vector<json::JSON>>("std::vector<json::JSON>");
    showSize<std::map<std::string, json::JSON>>("std::map<std::string, json::JSON>");
    showSize<ydsh::Union<std::string, double >>("ydsh::Union<std::string, double >");

//    printf("\\u%04x\n", 28);

    json::JSON value = {
            {"hello", false},
            {"aaaa", json::array()},
            {"123456" , json::array(34, "gare", nullptr)},
            {"AA", {
                        {"BBB", false}, {"", nullptr}
                    }
            }
    };
    printf("%s", json::Parser(value.serialize(2).c_str())().serialize(2).c_str());
//    printf("")
    return 0;
}
