#include <string>

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>

#include "beatsaber-hook/shared/utils/il2cpp-utils.hpp"
#include "beatsaber-hook/shared/utils/typedefs-string.hpp"
#include "bsml/shared/BSML.hpp"
#include "bsml/shared/Helpers/getters.hpp"
#include <bsml/shared/BSML/Components/ButtonIconImage.hpp>
#include <jni.h>

#include "Assets.hpp"
#include "Log.hpp"
#include "SpriteCache.hpp"
#include "UI/ViewControllers/SpotifyLoginViewController.hpp"
#include "main.hpp"

DEFINE_TYPE(SpotifySearch::UI::ViewControllers, SpotifyLoginViewController);

using namespace SpotifySearch::UI;
using namespace SpotifySearch::UI::ViewControllers;

static constexpr const char* const REDIRECT_URI_HOST = "127.0.0.1";
static constexpr const int REDIRECT_URI_PORT = 9999;

void SpotifyLoginViewController::DidActivate(const bool isFirstActivation, const bool addedToHierarchy, const bool screenSystemDisabling) {
    if (isFirstActivation) {
        BSML::parse_and_construct(Assets::SpotifyLoginViewController_bsml, this->get_transform(), this);

#if HOT_RELOAD
        fileWatcher->filePath = "/sdcard/SpotifyLoginViewController.bsml";
        fileWatcher->checkInterval = 1.0f;
#endif
    }

    if (pinInputContainer_) {
        if (isSecureAuthenticationTokenRequired()) {
            pinInputContainer_->get_gameObject()->set_active(true);
        } else {
            pinInputContainer_->get_gameObject()->set_active(false);
        }
    }
}

void SpotifyLoginViewController::PostParse() {
    // Set the clipboard icon
    static constexpr std::string_view KEY_CLIPBOARD_ICON = "clipboard-icon";
    UnityW<UnityEngine::Sprite> sprite = SpriteCache::getInstance().get(KEY_CLIPBOARD_ICON);
    if (!sprite) {
        sprite = BSML::Lite::ArrayToSprite(Assets::_binary_clipboard_icon_png_start);
        SpriteCache::getInstance().add(KEY_CLIPBOARD_ICON, sprite);
    }
    const std::vector<UnityW<UnityEngine::UI::Button>> pasteButtons = {
        clientIdPasteButton_,
        clientSecretPasteButton_};
    for (UnityW<UnityEngine::UI::Button> button : pasteButtons) {
        button->GetComponent<BSML::ButtonIconImage*>()->SetIcon(sprite);
        static constexpr float scale = 1.5f;
        button->get_transform()->Find("Content/Icon")->set_localScale({scale, scale, scale});
        Utils::removeRaycastFromButtonIcon(button);
    }

    // Set the redirect URI text field
    // Using bolded "Small Colon" because the normal one doesn't render correctly
    redirectUriTextField_->set_text(std::format("https<b>\uFE55</b> //{}<b>\uFE55</b> {}", REDIRECT_URI_HOST, REDIRECT_URI_PORT));

    // PIN input
    pinInputContainer_->get_gameObject()->set_active(isSecureAuthenticationTokenRequired());
    auto inputFieldView = pinTextField_->GetComponent<HMUI::InputFieldView*>();
    inputFieldView->_keyboardPositionOffset = {0, 50, 0};

    modalView_ = get_gameObject()->AddComponent<ModalView*>();
    modalView_->setPrimaryButton(false, "", nullptr);
    modalView_->setSecondaryButton(true, "OK", nullptr);
}

bool generateSelfSignedCert(const std::string& certFile, const std::string& keyFile) {
    SpotifySearch::Log.info("Generating SSL certificate. certFile = {} keyFile = {}", certFile, keyFile);

    // Generate an RSA key pair
    EVP_PKEY* pkey = nullptr;
    EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
    EVP_PKEY_keygen_init(ctx);
    EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, 2048);
    EVP_PKEY_keygen(ctx, &pkey);

    // Create an X.509 certificate
    X509* x509 = X509_new();
    ASN1_INTEGER_set(X509_get_serialNumber(x509), 1);
    X509_gmtime_adj(X509_get_notBefore(x509), 0);
    X509_gmtime_adj(X509_get_notAfter(x509), 31536000L); // 1 year

    // Attach the public RSA key
    X509_set_pubkey(x509, pkey);

    // Set subject and issuer
    X509_NAME* name = X509_get_subject_name(x509);
    X509_NAME_add_entry_by_txt(name, "C", MBSTRING_ASC, (unsigned char*) "US", -1, -1, 0);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, (unsigned char*) "localhost", -1, -1, 0);
    X509_set_issuer_name(x509, name);

    // Sign certificate
    X509_sign(x509, pkey, EVP_sha256());

    auto cleanup = [&]() {
        X509_free(x509);
        EVP_PKEY_free(pkey);
        EVP_PKEY_CTX_free(ctx);
    };

    // Write certificate to file
    FILE* certFp = fopen(certFile.c_str(), "wb");
    if (!certFp) {
        const auto reason = strerror(errno);
        SpotifySearch::Log.info("Failed to open certFile! error = {} reason = {} path = {}", errno, reason, certFile);
        cleanup();
        return false;
    }
    PEM_write_X509(certFp, x509);
    fclose(certFp);

    // Write private key to file
    FILE* keyFp = fopen(keyFile.c_str(), "wb");
    if (!keyFp) {
        const auto reason = strerror(errno);
        SpotifySearch::Log.info("Failed to open keyFile! error = {} reason = {} path = {}", errno, reason, keyFile);
        cleanup();
        return false;
    }
    PEM_write_PrivateKey(keyFp, pkey, nullptr, nullptr, 0, nullptr, nullptr);
    fclose(keyFp);

    // Cleanup
    cleanup();

    return true;
}

void SpotifyLoginViewController::onAuthorizationCodeReceived(const std::string& authorizationCode) {
    SpotifySearch::Log.info("Authorization code received!");

    // Stop the server
    server_->stop();

    // Authenticate
    try {
        SpotifySearch::spotifyClient->login(clientId_, clientSecret_, std::format("https://{}:{}", REDIRECT_URI_HOST, REDIRECT_URI_PORT), authorizationCode);

        std::string password;
        if (isSecureAuthenticationTokenRequired()) {
            password = pin_;
        }

        // Save auth token to file
        SpotifySearch::spotifyClient->saveAuthTokensToFile(spotify::Client::getAuthTokenPath(), password);

        waitingOnBrowserTextView_->set_text("Finished");

        // Re-launch the flow coordinator
        SpotifySearch::UI::FlowCoordinators::SpotifySearchFlowCoordinator::reopen();

        return;
    } catch (const std::exception& exception) {
        waitingOnBrowserTextView_->set_fontSize(3);
        waitingOnBrowserTextView_->set_text(std::format("Authentication Failure\n{}", exception.what()));
    }

    loginButton_->set_interactable(true);
    clientIdTextField_->set_interactable(true);
    clientIdPasteButton_->set_interactable(true);
    clientSecretTextField_->set_interactable(true);
    clientSecretPasteButton_->set_interactable(true);
}

void SpotifyLoginViewController::onPasteClientIdButtonClicked() {
    static auto UnityEngine_GUIUtility_get_systemCopyBuffer = il2cpp_utils::resolve_icall<StringW>("UnityEngine.GUIUtility::get_systemCopyBuffer");
    const std::string text = UnityEngine_GUIUtility_get_systemCopyBuffer();
    clientIdTextField_->set_text(text);
}

void SpotifyLoginViewController::onPasteClientSecretButtonClicked() {
    static auto UnityEngine_GUIUtility_get_systemCopyBuffer = il2cpp_utils::resolve_icall<StringW>("UnityEngine.GUIUtility::get_systemCopyBuffer");
    const std::string text = UnityEngine_GUIUtility_get_systemCopyBuffer();
    clientSecretTextField_->set_text(text);
}

void SpotifyLoginViewController::onLoginButtonClicked() {
    // Validate user input
    const std::string clientIdInput = clientIdTextField_->get_text();
    if (clientIdInput.empty()) {
        modalView_->setMessage("Client ID cannot be empty!");
        modalView_->show();
        return;
    }
    const std::string clientSecretInput = clientSecretTextField_->get_text();
    if (clientSecretInput.empty()) {
        modalView_->setMessage("Client secret cannot be empty!");
        modalView_->show();
        return;
    }
    if (isSecureAuthenticationTokenRequired()) {
        const std::string pinInput = pinTextField_->get_text();
        if (pinInput.empty()) {
            modalView_->setMessage("PIN cannot be empty!");
            modalView_->show();
            return;
        }
        pin_ = pinInput;
    }

    clientId_ = clientIdInput;
    clientSecret_ = clientSecretInput;

    // Disable the button
    loginButton_->set_interactable(false);
    clientIdTextField_->set_interactable(false);
    clientIdPasteButton_->set_interactable(false);
    clientSecretTextField_->set_interactable(false);
    clientSecretPasteButton_->set_interactable(false);
    waitingOnBrowserTextView_->get_gameObject()->set_active(true);
    waitingOnBrowserTextView_->set_fontSize(4);

    // Make sure the server is not already running
    if (isServerStarted_) {
        return;
    }

    // Start HTTPS server
    std::promise<void> isServerReadyPromise;
    std::future<void> isServerReadyFuture = isServerReadyPromise.get_future();
    isServerStarted_ = true;
    std::thread([this, promise = std::move(isServerReadyPromise)]() mutable {
        SpotifySearch::Log.info("Starting HTTPS server");

        // Generate a self-signed TLS certificate
        const std::filesystem::path certPath = SpotifySearch::getDataDirectory() / "cert.pem";
        const std::filesystem::path keyPath = SpotifySearch::getDataDirectory() / "key.pem";

        if (!generateSelfSignedCert(certPath, keyPath)) {
            SpotifySearch::Log.error("Failed generating TLS certificate!");
            isServerStarted_ = false;
            return;
        }

        // Create server
        server_ = std::make_unique<httplib::SSLServer>(certPath.c_str(), keyPath.c_str());
        if (!server_->is_valid()) {
            SpotifySearch::Log.error("Invalid server!");
            isServerStarted_ = false;
            return;
        }

        // Set error handler
        server_->set_error_handler([](const httplib::Request& request, httplib::Response& response) {
            SpotifySearch::Log.error("HTTPS Error: request = {}", request.path);

            response.status = 404;
            response.set_content("An error occurred!", "text/plain");
        });

        server_->Get("/", [this](const httplib::Request& request, httplib::Response& response) {
            SpotifySearch::Log.info("Received request: {} params: {}", request.path, request.params.size());

            // Check we got the authorization code
            std::string authorizationCode;
            for (const auto& item : request.params) {
                if (item.first == "code") {
                    authorizationCode = item.second;
                    break;
                }
            }
            if (!authorizationCode.empty()) {
                response.status = 200;
                response.set_content("Login successful. You can now close this window and return to Beat Saber.", "text/plain");
                BSML::MainThreadScheduler::Schedule([this, authorizationCode]() {
                    onAuthorizationCodeReceived(authorizationCode);
                });
                return;
            }

            response.status = 404;
        });

        // Start listening for requests
        SpotifySearch::Log.info("HTTPS server is listening on {}:{}", REDIRECT_URI_HOST, REDIRECT_URI_PORT);
        promise.set_value();
        if (!server_->listen(REDIRECT_URI_HOST, REDIRECT_URI_PORT)) {
            SpotifySearch::Log.error("Failed to start server!");
            isServerStarted_ = false;
            return;
        }
        SpotifySearch::Log.info("HTTPS server stopped");
        isServerStarted_ = false;
    }).detach();

    // Wait for the HTTPS server to start, the browser seems to freeze if we launch the URL before the server is ready.
    isServerReadyFuture.wait();

    // Build the query parameters for the request
    const std::string redirectUri = std::format("https://{}:{}", REDIRECT_URI_HOST, REDIRECT_URI_PORT);
    httplib::Params queryParams;
    queryParams.insert({"client_id", clientIdInput});
    queryParams.insert({"response_type", "code"});
    queryParams.insert({"redirect_uri", redirectUri});
    queryParams.insert({"scope", "user-library-read playlist-read-private playlist-read-collaborative"});

    // Open web browser
    const std::string url = httplib::append_query_params("https://accounts.spotify.com/authorize", queryParams);
    try {
        static auto UnityEngine_Application_OpenURL = il2cpp_utils::resolve_icall<void, StringW>("UnityEngine.Application::OpenURL");
        UnityEngine_Application_OpenURL(url);
    } catch (const std::exception& exception) {
        SpotifySearch::Log.error("Failed to open web browser: {}", exception.what());
    }
}
