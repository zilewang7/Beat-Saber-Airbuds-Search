#include "Configuration.hpp"

namespace SpotifySearch {

// Loads the config from disk using our modInfo, then returns it for use
// other config tools such as config-utils don't use this config, so it can be
// removed if those are in use
Configuration& getConfig() {
    static Configuration config(modInfo);
    return config;
}

std::filesystem::path getDataDirectory() {
    static const std::string dataDirectoryString = getDataDir(modInfo);
    static const std::filesystem::path dataDirectory {dataDirectoryString};
    if (!std::filesystem::exists(dataDirectory)) {
        std::filesystem::create_directory(dataDirectory);
    }
    return dataDirectory;
}

bool isSecureAuthenticationTokenRequired() {
    return getConfig().config["requirePassword"].GetBool();
}

void setIsSecureAuthenticationTokenRequired(bool value) {
    getConfig().config["requirePassword"].SetBool(value);
    getConfig().Write();
}

}
