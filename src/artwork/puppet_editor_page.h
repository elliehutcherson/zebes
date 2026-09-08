#pragma once

#include <string_view>

namespace zebes {

// The editor page, embedded at build time from puppet_editor.html so the server
// is one binary with no runtime dependency on where the repository sits.
std::string_view PuppetEditorPageHtml();

}  // namespace zebes
