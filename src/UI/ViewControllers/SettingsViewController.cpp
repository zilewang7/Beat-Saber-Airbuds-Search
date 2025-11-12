#include "HMUI/CurvedTextMeshPro.hpp"
#include "HMUI/Touchable.hpp"
#include "bsml/shared/BSML/Components/ExternalComponents.hpp"
#include <UnityEngine/Mesh.hpp>
#include <UnityEngine/Resources.hpp>
#include <VRUIControls/VRGraphicRaycaster.hpp>
#include <bsml/shared/BSML.hpp>
#include <bsml/shared/BSML/Tags/ModalTag.hpp>
#include <bsml/shared/Helpers/delegates.hpp>
#include <bsml/shared/Helpers/getters.hpp>

#include "Assets.hpp"
#include "UI/FlowCoordinators/SpotifySearchFlowCoordinator.hpp"
#include "UI/ViewControllers/SettingsViewController.hpp"
#include "main.hpp"

DEFINE_TYPE(SpotifySearch::UI::ViewControllers, SettingsViewController);

using namespace SpotifySearch::UI::ViewControllers;

void SettingsViewController::DidActivate(const bool isFirstActivation, bool addedToHierarchy, bool screenSystemDisabling) {
    if (isFirstActivation) {
        BSML::parse_and_construct(IncludedAssets::SettingsViewController_bsml, this->get_transform(), this);

        isClearingCache_ = false;
        showModalOnChange_ = true;

#if HOT_RELOAD
        fileWatcher->filePath = "/sdcard/SettingsViewController.bsml";
        fileWatcher->checkInterval = 1.0f;
#endif
        modalView_ = get_gameObject()->AddComponent<ModalView*>();
    } else {
        refresh();
    }
}

void SettingsViewController::PostParse() {
    refresh();
}

void SettingsViewController::refresh() {
    // Spotify account
    // profileImageView_->set_sprite(Utils::createSimpleSprite());
    refreshSpotifyAccountStatus();

    // Image cache
    refreshCacheSizeStatus();

    // Secure authentication token
    showModalOnChange_ = false;
    requirePinCheckbox_->set_Value(isSecureAuthenticationTokenRequired());
    showModalOnChange_ = true;
}

void SettingsViewController::refreshSpotifyAccountStatus() {

    const std::function<void()> onLoggedOut = [this]() {
        profileTextView_->set_text("You are not logged in.");
        loginOrLogoutButton_->get_transform()->Find("Content/Text")->GetComponent<HMUI::CurvedTextMeshPro*>()->set_text("Log in");
    };

    const std::function<void(const spotify::User&)> onLoggedIn = [this](const spotify::User& user) {
        profileTextView_->set_text(std::format("Logged in as <color=#FF00FF>{}</color>.", user.displayName));
        loginOrLogoutButton_->get_transform()->Find("Content/Text")->GetComponent<HMUI::CurvedTextMeshPro*>()->set_text("Log out");
    };

    onLoggedOut();

    if (!SpotifySearch::spotifyClient->isAuthenticated()) {
        return;
    }

    std::thread([this, onLoggedIn]() {
        try {
            const spotify::User user = SpotifySearch::spotifyClient->getUser();
            BSML::MainThreadScheduler::Schedule([this, onLoggedIn, user]() {
                onLoggedIn(user);
                /*Utils::getImageAsSprite(user.imageUrl, [this](const UnityW<UnityEngine::Sprite> sprite) {
                if (sprite) {
                    profileImageView_->set_sprite(sprite);
                }
            });*/
            });
        } catch (const std::exception& exception) {
            SpotifySearch::Log.error("Failed to get user: {}", exception.what());
        }
    }).detach();
}

void SettingsViewController::refreshCacheSizeStatus() {
    cacheSizeTextView_->set_text("(Calculating Usage...)");
    std::thread([this]() {
        const uintmax_t cacheSizeInBytes = getDirectorySizeInBytes(SpotifySearch::getDataDirectory() / "cache");
        BSML::MainThreadScheduler::Schedule([this, cacheSizeInBytes]() {
            cacheSizeTextView_->set_text(std::format("({} Used)", getHumanReadableSize(cacheSizeInBytes)));

            if (isClearingCache_) {
                clearCacheButton_->set_interactable(false);
                clearCacheButton_->GetComponentInChildren<HMUI::CurvedTextMeshPro*>()->set_text("Clearing...");
            } else {
                clearCacheButton_->set_interactable(true);
                clearCacheButton_->GetComponentInChildren<HMUI::CurvedTextMeshPro*>()->set_text("Clear Cache");
            }
        });
    }).detach();
}

void SettingsViewController::onLoginOrLogoutButtonClicked() {
    // Check if we are logged in
    if (SpotifySearch::spotifyClient->isAuthenticated()) {
        // Ask the user to confirm
        modalView_->setMessage("Are you sure you want to log out?");
        modalView_->setPrimaryButton(true, "Continue", [this]() {
            SpotifySearch::spotifyClient->logout();
            refreshSpotifyAccountStatus();
            SpotifySearch::spotifySearchFlowCoordinator_.clear();
            modalView_->hide(true);
        });
        modalView_->setSecondaryButton(true, "Cancel", nullptr);
        modalView_->show();
        return;
    }

    // Open main flow coordinator to start login flow
    SpotifySearch::openSpotifySearchFlowCoordinator();
}

void SettingsViewController::onClearCacheButtonClicked() {
    if (isClearingCache_) {
        return;
    }
    isClearingCache_ = true;
    clearCacheButton_->set_interactable(false);
    cacheSizeTextView_->set_text("(Clearing...)");

    std::thread([this]() {
        std::filesystem::remove_all(SpotifySearch::getDataDirectory() / "cache");
        isClearingCache_ = false;
        BSML::MainThreadScheduler::Schedule([this]() {
            clearCacheButton_->set_interactable(true);
            refreshCacheSizeStatus();
        });
    }).detach();
}

uintmax_t SettingsViewController::getDirectorySizeInBytes(const std::filesystem::path& path) {
    uintmax_t sizeInBytes = 0;
    if (!std::filesystem::exists(path)) {
        return 0;
    }
    for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator(path)) {
        if (std::filesystem::is_regular_file(entry)) {
            sizeInBytes += std::filesystem::file_size(entry.path());
        }
    }
    return sizeInBytes;
}

std::string SettingsViewController::getHumanReadableSize(const uintmax_t bytes) {
    if (bytes == 0) {
        return "0 B";
    }
    constexpr const char* const suffixes[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    size_t i = 0;
    double size = static_cast<double>(bytes);
    while (size >= 1024 && i < sizeof(suffixes)) {
        size /= 1024;
        ++i;
    }
    return std::format("{:.1f} {}", size, suffixes[i]);
}

void SettingsViewController::onRequirePinCheckboxChanged() {
    if (!showModalOnChange_) {
        return;
    }

    // Immediately reset the checkbox
    BSML::MainThreadScheduler::ScheduleNextFrame([this]() {
        showModalOnChange_ = false;
        requirePinCheckbox_->set_Value(!requirePinCheckbox_->get_Value());
        showModalOnChange_ = true;
    });

    modalView_->setMessage("Changing this option will require logging in to Spotify again. Are you sure you want to continue?");
    modalView_->setPrimaryButton(true, "Continue", [this](){
        // Update the checkbox to the intended state
        showModalOnChange_ = false;
        requirePinCheckbox_->set_Value(!requirePinCheckbox_->get_Value());
        showModalOnChange_ = true;

        // Dismiss modal
        modalView_->hide(true);

        // Update config
        setIsSecureAuthenticationTokenRequired(requirePinCheckbox_->get_Value());

        // Logout of Spotify
        SpotifySearch::spotifyClient->logout();
        refreshSpotifyAccountStatus();

        // Reset the flow coordinator to trigger the login flow the next time we open it
        SpotifySearch::UI::FlowCoordinators::SpotifySearchFlowCoordinator::reset();
    });
    modalView_->setSecondaryButton(true, "Cancel", nullptr);
    modalView_->show();
}
