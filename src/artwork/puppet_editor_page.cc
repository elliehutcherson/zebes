#include "artwork/puppet_editor_page.h"

#include <string_view>

#include "puppet_editor_html.h"

namespace zebes {

// The length comes from the array rather than from the terminator, so a stray
// zero byte anywhere in the page truncates nothing. Built from a char array it
// would be strlen, and half a page would be served with a 200 and no error
// anywhere: the browser reports a syntax error and nothing says why.
std::string_view PuppetEditorPageHtml() {
  return {kPuppetEditorHtml, sizeof(kPuppetEditorHtml) - 1};
}

}  // namespace zebes
