# Twelve-frame run phase audit

This is a diagnostic of the unchanged supplied pose sheet and retained trace. It makes no provider calls.

Frames 1–5 use the near leg for contact through toe-off, frame 6 is flight, frames 7–11 repeat the stance phases with the far leg, and frame 12 is flight. Frame 10 is therefore far late-support with the near leg folded in recovery. Its traced near chain runs knee `(141,201)` to ankle `(99,184)` to toe `(86,199)` in source-cell pixels: the ankle is higher than the toe, matching a raised-heel side-profile foot.

The trace has no heel landmark or sole-contact segment. The rejected boot proxy inferred a 3D boot from that incomplete representation and used an oblique camera. Do not use it for another generation request. Add and review explicit heel/sole semantics first.
