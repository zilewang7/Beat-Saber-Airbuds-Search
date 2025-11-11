#pragma once

#include <custom-types/shared/macros.hpp>
#include <bsml/shared/BSML.hpp>
#include <TMPro/TextMeshProUGUI.hpp>
#include <bsml/shared/BSML/Components/Settings/ToggleSetting.hpp>
#include <HMUI/EventSystemListener.hpp>

#include "Log.hpp"
#include "Modal.hpp"
#include "UI/ModalView.hpp"

#if HOT_RELOAD
#include <bsml/shared/BSML/ViewControllers/HotReloadViewController.hpp>
using BaseViewController = BSML::HotReloadViewController;
#else
#include <HMUI/ViewController.hpp>
using BaseViewController = HMUI::ViewController;
#endif

DECLARE_CLASS_CODEGEN_INTERFACES(SpotifySearch::UI::ViewControllers, SettingsViewController, BaseViewController) {

    DECLARE_OVERRIDE_METHOD_MATCH(void, DidActivate, &HMUI::ViewController::DidActivate, bool isFirstActivation, bool addedToHierarchy, bool screenSystemDisabling);

    DECLARE_INSTANCE_METHOD(void, PostParse);

    // Spotify Account
    // DECLARE_INSTANCE_FIELD(UnityW<HMUI::ImageView>, profileImageView_);
    DECLARE_INSTANCE_FIELD(UnityW<TMPro::TextMeshProUGUI>, profileTextView_);
    DECLARE_INSTANCE_FIELD(UnityW<UnityEngine::UI::Button>, loginOrLogoutButton_);
    DECLARE_INSTANCE_METHOD(void, onLoginOrLogoutButtonClicked);

    // Image Cache
    DECLARE_INSTANCE_FIELD(UnityW<TMPro::TextMeshProUGUI>, cacheSizeTextView_);
    DECLARE_INSTANCE_FIELD(UnityW<UnityEngine::UI::Button>, clearCacheButton_);
    DECLARE_INSTANCE_METHOD(void, onClearCacheButtonClicked);

    // Require PIN
    DECLARE_INSTANCE_FIELD(UnityW<BSML::ToggleSetting>, requirePinCheckbox_);
    DECLARE_INSTANCE_METHOD(void, onRequirePinCheckboxChanged);

    // Modal
    DECLARE_INSTANCE_FIELD(UnityW<ModalView>, modalView_);

    private:
    void refreshSpotifyAccountStatus();
    void refreshCacheSizeStatus();

    void refresh();

    uintmax_t getDirectorySizeInBytes(const std::filesystem::path& path);
    std::string getHumanReadableSize(uintmax_t bytes);

    std::atomic_bool isClearingCache_;

    std::atomic_bool showModalOnChange_;
};
