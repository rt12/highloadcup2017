#include "loader.h"

#include <fstream>
#include <iostream>
#include <future>

#include <rapidjson/document.h>
#include <rapidjson/filereadstream.h>
#include <rapidjson/istreamwrapper.h>

#include <sys/time.h>
#include <sys/resource.h>

void printMemStat()
{
    rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    printf("Max rss: %zu MB\n", ru.ru_maxrss/1024);
}

Loader::Loader(Database & db)
: d_db(db)
{
}

Loader::~Loader()
{
}

void Loader::loadDirectory(const std::string& dir)
{
    printMemStat();
    auto users = std::async(std::launch::async, [this, dir] { loadAll(dir + "/users_"); });
    auto locations = std::async(std::launch::async, [this, dir] { loadAll(dir + "/locations_"); });

    users.get();
    locations.get();

    printMemStat();

    loadAll(dir + "/visits_");
    printMemStat();

    d_db.printStat();
}


void Loader::loadAll(const std::string& basePattern)
{
    std::cout << "Loading pattern " << basePattern << std::endl;
    int i = 1;
    while (loadFile(basePattern + std::to_string(i) + ".json"))
        ++i;
}

bool Loader::loadFile(const std::string & filename)
{
    using namespace rapidjson;

    std::ifstream ifs(filename);

    if (!ifs) {
        return false;
    }

    IStreamWrapper isw(ifs);
    Document d;
    d.ParseStream(isw);

    if (d.HasMember("locations")) {
        if (!parseLocations(d["locations"])) {
            return false;
        }
    } 
    
    if (d.HasMember("visits")) {
        if (!parseVisits(d["visits"])) {
            return false;
        }
    } 
    
    if (d.HasMember("users")) {
        if (!parseUsers(d["users"])) {
            return false;
        }
    }

    return true;
}

template <typename T>
bool loadItems(Database& db, const rapidjson::Value& v)
{
    for (const auto& item : v.GetArray()) {
        T entity;
        entity.load(item);
        db.create(entity);
    }

    return true;
}

bool Loader::parseLocations(const rapidjson::Value& v)
{
    return loadItems<Location>(d_db, v);
}

bool Loader::parseUsers(const rapidjson::Value& v)
{
    return loadItems<User>(d_db, v);
}

bool Loader::parseVisits(const rapidjson::Value& v)
{
    return loadItems<Visit>(d_db, v);
}
