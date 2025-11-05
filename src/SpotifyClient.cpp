#include "web-utils/shared/WebUtils.hpp"

#include "Log.hpp"
#include "Spotify/Track.hpp"
#include "SpotifyClient.hpp"
#include "Utils.hpp"
#include "main.hpp"
#include "ThreadPool.hpp"
#include "Encryption.hpp"

using namespace spotify;

using namespace SpotifySearch::Utils::json;

std::string jsonDocumentToString(const rapidjson::Value& value) {
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    value.Accept(writer);
    return buffer.GetString();
}

bool spotify::Client::isAuthenticated() {
    return !encodedClientIdAndClientSecret_.empty() && !accessToken_.empty() && !refreshToken_.empty();
}

void spotify::Client::saveAuthTokensToFile(const std::filesystem::path& path, const std::string_view password) {
    // Create JSON document
    rapidjson::Document document;
    document.SetObject();

    // Get allocator
    rapidjson::Document::AllocatorType& allocator = document.GetAllocator();

    // Add data
    document.AddMember("client_id_and_client_secret", encodedClientIdAndClientSecret_, allocator);
    document.AddMember("access_token", accessToken_, allocator);
    document.AddMember("refresh_token", refreshToken_, allocator);

    // Convert JSON document to bytes
    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
    document.Accept(writer);
    std::vector<uint8_t> data(buffer.GetString(), buffer.GetString() + buffer.GetSize());

    // Encrypt the data if a password is provided
    if (!password.empty()) {
        data = SpotifySearch::encrypt(password, data);
    }

    // Save to file
    std::ofstream outputFileStream(path,  std::ios::binary);
    if (!outputFileStream) {
        throw std::runtime_error("Failed to open file for writing!");
    }
    outputFileStream.write(reinterpret_cast<const char*>(data.data()), data.size());
    outputFileStream.close();
}

void spotify::Client::getCredentialsFromJson(const rapidjson::Value& json) {
    encodedClientIdAndClientSecret_ = getString(json, "client_id_and_client_secret");
    accessToken_ = getString(json, "access_token");
    refreshToken_ = getString(json, "refresh_token");
}

void spotify::Client::loadAuthTokensFromFile(const std::filesystem::path& path) {
    // Read the file into a string
    std::ifstream inputFileStream(path);
    if (!inputFileStream) {
        throw std::runtime_error("Failed to open file for reading!");
    }
    std::stringstream buffer;
    buffer << inputFileStream.rdbuf();
    const std::string jsonStr = buffer.str();

    // Parse the string into a Document
    rapidjson::Document document;
    if (document.Parse(jsonStr.c_str()).HasParseError()) {
        throw std::runtime_error("Failed to parse JSON!");
    }

    getCredentialsFromJson(document);
}

void spotify::Client::login(const std::filesystem::path& path) {
    loadAuthTokensFromFile(path);
}

void spotify::Client::loginWithPassword(const std::string_view password) {
    std::ifstream stream(Client::getAuthTokenPath(), std::ios::binary);
    const std::vector<uint8_t> data((std::istreambuf_iterator<char>(stream)), {});
    const std::vector<uint8_t> decryptedData = SpotifySearch::decrypt(password, data);

    // Parse the string into a Document
    rapidjson::Document document;
    if (document.Parse(reinterpret_cast<const char*>(decryptedData.data()), decryptedData.size()).HasParseError()) {
        throw std::runtime_error("Failed to parse JSON!");
    }

    getCredentialsFromJson(document);
}

const rapidjson::Document& getJsonDocumentFromResponse(const WebUtils::JsonResponse& response) {
    const std::optional<rapidjson::Document>& data = response.responseData;
    if (!response.IsSuccessful()) {
        if (data) {
            throw std::runtime_error(std::format("API ERROR: code = {} data = {}", response.httpCode, jsonDocumentToString(*data)));
        }
        throw std::runtime_error(std::format("API ERROR: code = {}", response.httpCode));
    }
    if (!data) {
        throw std::runtime_error(std::format("API ERROR: Request successful but no data! code = {}", response.httpCode));
    }
    return *response.responseData;
}

bool spotify::Client::login(const std::string& clientId, const std::string& clientSecret, const std::string& redirectUri, const std::string& authorizationCode) {
    encodedClientIdAndClientSecret_ = SpotifySearch::Utils::encodeBase64(std::format("{}:{}", clientId, clientSecret));

    const auto response = WebUtils::Post<WebUtils::JsonResponse>(
        WebUtils::URLOptions(
            "https://accounts.spotify.com/api/token",
            WebUtils::URLOptions::QueryMap{
                {"grant_type", "authorization_code"},
                {"code", authorizationCode},
                {"redirect_uri", redirectUri},
            },
            WebUtils::URLOptions::HeaderMap{
                {"Content-Type", "application/x-www-form-urlencoded"},
                {"Authorization", std::format("Basic {}", encodedClientIdAndClientSecret_)}}),
        SpotifySearch::Utils::toSpan(""));

    const rapidjson::Document& document = getJsonDocumentFromResponse(response);
    accessToken_ = getString(document, "access_token");
    refreshToken_ = getString(document, "refresh_token");

    return true;
}

void spotify::Client::refreshAccessToken2() {
    SpotifySearch::Log.debug("");

    const std::string data = std::format("grant_type=refresh_token&refresh_token={}", refreshToken_);

    const auto response = WebUtils::Post<WebUtils::JsonResponse>(
        WebUtils::URLOptions(
            "https://accounts.spotify.com/api/token",
            WebUtils::URLOptions::QueryMap{},
            WebUtils::URLOptions::HeaderMap{
                {"Content-Type", "application/x-www-form-urlencoded"},
                {"Authorization", std::format("Basic {}", encodedClientIdAndClientSecret_)}}),
        SpotifySearch::Utils::toSpan(data));
    const rapidjson::Document& document = getJsonDocumentFromResponse(response);
    accessToken_ = document["access_token"].GetString();
}

void spotify::Client::logout() {
    encodedClientIdAndClientSecret_ = "";
    accessToken_ = "";
    refreshToken_ = "";
    std::filesystem::remove(getAuthTokenPath());
}

User spotify::Client::getUser() {
    refreshAccessToken2();

    const auto response = WebUtils::Get<WebUtils::JsonResponse>(
        WebUtils::URLOptions(
            "https://api.spotify.com/v1/me",
            WebUtils::URLOptions::QueryMap{},
            WebUtils::URLOptions::HeaderMap{
                {"Authorization", std::format("Bearer {}", accessToken_)}}));

    const rapidjson::Document& document = getJsonDocumentFromResponse(response);
    return getUserFromJson(document);
}

std::vector<Track> Client::getPlaylistTracks(const std::string_view playlistId, size_t offset, size_t limit) {
    refreshAccessToken2();

    std::vector<spotify::Track> tracks;

    const auto response = WebUtils::Get<WebUtils::JsonResponse>(
        WebUtils::URLOptions(
            std::format("https://api.spotify.com/v1/playlists/{}/tracks", playlistId),
            WebUtils::URLOptions::QueryMap{
                {"offset", std::to_string(offset)},
                {"limit", std::to_string(limit)},
                {"fields", "items(track(id,name,artists,album(images))"},
            },
            WebUtils::URLOptions::HeaderMap{
                {"Authorization", std::format("Bearer {}", accessToken_)}}));

    const rapidjson::Document& document = getJsonDocumentFromResponse(response);
    const auto items = document["items"].GetArray();
    for (rapidjson::SizeType i = 0; i < items.Size(); ++i) {
        const auto& track = items[i]["track"];
        try {
            const Track spotifyTrack = getTrackFromJson(track);
            tracks.push_back(spotifyTrack);
        } catch (const std::exception& exception) {
            SpotifySearch::Log.warn("Failed to parse Spotify track: {}", exception.what());
        }
    }

    return tracks;
}

std::vector<Playlist> spotify::Client::getPlaylists() {

    static std::unique_ptr<std::vector<Playlist>> cachedValue;
    if (cachedValue) {
        SpotifySearch::Log.info("RETURN CACHED");
        return *cachedValue;
    }

    std::vector<Playlist> playlists;

    refreshAccessToken2();

    const auto response = WebUtils::Get<WebUtils::JsonResponse>(
        WebUtils::URLOptions(
            "https://api.spotify.com/v1/me/playlists",
            WebUtils::URLOptions::QueryMap{},
            WebUtils::URLOptions::HeaderMap{
                {"Authorization", std::format("Bearer {}", accessToken_)}}));
    const rapidjson::Document& document = getJsonDocumentFromResponse(response);

    const auto items = document["items"].GetArray();
    for (rapidjson::SizeType i = 0; i < items.Size(); ++i) {
        const auto& itemJson = items[i];
        Playlist playlist = getPlaylistFromJson(itemJson);
        playlists.push_back(playlist);
    }
    cachedValue = std::make_unique<std::vector<Playlist>>(playlists);

    return playlists;
}

std::vector<Image> Client::getImagesFromJson(const rapidjson::Value& json) {
    std::vector<Image> images;
    for (rapidjson::SizeType i = 0; i < json.Size(); ++i) {
        const auto& imageJson = json[i];

        Image image{"", -1, -1};

        // URL
        image.url = imageJson["url"].GetString();

        // Width
        static constexpr const char* const JSON_KEY_WIDTH = "width";
        if (imageJson.HasMember(JSON_KEY_WIDTH)) {
            const auto& jsonValueWidth = imageJson[JSON_KEY_WIDTH];
            if (jsonValueWidth.IsNumber()) {
                image.width = jsonValueWidth.GetInt();
            }
        }

        // Height
        static constexpr const char* const JSON_KEY_HEIGHT = "height";
        if (imageJson.HasMember(JSON_KEY_HEIGHT)) {
            const auto& jsonValueHeight = imageJson[JSON_KEY_HEIGHT];
            if (jsonValueHeight.IsInt()) {
                image.height = jsonValueHeight.GetInt();
            }
        }

        images.push_back(image);
    }
    return images;
}

Image Client::getSmallestImage(const std::vector<Image>& images) {
    Image smallestImage = images.at(0);
    for (const Image& image : images) {
        if (image.width == -1 || image.height == -1) {
            continue;
        }
        if (smallestImage.width == -1 || smallestImage.height == -1 || image.width < smallestImage.width || image.height < smallestImage.height) {
            smallestImage = image;
        }
    }
    return smallestImage;
}

Playlist Client::getPlaylistFromJson(const rapidjson::Value& json) {
    Playlist playlist;

    playlist.id = json["id"].GetString();
    playlist.name = json["name"].GetString();

    playlist.tracksUrl = json["tracks"]["href"].GetString();
    playlist.totalItemCount = json["tracks"]["total"].GetUint64();

    if (json.HasMember("images")) {
        if (json["images"].IsArray()) {
            const auto& imagesJson = json["images"].GetArray();
            const std::vector<Image> images = getImagesFromJson(imagesJson);

            // Find smallest image
            if (!images.empty()) {
                const Image smallestImage = getSmallestImage(images);
                playlist.imageUrl = smallestImage.url;
            }
        } else {
            SpotifySearch::Log.warn("Playlist has images but wrong type: {}", static_cast<int>(json["images"].GetType()));
        }
    } else {
        SpotifySearch::Log.warn("Playlist is missing images!");
    }

    return playlist;
}

Track Client::getTrackFromJson(const rapidjson::Value& document) {
    Track track;

    // ID
    track.id = getString(document, "id");

    // Name
    track.name = getString(document, "name");

    // Artists
    const auto& artists = document["artists"].GetArray();
    for (rapidjson::SizeType j = 0; j < artists.Size(); ++j) {
        spotify::Artist spotifyArtist;
        spotifyArtist.id = artists[j]["id"].GetString();
        spotifyArtist.name = artists[j]["name"].GetString();
        track.artists.push_back(spotifyArtist);
    }

    // Album
    const auto& albumJson = document["album"].GetObject();
    const auto& imagesJson = albumJson["images"].GetArray();

    const std::vector<Image> images = getImagesFromJson(imagesJson);

    // Find smallest image
    if (!images.empty()) {
        const Image smallestImage = getSmallestImage(images);
        track.album.url = smallestImage.url;
    }

    return track;
}

User Client::getUserFromJson(const rapidjson::Value& json) {
    User user;

    // ID
    user.id = getString(json, "id");

    // Display name
    user.displayName = getString(json, "display_name");

    // Profile image URL
    const auto& images = json["images"].GetArray();
    for (rapidjson::SizeType i = 0; i < images.Size(); ++i) {
        user.imageUrl = getString(images[0], "url");
    }

    return user;
}

Playlist Client::getLikedSongsPlaylist() {
    refreshAccessToken2();

    Playlist playlist;

    const auto response = WebUtils::Get<WebUtils::JsonResponse>(
        WebUtils::URLOptions(
            "https://api.spotify.com/v1/me/tracks",
            WebUtils::URLOptions::QueryMap{
                {"offset", std::to_string(0)},
                {"limit", std::to_string(1)}},
            WebUtils::URLOptions::HeaderMap{
                {"Authorization", std::format("Bearer {}", accessToken_)}}));
    const rapidjson::Document& document = getJsonDocumentFromResponse(response);

    playlist.id = "liked-songs";
    playlist.name = "Liked Songs";
    playlist.totalItemCount = document["total"].GetUint64();

    return playlist;
}

std::vector<spotify::Track> spotify::Client::getLikedSongs(const size_t offset, size_t limit) {
    refreshAccessToken2();

    std::vector<spotify::Track> resultTracks;

    // Determine chunk count
    size_t chunkCount = (limit + MAX_LIMIT - 1) / MAX_LIMIT;
    if (limit == 0) {
        chunkCount = 1;
    }

    auto getResponse = [this](const size_t offset, const size_t limit) -> rapidjson::Document {
        const auto response = WebUtils::Get<WebUtils::JsonResponse>(
            WebUtils::URLOptions(
                "https://api.spotify.com/v1/me/tracks",
                WebUtils::URLOptions::QueryMap{
                    {"offset", std::to_string(offset)},
                    {"limit", std::to_string(limit)}},
                WebUtils::URLOptions::HeaderMap{
                    {"Authorization", std::format("Bearer {}", accessToken_)}}));

        rapidjson::Document document;
        document.CopyFrom(getJsonDocumentFromResponse(response), document.GetAllocator());
        return document;
    };

    SpotifySearch::ThreadPool threadPool;

    std::mutex mutex;

    for (size_t i = 0; i < chunkCount; ++i) {

        if (i == 0) {
            const rapidjson::Document& document = getResponse(offset + (MAX_LIMIT * i), std::min(limit, MAX_LIMIT));

            // Get total items in this playlist
            const size_t total = document["total"].GetUint64();
            if (limit == 0) {
                limit = total;
                chunkCount = (limit + MAX_LIMIT - 1) / MAX_LIMIT;
            } else {
                limit = std::min(limit, total);
            }

            const auto items = document["items"].GetArray();
            for (rapidjson::SizeType i = 0; i < items.Size(); ++i) {
                const auto& track = items[i]["track"];
                try {
                    const Track spotifyTrack = getTrackFromJson(track);
                    resultTracks.push_back(spotifyTrack);
                } catch (const std::exception& exception) {
                    SpotifySearch::Log.warn("Failed to parse Spotify track: {}", exception.what());
                }
            }
        } else {
            auto task = [this, getResponse, offset, i, limit, &mutex, &resultTracks](){
                SpotifySearch::Log.info("Start task: offset = {} i = {} limit = {}", offset, i, limit);
                const rapidjson::Document& document = getResponse(offset + (MAX_LIMIT * i), std::min(limit, MAX_LIMIT));

                const auto items = document["items"].GetArray();
                for (rapidjson::SizeType i = 0; i < items.Size(); ++i) {
                    const auto& track = items[i]["track"];
                    try {
                        const Track spotifyTrack = getTrackFromJson(track);
                        std::lock_guard lock(mutex);
                        resultTracks.push_back(spotifyTrack);
                    } catch (const std::exception& exception) {
                        SpotifySearch::Log.warn("Failed to parse Spotify track: {}", exception.what());
                    }
                }
            };

            threadPool.submit(task);
        }
    }

    threadPool.wait();

    return resultTracks;
}
