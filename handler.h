#pragma once

#include <string>
#include <mutex>
#include <boost/utility/string_ref.hpp>
#include <rapidjson/stringbuffer.h>

class Database;

enum class HttpStatus
{
    invalid = 0,
    ok = 200,
    created = 201,
    accepted = 202,
    no_content = 204,
    multiple_choices = 300,
    moved_permanently = 301,
    moved_temporarily = 302,
    not_modified = 304,
    bad_request = 400,
    unauthorized = 401,
    forbidden = 403,
    not_found = 404,
    internal_server_error = 500,
    not_implemented = 501,
    bad_gateway = 502,
    service_unavailable = 503
};

struct Response {

    HttpStatus code = HttpStatus::invalid;
    boost::string_ref contentType;
    boost::string_ref dataRef;
    std::array<char, 4096*4> dataBuf;

    Response() {
        clear();
    }

    void clear()
    {
        contentType = "application/octet-stream";
        code = HttpStatus::invalid;
        dataRef.clear();
    }

    bool valid() const
    {
        return code != HttpStatus::invalid;
    }

    void useDataBuf(size_t size)
    {
        dataRef = boost::string_ref(dataBuf.data(), size);
    }

    const char* data() const
    {
        return dataRef.data();
    }

    size_t size() const {
        return dataRef.size();
    }

    void setContentJson()
    {
        contentType = "application/json; charset=UTF-8";
    }
};

class Handler
{
public:

    enum class Method {
        GET,
        POST
    };

    enum class Entity {
        Unknown,
        User,
        Location,
        Visit
    };

    Handler(Database& db);
    ~Handler();

    int handle(Method method, 
        const std::string& body,
        const std::string& path, 
        const std::string& query, 
        Response& response);

private:

    template <typename T>
    int createEntity(const std::string& json, Response& response);

    int getAverage(uint32_t id, const std::string& query, Response& response);
    int getVisits(uint32_t id, const std::string& query, Response& response);

    Database& d_db;
    std::mutex d_mutex;
};
