#include "connection.h"

namespace StatusStrings {

    const std::string ok =
        "HTTP/1.1 200 OK\r\n";
    const std::string created =
        "HTTP/1.1 201 Created\r\n";
    const std::string accepted =
        "HTTP/1.1 202 Accepted\r\n";
    const std::string no_content =
        "HTTP/1.1 204 No Content\r\n";
    const std::string multiple_choices =
        "HTTP/1.1 300 Multiple Choices\r\n";
    const std::string moved_permanently =
        "HTTP/1.1 301 Moved Permanently\r\n";
    const std::string moved_temporarily =
        "HTTP/1.1 302 Moved Temporarily\r\n";
    const std::string not_modified =
        "HTTP/1.1 304 Not Modified\r\n";
    const std::string bad_request =
        "HTTP/1.1 400 Bad Request\r\n";
    const std::string unauthorized =
        "HTTP/1.1 401 Unauthorized\r\n";
    const std::string forbidden =
        "HTTP/1.1 403 Forbidden\r\n";
    const std::string not_found =
        "HTTP/1.1 404 Not Found\r\n";
    const std::string internal_server_error =
        "HTTP/1.1 500 Internal Server Error\r\n";
    const std::string not_implemented =
        "HTTP/1.1 501 Not Implemented\r\n";
    const std::string bad_gateway =
        "HTTP/1.1 502 Bad Gateway\r\n";
    const std::string service_unavailable =
        "HTTP/1.1 503 Service Unavailable\r\n";

    boost::string_ref toStringRef(HttpStatus status)
    {
        switch (status) {
        case HttpStatus::ok:
            return boost::string_ref(ok);
        case HttpStatus::created:
            return boost::string_ref(created);
        case HttpStatus::accepted:
            return boost::string_ref(accepted);
        case HttpStatus::no_content:
            return boost::string_ref(no_content);
        case HttpStatus::multiple_choices:
            return boost::string_ref(multiple_choices);
        case HttpStatus::moved_permanently:
            return boost::string_ref(moved_permanently);
        case HttpStatus::moved_temporarily:
            return boost::string_ref(moved_temporarily);
        case HttpStatus::not_modified:
            return boost::string_ref(not_modified);
        case HttpStatus::bad_request:
            return boost::string_ref(bad_request);
        case HttpStatus::unauthorized:
            return boost::string_ref(unauthorized);
        case HttpStatus::forbidden:
            return boost::string_ref(forbidden);
        case HttpStatus::not_found:
            return boost::string_ref(not_found);
        case HttpStatus::internal_server_error:
            return boost::string_ref(internal_server_error);
        case HttpStatus::not_implemented:
            return boost::string_ref(not_implemented);
        case HttpStatus::bad_gateway:
            return boost::string_ref(bad_gateway);
        case HttpStatus::service_unavailable:
            return boost::string_ref(service_unavailable);
        default:
            return boost::string_ref(internal_server_error);
        }
    }

} // namespace status_strings


void ConnectionBase::formatHeaders(const Response& response)
{
    int len = snprintf(d_headersBuf.data(), d_headersBuf.size(),
        "%s"
        "Content-Length: %zu\r\n"
        "Connection: %s\r\n"
        "Content-Type: %s\r\n"
        "\r\n",

        StatusStrings::toStringRef(response.code).data(),
        response.size(),
        keepAlive ? "keep-alive" : "close",
        response.contentType.data());

    d_headersRef = boost::string_ref(d_headersBuf.data(), len);
}

