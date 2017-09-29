#include "handler.h"
#include "database.h"

#include <iostream>

#include <boost/algorithm/string.hpp>
#include <boost/utility/string_ref.hpp>
#include <boost/tokenizer.hpp>
#include <boost/token_iterator.hpp>

#include <rapidjson/document.h>
#include <rapidjson/writer.h>

using boost::string_ref;

template <typename Iter>
int percent_decode(string_ref input, Iter out)
{
    static const char tbl[256] = {
        -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
        0, 1, 2, 3, 4, 5, 6, 7,  8, 9,-1,-1,-1,-1,-1,-1,
        -1,10,11,12,13,14,15,-1, -1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
        -1,10,11,12,13,14,15,-1, -1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1,
        -1,-1,-1,-1,-1,-1,-1,-1, -1,-1,-1,-1,-1,-1,-1,-1
    };

    char c, v1, v2;
    Iter beg = out;

    for (auto in = input.begin(); in != input.end();) {
        char c = *in++;
        if (c == '%') {
            if (!(v1 = *in++) || (v1 = tbl[(unsigned char)v1]) < 0 ||
                !(v2 = *in++) || (v2 = tbl[(unsigned char)v2]) < 0) {
                return -1;
            }
            c = (v1 << 4) | v2;
        } else if (c == '+') {
            c = ' ';
        }

        *out++ = c;
    }

    return 0;
}

std::pair<string_ref, string_ref> splitPair(boost::string_ref s, const char delim)
{
    size_t p = s.find_first_of(delim);

    if (p == string_ref::npos) {
        return std::make_pair(s, string_ref());
    }

    return std::make_pair(s.substr(0, p), s.substr(p + 1));
}

template <typename Handler>
bool split(boost::string_ref s, const char delim, Handler f) {
    size_t p;
    while (true) {
        p = s.find_first_of(delim);
        if (!f(s.substr(0, p)))
            return false;

        if (p == boost::string_ref::npos) {
            break;
        }
        s.remove_prefix(p + 1);
    }

    return true;
}

bool parseUint(uint32_t& v, string_ref input)
{
    if (!std::all_of(input.begin(), input.end(), ::isdigit)) {
        return false;
    }

    return sscanf(input.data(), "%u", &v) == 1;
}

bool parseVisitQuery(string_ref query, VisitsQuery& vq)
{
    return split(query, '&', [&vq](string_ref param) {
        auto kv = splitPair(param, '=');

        if (kv.first == "toDate")
            return parseUint(vq.toDate, kv.second);
        if (kv.first == "fromDate")
            return parseUint(vq.fromDate, kv.second);
        if (kv.first == "toDistance")
            return parseUint(vq.toDistance, kv.second);
        if (kv.first == "country") {
            vq.country.reserve(kv.second.size());
            return percent_decode(kv.second, std::back_inserter(vq.country)) == 0;
        }

        return true;
    });
}

bool parseAverageQuery(string_ref query, AverageQuery& aq)
{
    return split(query, '&', [&aq](string_ref param) {
        auto kv = splitPair(param, '=');

        if (kv.first == "toDate")
            return parseUint(aq.toDate, kv.second);
        if (kv.first == "fromDate")
            return parseUint(aq.fromDate, kv.second);
        if (kv.first == "toAge")
            return parseUint(aq.toAge, kv.second);
        if (kv.first == "fromAge")
            return parseUint(aq.fromAge, kv.second);

        if (kv.first == "gender") {
            if (kv.second.size() != 1)
                return false;
            char g = kv.second[0];
            if (g != 'm' && g != 'f')
                return false;
            aq.gender = g;
            return true;
        }

        return true;
    });

}

string_ref strNew("new");
string_ref strUsers("users");
string_ref strLocations("locations");
string_ref strVisits("visits");
string_ref strAvg("avg");

Handler::Entity getEntityId(const boost::string_ref& entity)
{
    if (entity == strVisits)
        return Handler::Entity::Visit;
    if (entity == strUsers)
        return Handler::Entity::User;
    if (entity == strLocations)
        return Handler::Entity::Location;

    return Handler::Entity::Unknown;
}


Handler::Handler(Database& db)
    : d_db(db)
{
}

Handler::~Handler()
{
}

int Handler::handle(
    Method method, 
    const std::string& body,
    const std::string& path, 
    const std::string& query, 
    Response& response)
{   
    boost::string_ref parts[4];
    size_t partCount = 0;

    bool sr = split(path, '/', [&](string_ref part) {
        parts[partCount++] = part;
        if (partCount > 4) {
            return false;
        }
        return true;
    });

    if (partCount < 3) {
        return 400;
    }

    const auto& entity = parts[1];
    const auto& idStr = parts[2];

    Entity entityId = getEntityId(entity);
    if (entityId == Entity::Unknown) {
        return 400;
    }

    if (method == Method::POST && idStr == strNew) {
        std::lock_guard<std::mutex> lk(d_mutex);

        int result = 400;

        switch (entityId) {
        case Handler::Entity::User:
            return createEntity<User>(body, response);
        case Handler::Entity::Location:
            return createEntity<Location>(body, response);
        case Handler::Entity::Visit:
            return createEntity<Visit>(body, response);
        default:
            return 400;
        }
    }

    int id = atoi(idStr.data());

    if (partCount == 3) {
        // GET /entity/id
        if (method == Method::GET) {
            bool found = false;

            boost::string_ref result(
                    response.dataBuf.data(),
                    response.dataBuf.size());

            switch (entityId) {
            case Handler::Entity::User:
                found = d_db.getUser(id, result);
                break;
            case Handler::Entity::Location:
                found = d_db.getLocation(id, result);
                break;
            case Handler::Entity::Visit:
                found = d_db.getVisit(id, result);
                break;
            default:
                return 400;
            }

            if (found) {
                response.dataRef = result;
                return 200;
            }

            return 404;

        } else if (method == Method::POST) {
            rapidjson::Document d;
            if (d.Parse(body.data(), body.length()).HasParseError())
                return 400;

            Database::UpdateResult result = Database::UpdateResult::badData;

            {
                std::lock_guard<std::mutex> lk(d_mutex);
                switch (entityId) {
                case Handler::Entity::User:
                    result = d_db.updateUser(id, d);
                    break;
                case Handler::Entity::Location:
                    result = d_db.updateLocation(id, d);
                    break;
                case Handler::Entity::Visit:
                    result = d_db.updateVisit(id, d);
                    break;
                default:
                    return 400;
                }
            }

            if (result == Database::UpdateResult::ok) {
                response.dataRef = "{}";
            }

            switch (result) {
            case Database::UpdateResult::ok:
                return 200;
            case Database::UpdateResult::badData:
                return 400;
            case Database::UpdateResult::notFound:
                return 404;
            }

            return 400;
        }

    }

    if (parts[3] == strVisits && entityId == Entity::User) {
        return getVisits(id, query, response);
    }

    if (parts[3] == strAvg && entityId == Entity::Location) {
        return getAverage(id, query, response);
    }

   
    return 400;
}

template <typename T>
int Handler::createEntity(const std::string& json, Response& response)
{
    rapidjson::Document d;
    d.Parse(json.data(), json.length());
    
    T entity;
    if (!entity.load(d))
        return 400;

    d_db.create(entity);
    response.dataRef = "{}";

    return 200;
}

int Handler::getAverage(uint32_t id, const std::string& query, Response& res)
{
    AverageQuery aq;
    if (!parseAverageQuery(query, aq))
        return 400;

    double avg = 0;

    if (!d_db.getAverage(id, aq, avg))
        return 404;

    // round to 5 places
    avg = round(avg * 100000) / 100000.0;

    int bufused = snprintf(
            res.dataBuf.data(),
            res.dataBuf.size(),
            "{ \"avg\": %.5f }", avg);

    res.useDataBuf(bufused);

    return 200;
}


static boost::string_ref visitRespPrefix("{ \"visits\" : [");
static boost::string_ref visitRespSuffix("]}");

char* escape_json(char *p, const boost::string_ref& s) {
    for (auto c : s) {
        if (c == '"' || c == '\\' || (c >= 0x00 && c <= 0x1f)) {
            p += sprintf(p, "\\u%04x", int(c));
        } else {
            *p++ = c;
        }
    }

    return p;
}

int Handler::getVisits(uint32_t id, const std::string& query, Response& res)
{
    VisitsQuery vq;
    std::vector<UserVisit> visits;

    if (!parseVisitQuery(query, vq)) {
        return 400;
    }

    if (!d_db.getVisits(id, vq, visits)) {
        return 404;
    }

    char *p = res.dataBuf.data();
    size_t bufleft = res.dataBuf.size();
    char *end = p + bufleft;

    memcpy(p, visitRespPrefix.data(), visitRespPrefix.size());
    p += visitRespPrefix.size();

    for(const auto& v: visits) {
        if (v.place.size()*5 + 80 > size_t(end - p)) {
            fprintf(stderr, "Response buffer overflow!\n");
            break;
        }

        p += sprintf(p, "{ \"mark\": %u, \"visited_at\": %u, \"place\": \"",
                v.mark, v.visited_at);
        p = escape_json(p, v.place);
        p += sprintf(p, "\"},");
    }

    if (!visits.empty())
        --p; // overwrite last ','

    memcpy(p, visitRespSuffix.data(), visitRespSuffix.size());
    p += visitRespSuffix.size();
    p[1] = 0;

    res.useDataBuf(p - res.dataBuf.data());
    return 200;
}

