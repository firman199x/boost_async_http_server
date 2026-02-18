#pragma once

#include <boost/beast/core.hpp>
#include <iostream>
#include <string>

std::string path_cat(boost::beast::string_view base,
                     boost::beast::string_view path);

boost::beast::string_view mime_type(boost::beast::string_view path);

void fail(boost::beast::error_code ec, char const* what);

