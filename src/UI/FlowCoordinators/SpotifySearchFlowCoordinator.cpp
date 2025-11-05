#include "UI/FlowCoordinators/SpotifySearchFlowCoordinator.hpp"

#include "GlobalNamespace/SongPreviewPlayer.hpp"
#include "UnityEngine/ScriptableObject.hpp"
#include "bsml/shared/Helpers/getters.hpp"
#include <bsml/shared/Helpers/creation.hpp>

#include "Log.hpp"
#include "main.hpp"

DEFINE_TYPE(SpotifySearch::UI::FlowCoordinators, SpotifySearchFlowCoordinator);

using namespace SpotifySearch::UI::FlowCoordinators;

void SpotifySearchFlowCoordinator::DidActivate(bool isFirstActivation, bool addedToHierarchy, bool screenSystemEnabling) {
    SpotifySearch::Log.info("activate");
    if (!isFirstActivation) {
        return;
    }

    // Set title
    SetTitle("Spotify Search", HMUI::ViewController::AnimationType::In);

    // Make sure the back button is visible
    showBackButton = true;

    // Show view controllers
    if (SpotifySearch::spotifyClient->isAuthenticated()) {
        // Initialize view controllers
        mainViewController_ = BSML::Helpers::CreateViewController<ViewControllers::MainViewController*>();
        filterOptionsViewController_ = BSML::Helpers::CreateViewController<ViewControllers::FilterOptionsViewController*>();
        downloadHistoryViewController_ = BSML::Helpers::CreateViewController<ViewControllers::DownloadHistoryViewController*>();

        ProvideInitialViewControllers(mainViewController_, filterOptionsViewController_, downloadHistoryViewController_, nullptr, nullptr);
    } else {
        // We aren't authenticated yet. That means this is either the first time loading the mod, or the auth token was encrypted.
        if (std::filesystem::exists(spotify::Client::getAuthTokenPath())) {
            // Initialize view controllers
            spotifyAuthenticateViewController_ = BSML::Helpers::CreateViewController<ViewControllers::SpotifyAuthenticateViewController*>();

            ProvideInitialViewControllers(spotifyAuthenticateViewController_, nullptr, nullptr, nullptr, nullptr);
        } else {
            // Initialize view controllers
            spotifyLoginViewController_ = BSML::Helpers::CreateViewController<ViewControllers::SpotifyLoginViewController*>();

            ProvideInitialViewControllers(spotifyLoginViewController_, nullptr, nullptr, nullptr, nullptr);
        }
    }
}

void SpotifySearchFlowCoordinator::BackButtonWasPressed(HMUI::ViewController* topViewController) {
    SpotifySearch::Log.info("back button pressed, set return = false");
    SpotifySearch::returnToSpotifySearch = false;

    // Stop the song preview if it's playing
    UnityW<GlobalNamespace::SongPreviewPlayer> songPreviewPlayer = BSML::Helpers::GetDiContainer()->Resolve<GlobalNamespace::SongPreviewPlayer*>();
    if (songPreviewPlayer) {
        songPreviewPlayer->CrossfadeToDefault();
    }

    // Dismiss this flow coordinator
    this->_parentFlowCoordinator->DismissFlowCoordinator(this, HMUI::ViewController::AnimationDirection::Horizontal, nullptr, false);
}

void SpotifySearchFlowCoordinator::reset() {
    if (!spotifySearchFlowCoordinator_) {
        return;
    }
    auto p = spotifySearchFlowCoordinator_->_parentFlowCoordinator;
    SpotifySearch::Log.info("p = {}", static_cast<void*>(p));
    if (p) {
        p->DismissFlowCoordinator(spotifySearchFlowCoordinator_.ptr(), HMUI::ViewController::AnimationDirection::Horizontal, nullptr, true);
    }
    SpotifySearch::spotifySearchFlowCoordinator_ = nullptr;
}

void SpotifySearchFlowCoordinator::reopen() {
    SpotifySearchFlowCoordinator::reset();
    SpotifySearch::openSpotifySearchFlowCoordinator();
}
