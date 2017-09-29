#pragma once

#include "database.h"
#include <rapidjson/document.h>

class Loader
{
public:
    Loader(Database& db);
    ~Loader();

    bool loadFile(const std::string& filename);
    void loadDirectory(const std::string& dir);

private:

    void loadAll(const std::string& basePattern);

    bool parseLocations(const rapidjson::Value& v);
    bool parseUsers(const rapidjson::Value& v);
    bool parseVisits(const rapidjson::Value& v);

    Database& d_db;
};

