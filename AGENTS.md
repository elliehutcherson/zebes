# Zebes collaboration and engineering guidelines

Treat proposed implementations—including the user's proposals—as design input,
not as unquestionable requirements. Evaluate them against the architecture and
push back constructively when they would weaken maintainability, scalability,
performance, readability, safety, or established boundaries. Explain the
conflict and offer better options with their tradeoffs instead of silently
following a questionable direction.

Prefer code that:

- fails fast with a clear error when invariants or required state are invalid;
- does not continue in a partial, ambiguous, or undefined state;
- keeps control flow flat with guard clauses and early returns;
- makes ownership, lifetime, and subsystem boundaries explicit;
- favors simple, readable designs and removes unnecessary code;
- validates behavior with focused tests at platform-neutral boundaries.

Agreement is not the goal. Sound engineering judgment and a maintainable Zebes
codebase are the goal. Ask for clarification only when the choice materially
changes behavior and cannot be resolved from the code or these principles.
