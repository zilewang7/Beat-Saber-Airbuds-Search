#include "UI/ViewControllers/SpotifyAuthenticateViewController.hpp"

#include "bsml/shared/BSML.hpp"
#include "bsml/shared/Helpers/getters.hpp"
#include "bsml/shared/BSML/ViewControllers/HotReloadViewController.hpp"

#include "assets.hpp"
#include "Log.hpp"
#include "main.hpp"

DEFINE_TYPE(SpotifySearch::UI::ViewControllers, SpotifyAuthenticateViewController);

using namespace SpotifySearch::UI;
using namespace SpotifySearch::UI::ViewControllers;


void SpotifyAuthenticateViewController::DidActivate(const bool isFirstActivation, const bool addedToHierarchy, const bool screenSystemDisabling) {
    if (isFirstActivation) {
        BSML::parse_and_construct(IncludedAssets::SpotifyAuthenticateViewController_bsml, this->get_transform(), this);

#if HOT_RELOAD
        fileWatcher->filePath = "/sdcard/SpotifyAuthenticateViewController.bsml";
        fileWatcher->checkInterval = 1.0f;
#endif
    }

    modalView_ = get_gameObject()->AddComponent<ModalView*>();
}

void SpotifyAuthenticateViewController::onLoginButtonClicked() {
    modalView_->setPrimaryButton(false, "", nullptr);
    modalView_->setSecondaryButton(true, "OK", nullptr);

    const std::string pinInput = pinTextField_->get_text();
    if (pinInput.empty()) {
        modalView_->setMessage("The PIN cannot be empty!");
        modalView_->show();
        return;
    }

    try {
        SpotifySearch::spotifyClient->loginWithPassword(pinInput);
    } catch (const std::exception& exception) {
        modalView_->setMessage(std::format("Incorrect PIN!\n{}", exception.what()));
        modalView_->show();
        return;
    }

    if (!SpotifySearch::spotifyClient->isAuthenticated()) {
        modalView_->setMessage("Incorrect PIN!");
        modalView_->show();
        return;
    }

    // Re-launch the flow coordinator
    SpotifySearch::UI::FlowCoordinators::SpotifySearchFlowCoordinator::reopen();
}

void SpotifyAuthenticateViewController::onForgotPinButtonClicked() {
    modalView_->setMessage("You will be logged out of Spotify.");
    modalView_->setPrimaryButton(true, "continue", [](){
        try {
            std::filesystem::remove(spotify::Client::getAuthTokenPath());
        } catch (const std::filesystem::filesystem_error& exception) {
            SpotifySearch::Log.warn("Failed to remove auth token file: {}", exception.what());
        }

        // Re-launch the flow coordinator
        SpotifySearch::UI::FlowCoordinators::SpotifySearchFlowCoordinator::reopen();
    });
    modalView_->setSecondaryButton(true, "Cancel", nullptr);
    modalView_->show();
}
