#include "HMUI/Touchable.hpp"
#include "bsml/shared/BSML/MainThreadScheduler.hpp"

#include "assets.hpp"
#include "Log.hpp"
#include "UI/TableViewCells/SpotifyTrackTableViewCell.hpp"
#include "UI/TableViewDataSources/SpotifyTrackTableViewDataSource.hpp"
#include "main.hpp"

DEFINE_TYPE(SpotifySearch::UI, SpotifyTrackTableViewDataSource);

using namespace SpotifySearch::UI;

HMUI::TableCell* SpotifyTrackTableViewDataSource::CellForIdx(HMUI::TableView* tableView, int idx) {
    auto tcd = tableView->DequeueReusableCellForIdentifier(SpotifyTrackTableViewCell::CELL_REUSE_ID);
    SpotifyTrackTableViewCell* spotifyCell;
    if (!tcd) {
        auto tableCell = UnityEngine::GameObject::New_ctor("SpotifyTrackTableViewCell");
        spotifyCell = tableCell->AddComponent<SpotifyTrackTableViewCell*>();
        spotifyCell->set_interactable(true);

        spotifyCell->set_reuseIdentifier(SpotifyTrackTableViewCell::CELL_REUSE_ID);
        BSML::parse_and_construct(IncludedAssets::SpotifyTrackTableViewCell_bsml, spotifyCell->get_transform(), spotifyCell);
        spotifyCell->get_gameObject()->AddComponent<HMUI::Touchable*>();
    } else {
        spotifyCell = tcd->GetComponent<SpotifyTrackTableViewCell*>();
    }

    const spotify::PlaylistTrack track = tracks_.at(idx);
    spotifyCell->setTrack(track);

    return spotifyCell;
}

int SpotifyTrackTableViewDataSource::NumberOfCells() {
    return tracks_.size();
}

float SpotifyTrackTableViewDataSource::CellSize() {
    return 8.0f;
}
