#pragma once

#include <custom-types/shared/macros.hpp>
#include <HMUI/InputFieldView.hpp>

#include "Spotify/SpotifyClient.hpp"
#include "UI/ModalView.hpp"

#if HOT_RELOAD
#include "bsml/shared/BSML/ViewControllers/HotReloadViewController.hpp"
using BaseViewController = BSML::HotReloadViewController;
#else
#include "HMUI/ViewController.hpp"
using BaseViewController = HMUI::ViewController;
#endif

DECLARE_CLASS_CODEGEN_INTERFACES(SpotifySearch::UI::ViewControllers, SpotifyAuthenticateViewController, BaseViewController) {

    DECLARE_OVERRIDE_METHOD_MATCH(void, DidActivate, &HMUI::ViewController::DidActivate, bool isFirstActivation, bool addedToHierarchy, bool screenSystemDisabling);

    // PIN
    DECLARE_INSTANCE_FIELD(UnityW<HMUI::InputFieldView>, pinTextField_);

    // Login button
    DECLARE_INSTANCE_FIELD(UnityW<UnityEngine::UI::Button>, loginButton_);
    DECLARE_INSTANCE_METHOD(void, onLoginButtonClicked);

    // Forgot PIN button
    DECLARE_INSTANCE_METHOD(void, onForgotPinButtonClicked);

    // Modal
    DECLARE_INSTANCE_FIELD(UnityW<ModalView>, modalView_);

};
