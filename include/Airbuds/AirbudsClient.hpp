#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <string_view>

#include "web-utils/shared/WebUtils.hpp"

#include "Configuration.hpp"
#include "Log.hpp"
#include "Airbuds/Playlist.hpp"
#include "Airbuds/Track.hpp"
#include "Airbuds/Json.hpp"
#include "Airbuds/Utils.hpp"

namespace airbuds {

class Client {

    public:
    std::vector<PlaylistTrack> getRecentlyPlayed();
    std::vector<PlaylistTrack> getRecentlyPlayedCachedOnly();

    std::vector<PlaylistTrack> getPlaylistTracks(std::string_view playlistId);

    std::vector<Playlist> getPlaylists();
    std::vector<Playlist> getPlaylistsCachedOnly();
    Playlist getRecentlyPlayedPlaylist();

    std::string getLastRecentlyPlayedWarning() const;
    void resetAirbudsCredentials();

    private:
    static constexpr size_t AIRBUDS_PAGE_LIMIT = 30;

    struct AirbudsCredentials {
        std::string accessToken;
        std::string userId;
    };

    std::string airbudsAccessToken_;
    std::string airbudsUserId_;
    std::optional<std::chrono::system_clock::time_point> airbudsAccessTokenExpiry_;

    std::string lastRecentlyPlayedWarning_;
    std::vector<PlaylistTrack> cachedRecentlyPlayedTracks_;

    std::optional<AirbudsCredentials> getAirbudsCredentials();
    bool isAirbudsAccessTokenValid() const;
    void refreshAirbudsAccessToken(const std::string& refreshToken);

    rapidjson::Document apiGetRecentlyPlayed(const AirbudsCredentials& credentials, const std::optional<std::string>& cursor, size_t limit);

    std::vector<PlaylistTrack> getRecentlyPlayedTracks();
};

} // namespace airbuds
