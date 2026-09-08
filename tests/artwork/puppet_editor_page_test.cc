#include "artwork/puppet_editor_page.h"

#include <string_view>

#include "gtest/gtest.h"

namespace zebes {
namespace {

// The page reaches the browser as one string with no length beside it, so a
// short read anywhere between the HTML file and the socket looks like a working
// server serving a page whose script stops mid-statement. Checking both ends
// costs nothing and is the only thing that says the whole file arrived.
TEST(PuppetEditorPageTest, TheEmbeddedPageIsWholeFromDoctypeToClosingTag) {
  const std::string_view page = PuppetEditorPageHtml();
  EXPECT_TRUE(page.starts_with("<!doctype html>")) << page.substr(0, 40);
  EXPECT_TRUE(page.ends_with("</html>\n")) << page.substr(page.size() - 40);
}

// A zero byte in the source HTML used to end the page early, because a
// string_view built from a char array takes its length from the terminator.
TEST(PuppetEditorPageTest, TheEmbeddedPageCarriesNoZeroByte) {
  EXPECT_EQ(PuppetEditorPageHtml().find('\0'), std::string_view::npos);
}

}  // namespace
}  // namespace zebes
