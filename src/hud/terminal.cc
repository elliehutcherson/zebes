#include "hud/terminal.h"

#include "common/logging.h"
#include "imgui.h"

namespace zebes {

void HudTerminal::Render() {
  ImGui::BeginChild("Log");
  ImGui::TextUnformatted(HudLogSink::Get()->log()->c_str());
  if (log_size_ != HudLogSink::Get()->log()->size()) {
    ImGui::SetScrollHereY(1.0f);
    log_size_ = HudLogSink::Get()->log()->size();
  }
  ImGui::EndChild();
}

}  // namespace zebes
