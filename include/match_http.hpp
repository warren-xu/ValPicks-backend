#pragma once
#include "../include/http.hpp"
#include <string>
#include <unistd.h>

/* 
Handles match endpoints and default message.
Returns full HTTP response string. */
std::string handle_match_http(const HttpRequest& req);
