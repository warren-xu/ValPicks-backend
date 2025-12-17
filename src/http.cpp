#include "../include/http.hpp"

#include <sstream>

std::string url_decode(const std::string &s)
{
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] == '%' && i + 2 < s.size())
        {
            int v = 0;
            std::istringstream iss(s.substr(i + 1, 2));
            iss >> std::hex >> v;
            out.push_back(static_cast<char>(v));
            i += 2;
        }
        else if (s[i] == '+')
        {
            out.push_back(' ');
        }
        else
        {
            out.push_back(s[i]);
        }
    }
    return out;
}

std::string get_query_param(const std::string &query, const std::string &key)
{
    std::string pattern = key + "=";
    std::size_t pos = query.find(pattern);
    if (pos == std::string::npos)
        return "";
    pos += pattern.size();
    std::size_t end = query.find("&", pos);
    std::string val = query.substr(
        pos,
        end == std::string::npos ? std::string::npos : end - pos);
    return url_decode(val);
}

bool parse_http_request(const std::string &raw, HttpRequest &out)
{
    std::size_t lineEnd = raw.find("\r\n");
    if (lineEnd == std::string::npos)
        return false;
    std::string firstLine = raw.substr(0, lineEnd);

    std::istringstream iss(firstLine);
    std::string method, uri, version;
    if (!(iss >> method >> uri >> version))
        return false;

    out.method = method;

    std::size_t qPos = uri.find("?");
    if (qPos == std::string::npos)
    {
        out.path = uri;
        out.query = "";
    }
    else
    {
        out.path = uri.substr(0, qPos);
        out.query = uri.substr(qPos + 1);
    }
    return true;
}

std::string make_http_response(const std::string &body,
                               const std::string &contentType,
                               int statusCode,
                               const std::string &statusText)
{
    std::ostringstream oss;
    oss << "HTTP/1.1 " << statusCode << " " << statusText << "\r\n";
    oss << "Content-Type: " << contentType << "\r\n";
    oss << "Content-Length: " << body.size() << "\r\n";
    oss << "Connection: close\r\n";
    oss << "Access-Control-Allow-Origin: *\r\n";
    oss << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
    oss << "Access-Control-Allow-Headers: Content-Type\r\n";
    oss << "\r\n";
    oss << body;
    return oss.str();
}
