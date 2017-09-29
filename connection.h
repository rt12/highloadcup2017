#pragma once

#include <rapidjson/stringbuffer.h>

#include <array>
#include <iostream>
#include <http_parser.h>
#include "picohttpparser.h"
#include "handler.h"

const boost::string_ref METHOD_GET("GET");
const boost::string_ref METHOD_POST("POST");
const boost::string_ref HTTP_HDR_CONTENT_LENGTH("Content-Length");
const boost::string_ref HTTP_HDR_CONNECTION("Connection");
const boost::string_ref HTTP_HDR_KEEP_ALIVE("keep-alive");
const boost::string_ref HTTP_HDR_CLOSE("close");

inline void toLower(char *p, size_t len)
{
    char *end = p + len;
    while (p != end) {
        *p = (char)tolower(*p);
        ++p;
    }
}

class HttpParser {
public:
    boost::string_ref methodRef, pathRef;
    int minor_version;
    phr_header headers[100];
    size_t headerscount = 0;

    int parseRequest(const char* buf, size_t buflen)
    {
        if (!d_stored.empty()) {
            d_stored.append(buf, buflen);
            buf = d_stored.data();
            buflen = d_stored.length();
        }

        const char *method, *path;
        size_t method_len, path_len;

        size_t numhdr = sizeof(headers) / sizeof(headers[0]);
        int res = phr_parse_request(buf, buflen, &method, &method_len, &path, &path_len, &minor_version, headers, &numhdr, 0);

        // incomplete
        if (res == -2) {
            // remember incomplete part
            if (d_stored.empty()) {
                d_stored.assign(buf, buflen);
            }
        }

        if (res < 0)
            return res;

        headerscount = numhdr;
        methodRef = boost::string_ref(method, method_len);
        pathRef = boost::string_ref(path, path_len);
        d_stored.clear();
        return res;
    }

private:

    std::string d_stored;
};

const size_t CONN_BUF_SIZE = 0xffff; // 64k
const ssize_t UV_EOF = -1;

struct uv_buf_t {
    char* base;
    size_t len;
};

class ConnectionBase {

public:
    ConnectionBase(Handler& handler): d_handler(handler)
    {
    }

    ~ConnectionBase()
    {
    }

    void onRead(ssize_t nread, const uv_buf_t * buf)
    {
        // frintf(stderr, "onRead (responseSent=%d)\n", responseSent);

        if (nread == UV_EOF) {
            onMessageComplete();
        } else {
            if (nread == 0) {
                close();
            } else {
                int ret = handleData(nread, buf);
                if (ret != 0) {
                    writeResponse(ret);
                }
            }
        }
    }

    int processHeaders()
    {
        if (onUrl(reqParser.pathRef.data(), reqParser.pathRef.length()) != 0) {
            return 400;
        }

        if (reqParser.methodRef == METHOD_GET)
            method = Handler::Method::GET;
        else if (reqParser.methodRef == METHOD_POST)
            method = Handler::Method::POST;
        else
            return 400;

        if (reqParser.minor_version == 1)
            keepAlive = true;

        for (size_t i = 0; i < reqParser.headerscount; ++i) {
            const auto& field = reqParser.headers[i];
            const boost::string_ref name(field.name, field.name_len);
            const boost::string_ref value(field.value, field.value_len);

            if (HTTP_HDR_CONTENT_LENGTH == name) {
                d_contentLength = atoi(field.value);
            } else if (HTTP_HDR_CONNECTION == name) {
                toLower((char*)field.value, field.value_len);
                if (value == HTTP_HDR_CLOSE)
                    keepAlive = false;
                else if(value == HTTP_HDR_KEEP_ALIVE)
                    keepAlive = true;
            }
        }

        return 0;
    }

    int handleData(ssize_t nread, const uv_buf_t * buf)
    {
        const char* pbuf = buf->base;

        if (!headerDone) {
            int pr = reqParser.parseRequest(buf->base, nread);
            if (pr > 0) {
                headerDone = true;

                int res = processHeaders();
                if (res != 0)
                    return res;

                // advance data pointer
                nread -= pr;
                pbuf += pr;

            } else if (pr == -1) {
                std::cerr << "Header parse error, hdr = \n" << boost::string_ref(buf->base, nread) << std::endl;
                return 400;
            }
        }

        // check if some extra shit is there
        if (d_dataRead + nread > d_contentLength) {
            nread = d_contentLength - d_dataRead;
        }

        onBody(pbuf, nread);
        d_dataRead += nread;

        if (d_contentLength == d_dataRead)
            onMessageComplete();

        return 0;
    }

    int onMessageComplete()
    {
        // fprintf(stderr, "onMessageComplete (responseSent=%d)\n", responseSent);

        int result = d_handler.handle(method, body, path, query, d_response);
        if (result == 200)
            d_response.setContentJson();

        writeResponse(result);
        return 0;
    }

    int onUrl(const char *at, size_t length)
    {
        http_parser_url url;
        if (http_parser_parse_url(at, length, false, &url)) {
            std::cout << "Failed to parse url: " << std::string(at, length) << std::endl;
            return 1;
        }

        if (url.field_set & (1 << UF_PATH)) {
            path.assign(at + url.field_data[UF_PATH].off, url.field_data[UF_PATH].len);
        }

        if (url.field_set & (1 << UF_QUERY)) {
            query.assign(at + url.field_data[UF_QUERY].off, url.field_data[UF_QUERY].len);
        }

        // fprintf(stderr, "Path: %s, query: %s\n", path.c_str(), query.c_str());

        return 0;
    }

    int onBody(const char *at, size_t length)
    {
        body.append(at, length);
        return 0;
    }

    int onWriteComplete()
    {
        if (!keepAlive) {
            close();
            return 0;
        }

        headerDone = false;
        keepAlive = false;

        path.clear();
        query.clear();
        body.clear();
        d_dataRead = 0;
        d_contentLength = 0;

        // clear response
        d_response.clear();

        startRead();

        return 0;
    }

    void formatHeaders(const Response& response);

    virtual void startRead() = 0;
    virtual void writeResponse(int status) = 0;
    virtual void close() = 0;

    // HTTP parsing
    HttpParser reqParser;
    bool keepAlive = false;
    bool headerDone = false;
    Handler::Method method;

    std::string path;
    std::string query;
    std::string body;
    Response d_response;

    Handler& d_handler;
    boost::string_ref d_headersRef;
    std::array<char, 2048> d_headersBuf;
    std::array<char, CONN_BUF_SIZE> d_readBuf;

    size_t d_contentLength = 0;
    size_t d_dataRead = 0;
};

