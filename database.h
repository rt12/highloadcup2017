#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <map>

#include <rapidjson/document.h>
#include <boost/utility/string_ref.hpp>

#include "hybridhash.h"

struct User
{
    uint32_t id = 0;
    std::string email;
    std::string first_name;
    std::string last_name;
    int32_t birth_date;
    char gender;

    bool load(const rapidjson::Value& v);
    void store(rapidjson::Value& v, rapidjson::Document& d) const;
};

struct Location
{
    uint32_t id = 0;
    std::string place;
    std::string country;
    std::string city;
    uint32_t distance;

    bool load(const rapidjson::Value& v);
    void store(rapidjson::Value & v, rapidjson::Document& d) const;
};

struct Visit
{
    uint32_t id = 0;
    uint32_t location = 0;
    uint32_t user = 0;
    uint32_t visited_at;
    uint8_t mark;

    bool load(const rapidjson::Value& v);
    void store(rapidjson::Value & v, rapidjson::Document& d) const;
};

struct VisitsQuery
{
    VisitsQuery() 
        : fromDate(0), toDate(0), toDistance(0) {}

    uint32_t fromDate;
    uint32_t toDate;
    std::string country;
    uint32_t toDistance;
};

struct AverageQuery
{
    AverageQuery() 
        : fromDate(0), toDate(0), fromAge(0), toAge(0), gender(0) {}

    uint32_t fromDate;
    uint32_t toDate;
    uint32_t fromAge;
    uint32_t toAge;
    char gender;
};

struct UserVisits;
struct LocationVisits;

struct VisitWrap {
    Visit entity;
    
    void getJson(boost::string_ref& ref) const;

    UserVisits* user;
    LocationVisits* location;
};

struct UserVisit
{
    uint8_t mark;
    uint32_t visited_at;
    boost::string_ref place;

    void store(rapidjson::Value & v, rapidjson::Document& d);
};


struct OrderedVisits
{
    typedef std::vector<VisitWrap*> StorageT;
    StorageT visits;
    
    StorageT::iterator begin() { return visits.begin(); }
    StorageT::iterator end() { return visits.end(); }
    StorageT::iterator lower_bound(uint32_t visited_at);

    void add(VisitWrap* v);
    void normalize();
    void remove(uint32_t visit_id);
};

struct UserVisits
{
    User entity;
    std::string json;

    void getJson(boost::string_ref& ref) const {
        ref = json;
    }

    // ordered by visited_at
    OrderedVisits visits;
};


struct LocationVisits
{
    Location entity;
    std::string json;

    void getJson(boost::string_ref& ref) const {
        ref = json;
    }

    // ordered by visited_at
    OrderedVisits visits;
};

class Database
{
public:

    enum class UpdateResult {
        ok,
        notFound,
        badData
    };
 
    Database();
    ~Database();
    
    void setNow(uint32_t timestamp);
    void reserve(bool fullRun);
    void printStat();

    bool get(uint32_t id, User& user);
    bool get(uint32_t id, Location& location);
    bool get(uint32_t id, Visit& visit);

    bool getUser(uint32_t id, boost::string_ref& res);
    bool getLocation(uint32_t id, boost::string_ref& res);
    bool getVisit(uint32_t id, boost::string_ref& res);

    UpdateResult updateUser(uint32_t id, const rapidjson::Value& v);
    UpdateResult updateLocation(uint32_t id, const rapidjson::Value& v);
    UpdateResult updateVisit(uint32_t id, const rapidjson::Value& v);

    bool create(const User& user);
    bool create(const Location& location);
    bool create(const Visit& visit);

    bool getVisits(uint32_t user, const VisitsQuery& q, std::vector<UserVisit>& visits);
    bool getAverage(uint32_t location, const AverageQuery& q, double& avg);

private:

    HybridHash<UserVisits> d_users;
    HybridHash<LocationVisits> d_locations;
    HybridHash<VisitWrap> d_visits;

    uint32_t d_now;
};

