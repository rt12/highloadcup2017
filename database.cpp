#include "database.h"

#include <algorithm>
#include <iostream>
#include <time.h>
#include <stdio.h>

#include <boost/date_time/gregorian/gregorian.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

using rapidjson::StringRef;
using rapidjson::Value;

template <typename T>
std::string toJson(const T& entity)
{
    rapidjson::Document d;
    entity.store(d, d);

    rapidjson::StringBuffer buf;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buf);
    d.Accept(writer);

    return std::string(buf.GetString(), buf.GetSize());
}

Database::Database()
{
    d_now = time(0);
}

Database::~Database()
{
}

void Database::setNow(uint32_t now)
{
    d_now = now;
}

void Database::reserve(bool fullRun)
{
    if (fullRun) {
        d_users.reserve(1000074UL * 110/100);
        d_locations.reserve(761314UL * 110/100);
        d_visits.reserve(10000740UL * 110/100);
    } else {
        d_users.reserve(5000);
        d_locations.reserve(5000);
        d_visits.reserve(110000);
    }
}

void Database::printStat()
{
    std::cout << "Users: " << d_users.size() << std::endl;
    std::cout << "Locations: " << d_locations.size() << std::endl;
    std::cout << "Visits: " << d_visits.size() << std::endl;
}

template <typename T>
bool loadInt(T& dest, const rapidjson::Value& v, const char* name)
{
    if (v.HasMember(name)) {
        const Value& item = v[name];
        if (!item.IsInt()) {
            return false;
        }
        dest = item.GetInt();
    }

    return true;
}

bool loadString(std::string& dest, const rapidjson::Value& v, const char* name)
{
    if (v.HasMember(name)) {
        const Value& item = v[name];
        if (!item.IsString()) {
            return false;
        }
        dest = item.GetString();
    }

    return true;
}

#define LOAD_UINT_VALUE(name) if(!loadInt(name, v, #name)) { return false; }
#define LOAD_STRING_VALUE(name) if(!loadString(name, v, #name)) { return false; }

bool User::load(const rapidjson::Value & v)
{
    LOAD_UINT_VALUE(id);
    LOAD_STRING_VALUE(first_name);
    LOAD_STRING_VALUE(last_name);
    LOAD_STRING_VALUE(email);
    LOAD_UINT_VALUE(birth_date);

    if (v.HasMember("gender")) {
        const auto& item = v["gender"];
        if (!item.IsString() || item.GetStringLength() != 1)
            return false;
        gender = item.GetString()[0];
    }

    return true;
}

void User::store(rapidjson::Value& v, rapidjson::Document& d) const
{
    v.SetObject();

    auto& a = d.GetAllocator();

    v.AddMember("id", Value(id), a);
    v.AddMember("email", StringRef(email.data(), email.size()), a);
    v.AddMember("first_name", StringRef(first_name.data(), first_name.size()), a);
    v.AddMember("last_name", StringRef(last_name.data(), last_name.size()), a);
    v.AddMember("gender", StringRef(&gender, 1), a);
    v.AddMember("birth_date", birth_date, a);
}

bool Location::load(const rapidjson::Value & v)
{
    LOAD_UINT_VALUE(id);
    LOAD_STRING_VALUE(place);
    LOAD_STRING_VALUE(country);
    LOAD_STRING_VALUE(city);
    LOAD_UINT_VALUE(distance);

    return true;
}

void Location::store(rapidjson::Value & v, rapidjson::Document& d) const
{
    auto& a = d.GetAllocator();
    v.SetObject();
    v.AddMember("id", Value(id), a);
    v.AddMember("place", StringRef(place.data(), place.size()), a);
    v.AddMember("country", StringRef(country.c_str(), country.size()), a);
    v.AddMember("city", StringRef(city.c_str(), city.size()), a);
    v.AddMember("distance", distance, a);
}


bool Visit::load(const rapidjson::Value & v)
{
    LOAD_UINT_VALUE(id);
    LOAD_UINT_VALUE(location);
    LOAD_UINT_VALUE(user);
    LOAD_UINT_VALUE(visited_at);
    LOAD_UINT_VALUE(mark);

    return true;
}

void Visit::store(rapidjson::Value & v, rapidjson::Document& d) const
{
    auto& a = d.GetAllocator();
    v.SetObject();
    v.AddMember("id", Value(id), a);
    v.AddMember("location", location, a);
    v.AddMember("user", user, a);
    v.AddMember("visited_at", int32_t(visited_at), a);
    v.AddMember("mark", int(mark), a);
}

void UserVisit::store(rapidjson::Value & v, rapidjson::Document& d)
{
    auto& a = d.GetAllocator();
    v.SetObject();

    v.AddMember("mark", int(mark), a);
    v.AddMember("visited_at", visited_at, a);
    v.AddMember("place", StringRef(place.begin(), place.size()), a);
}


bool visitedTimeLess(const VisitWrap* a, const VisitWrap* b)
{
    return a->entity.visited_at < b->entity.visited_at;
}

OrderedVisits::StorageT::iterator OrderedVisits::lower_bound(uint32_t visited_at)
{
    VisitWrap vw;
    vw.entity.visited_at = visited_at;

    return std::lower_bound(visits.begin(), visits.end(), &vw, visitedTimeLess);
}

void OrderedVisits::add(VisitWrap* v)
{
    visits.push_back(v);
    normalize();
}

void OrderedVisits::normalize()
{
    visits.shrink_to_fit();
    std::sort(visits.begin(), visits.end(), visitedTimeLess);
}

void OrderedVisits::remove(uint32_t visit_id)
{
    for (auto it = visits.begin(); it != visits.end(); ++it) {
        if ((*it)->entity.id == visit_id) {
            visits.erase(it);
            return;
        }
    }
}

void VisitWrap::getJson(boost::string_ref& ref) const {
    if (ref.size() < 80)
        return;

    int sz = snprintf((char*)ref.data(), ref.size(),
            "{\"user\": %u, \"location\": %u, \"visited_at\": %u, \"id\": %u, \"mark\": %u}",
            entity.user, 
            entity.location, 
            entity.visited_at, 
            entity.id, 
            entity.mark);

    ref = boost::string_ref(ref.data(), sz);
}


template <typename MapT, typename T>
bool getEntity(MapT& m, uint32_t id, T& value)
{
    auto it = m.find(id);
    if (it == m.end())
        return false;

    value = it->entity;
    return true;
}

template <typename MapT>
bool getEntityJson(MapT& m, uint32_t id, boost::string_ref& value)
{
    auto it = m.find(id);
    if (it == m.end())
        return false;

    it->getJson(value);
    return true;
}

template <typename Entity, typename MapT>
Database::UpdateResult updateEntity(MapT& m, uint32_t id, const rapidjson::Value & v)
{
    auto it = m.find(id);
    if (it == m.end())
        return Database::UpdateResult::notFound;

    auto& holder = *it;
    Entity item(holder.entity);

    if (!item.load(v))
        return Database::UpdateResult::badData;

    holder.json = toJson(item);
    holder.entity = std::move(item);
    return Database::UpdateResult::ok;
}

bool Database::getUser(uint32_t id, boost::string_ref& res)
{
    return getEntityJson(d_users, id, res);
}

bool Database::getLocation(uint32_t id, boost::string_ref& res)
{
    return getEntityJson(d_locations, id, res);
}

bool Database::getVisit(uint32_t id, boost::string_ref& res)
{
    return getEntityJson(d_visits, id, res);
}

bool Database::get(uint32_t id, User& user)
{
    return getEntity(d_users, id, user);
}

bool Database::get(uint32_t id, Location& location)
{
    return getEntity(d_locations, id, location);
}

bool Database::get(uint32_t id, Visit& visit)
{
    return getEntity(d_visits, id, visit);
}

Database::UpdateResult Database::updateUser(uint32_t id, const rapidjson::Value& v)
{
    return updateEntity<User>(d_users, id, v);
}

Database::UpdateResult Database::updateLocation(uint32_t id, const rapidjson::Value& v)
{
    return updateEntity<Location>(d_locations, id, v);
}

Database::UpdateResult Database::updateVisit(uint32_t id, const rapidjson::Value& v)
{
    auto it = d_visits.find(id);
    if (it == d_visits.end())
        return UpdateResult::notFound;

    VisitWrap& vw = *it;
    Visit oldValue = vw.entity;
    Visit newValue(oldValue);
    
    if (!newValue.load(v))
        return UpdateResult::badData;

    auto userIt = d_users.find(newValue.user);
    if (userIt == d_users.end())
        return UpdateResult::badData;

    auto locationIt = d_locations.find(newValue.location);
    if (locationIt == d_locations.end())
        return UpdateResult::badData;

    // overwrite value in the db
    vw.entity = newValue;

    if (oldValue.user != newValue.user) {
        vw.user->visits.remove(oldValue.id);
        vw.user = userIt;
        vw.user->visits.add(&vw);
    }

    if (oldValue.location != newValue.location) {
        vw.location->visits.remove(oldValue.id);
        vw.location = locationIt;
        vw.location->visits.add(&vw);
    }

    if (oldValue.visited_at != newValue.visited_at) {
        vw.location->visits.normalize();
        vw.user->visits.normalize();
    }

    return UpdateResult::ok;
}

bool Database::create(const User & user)
{
    auto& item = d_users[user.id];
    item.entity = user;
    item.json = toJson(user);
   
    return true;
}

bool Database::create(const Location& location)
{
    auto& item = d_locations[location.id];
    item.entity = location;
    item.json = toJson(location);

    return true;
}

bool Database::create(const Visit & visit)
{
    VisitWrap& dest = d_visits[visit.id];

    auto& lv = d_locations[visit.location];
    auto& uv = d_users[visit.user];

    dest.entity = visit;
    dest.location = &lv;
    dest.user = &uv;

    uv.visits.add(&dest);
    lv.visits.add(&dest);

    return true;
}

bool Database::getVisits(uint32_t user, const VisitsQuery& q, std::vector<UserVisit>& visits)
{
    const auto it = d_users.find(user);
    if (it == d_users.end()) {
        return false;
    }

    auto& userVisits = it->visits;
    auto vi = q.fromDate ? userVisits.lower_bound(q.fromDate) : userVisits.begin();

    for (; vi != userVisits.end(); ++vi) {
        const auto& v = *vi;

        if (q.toDate && v->entity.visited_at >= q.toDate) {
            break;
        }

        if (q.toDistance && v->location->entity.distance >= q.toDistance) {
            continue;
        }

        if (!q.country.empty() && q.country != v->location->entity.country) {
            continue;
        }

        visits.emplace_back(UserVisit{v->entity.mark, v->entity.visited_at, v->location->entity.place});
    }

    return true;
}


bool Database::getAverage(uint32_t location, const AverageQuery& q, double& avg)
{
    avg = 0;
    double sum = 0.0;
    size_t count = 0;

    auto locIt = d_locations.find(location);
    if (locIt == d_locations.end()) {
        return false;
    }

    bool ageFilter = false;
    boost::gregorian::date fromBirth = boost::gregorian::date(boost::gregorian::special_values::min_date_time);
    boost::gregorian::date toBirth = boost::gregorian::date(boost::gregorian::special_values::max_date_time);
    boost::gregorian::date epoch(1970, 1, 1);

    if (q.fromAge || q.toAge) {
        auto today = boost::posix_time::from_time_t(d_now).date();

        if (q.fromAge) {
            toBirth = today - boost::gregorian::years(q.fromAge);
        }

        if (q.toAge) {
            fromBirth = today - boost::gregorian::years(q.toAge);
        }

        ageFilter = true;
    }

    LocationVisits& lv = *locIt;
    auto vi = q.fromDate ? lv.visits.lower_bound(q.fromDate) : lv.visits.begin();

    for (; vi != lv.visits.end(); ++vi) {
        const auto& visit = *vi;
        if (q.toDate && visit->entity.visited_at > q.toDate)
            break;

        if (q.gender && visit->user->entity.gender != q.gender)
            continue;

        if (ageFilter) {
            auto userBirthdate = epoch + boost::gregorian::date_duration(visit->user->entity.birth_date / 60 / 60 / 24);
            if (userBirthdate < fromBirth || userBirthdate > toBirth)
                continue;
        }

        ++count;

        sum += visit->entity.mark;
    }

    if (count)
        avg =sum / count;

    return true;
}

