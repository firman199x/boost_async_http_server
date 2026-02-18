#include "utils.h"

std::string path_cat(boost::beast::string_view base,
                     boost::beast::string_view path)
{
    if (base.empty()) return std::string(path);
    std::string result(base);
#ifdef BOOST_MSVC
    char constexpr path_separator = '\\';
    if (result.back() == path_separator) result.resize(result.size() - 1);
    result.append(path.data(), path.size());
    for (auto& c : result)
        if (c == '/') c = path_separator;
#else
    char constexpr path_separator = '/';
    if (result.back() == path_separator) result.resize(result.size() - 1);
    result.append(path.data(), path.size());
#endif
    return result;
}

boost::beast::string_view mime_type(boost::beast::string_view path)
{
    auto const ext = [&path] {
        auto const pos = path.rfind(".");
        if (pos == boost::beast::string_view::npos)
            return boost::beast::string_view{};
        return path.substr(pos);
    }();
    if (boost::beast::iequals(ext, ".htm")) return "text/html";
    if (boost::beast::iequals(ext, ".html")) return "text/html";
    if (boost::beast::iequals(ext, ".php")) return "text/html";
    if (boost::beast::iequals(ext, ".css")) return "text/css";
    if (boost::beast::iequals(ext, ".txt")) return "text/plain";
    if (boost::beast::iequals(ext, ".js")) return "application/javascript";
    if (boost::beast::iequals(ext, ".json")) return "application/json";
    if (boost::beast::iequals(ext, ".xml")) return "application/xml";
    if (boost::beast::iequals(ext, ".swf"))
        return "application/x-shockwave-flash";
    if (boost::beast::iequals(ext, ".flv")) return "video/x-flv";
    if (boost::beast::iequals(ext, ".png")) return "image/png";
    if (boost::beast::iequals(ext, ".jpe")) return "image/jpeg";
    if (boost::beast::iequals(ext, ".jpeg")) return "image/jpeg";
    if (boost::beast::iequals(ext, ".jpg")) return "image/jpeg";
    if (boost::beast::iequals(ext, ".gif")) return "image/gif";
    if (boost::beast::iequals(ext, ".bmp")) return "image/bmp";
    if (boost::beast::iequals(ext, ".ico")) return "image/vnd.microsoft.icon";
    if (boost::beast::iequals(ext, ".tiff")) return "image/tiff";
    if (boost::beast::iequals(ext, ".tif")) return "image/tiff";
    if (boost::beast::iequals(ext, ".svg")) return "image/svg+xml";
    if (boost::beast::iequals(ext, ".svgz")) return "image/svg+xml";
    return "application/text";
}

void fail(boost::beast::error_code ec, char const* what)
{
    std::cerr << what << ": " << ec.message() << "\n";
}

