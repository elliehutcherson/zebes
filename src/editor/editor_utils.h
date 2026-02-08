#pragma once

namespace zebes {

class GuiInterface;

// Calculates the width of a button such that 'num_buttons' fit evenly in the available width.
float CalculateButtonWidth(GuiInterface* gui, int num_buttons);

}  // namespace zebes
