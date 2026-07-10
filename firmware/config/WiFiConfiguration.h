#pragma once

#include <Arduino.h>

#if __has_include("config/WiFiSecrets.h")
#include "config/WiFiSecrets.h"
#else
namespace WiFiSecrets
{
    static constexpr const char *SSID = "";
    static constexpr const char *PASSWORD = "";
}
#endif

namespace WiFiConfiguration
{
    static constexpr const char *WIFI_SSID = WiFiSecrets::SSID;
    static constexpr const char *WIFI_PASSWORD = WiFiSecrets::PASSWORD;
}
