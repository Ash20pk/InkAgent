#pragma once

#include <cstddef>
#include <string>
#include <vector>

// Apps that ship as data.
//
// A manifest dropped in /Apps appears in the drawer with no firmware build.
// It can describe screens and bind to the data-source whitelist, and that is
// all it can do — there is no control flow in the format and no way to name a
// source the firmware does not already expose. So a third-party app cannot
// poll, cannot notify, cannot reach the network and cannot outlive the screen
// it draws. The ceiling is the point.
namespace engage {

struct CatalogEntry {
  std::string path;  // /Apps/<file>.json
  std::string name;  // tile label, a literal in the manifest
  std::string icon;  // icon name from the manifest, resolved by the drawer
};

// Apps folder, created on first scan so it is there to drop files into.
constexpr const char* kAppsFolder = "/Apps";

// A manifest is a screen, not a program: anything much larger than this is a
// mistake, and reading it would cost more heap than a screen is worth.
constexpr size_t kMaxManifestBytes = 4096;

// Most of a drawer's worth. Past this the grid stops being glanceable anyway.
constexpr size_t kMaxCatalogApps = 8;

// Scans /Apps. Entries without a name are skipped: a tile with no label is a
// tile nobody can choose deliberately.
std::vector<CatalogEntry> scanApps();

// Reads a manifest file into `out`. False when missing, empty, oversized or
// unreadable.
bool readManifest(const char* path, std::string& out);

}  // namespace engage
