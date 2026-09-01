"""Patching ComfyUI workflow templates.

Templates are not written by hand. Build the graph in the ComfyUI UI, enable dev
mode, and use **Save (API Format)** — that export is the template. Hand-authoring
the JSON means guessing node class names and input keys, and a wrong key fails
on the server rather than here.

Nodes are addressed by their **title**, not their numeric id. Ids shift whenever
the graph is edited in the UI, so a template patched by id silently starts
writing the seed into the wrong node. Give each node you intend to drive a title
beginning with `ZEBES_` in the UI (right-click, Title), and the template becomes
self-documenting: `list_handles` prints exactly which knobs a template exposes.
"""

from __future__ import annotations

import copy
import json
from pathlib import Path

# Titles the runner expects to find. A template need not expose all of them, but
# it may not expose one under a different spelling and still be driven.
HANDLE_PREFIX = "ZEBES_"

POSITIVE_PROMPT = "ZEBES_POSITIVE"
NEGATIVE_PROMPT = "ZEBES_NEGATIVE"
SEED = "ZEBES_SEED"
CONTROL_IMAGE = "ZEBES_CONTROL_IMAGE"
IDENTITY_IMAGE = "ZEBES_IDENTITY_IMAGE"
OUTPUT = "ZEBES_OUTPUT"


class WorkflowError(ValueError):
    """Raised when a template cannot be patched as asked."""


def load(path: Path) -> dict:
    """Load an API-format workflow export.

    The UI's ordinary save format is a different shape — it carries `nodes` and
    `links` arrays — and posting it to `/prompt` fails in a way that is hard to
    read, so it is rejected here with an explanation instead.
    """
    if not path.is_file():
        raise WorkflowError(f"no such workflow template: {path}")

    data = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(data, dict):
        raise WorkflowError(f"{path}: a workflow must be a JSON object")
    if "nodes" in data and "links" in data:
        raise WorkflowError(
            f"{path} is a UI-format workflow, not an API-format one. In ComfyUI "
            "enable dev mode in settings and use 'Save (API Format)'."
        )

    for node_id, node in data.items():
        if not isinstance(node, dict) or "class_type" not in node:
            raise WorkflowError(
                f"{path}: node {node_id!r} has no class_type, so this is not an "
                "API-format workflow"
            )
    return data


def title_of(node: dict) -> str | None:
    meta = node.get("_meta")
    if isinstance(meta, dict):
        title = meta.get("title")
        if isinstance(title, str):
            return title
    return None


def find(workflow: dict, title: str) -> str:
    """Return the id of the single node carrying `title`.

    Two nodes sharing a title is an authoring mistake in the template, and
    picking either one would make the run's behaviour depend on dict ordering.
    """
    matches = [
        node_id for node_id, node in workflow.items() if title_of(node) == title
    ]
    if not matches:
        available = sorted(list_handles(workflow))
        raise WorkflowError(
            f"no node titled {title!r}. Titles beginning with {HANDLE_PREFIX} in "
            f"this template: {available or 'none'}. Set the title in the ComfyUI "
            "UI with right-click > Title."
        )
    if len(matches) > 1:
        raise WorkflowError(
            f"{len(matches)} nodes are titled {title!r} (ids {sorted(matches)}); "
            "titles used as handles must be unique"
        )
    return matches[0]


def list_handles(workflow: dict) -> dict[str, str]:
    """Every `ZEBES_`-titled node in the template, as title to node id."""
    return {
        title: node_id
        for node_id, node in workflow.items()
        if (title := title_of(node)) is not None and title.startswith(HANDLE_PREFIX)
    }


def set_input(workflow: dict, title: str, key: str, value) -> dict:
    """Return a copy of `workflow` with one input replaced.

    The key must already exist. Adding an input that the node does not declare
    produces a validation failure on the server that names the node but not the
    caller, so it is caught here where the template is in hand.
    """
    patched = copy.deepcopy(workflow)
    node_id = find(patched, title)
    inputs = patched[node_id].setdefault("inputs", {})

    if key not in inputs:
        raise WorkflowError(
            f"node {title!r} ({patched[node_id]['class_type']}) has no input "
            f"{key!r}; it has {sorted(inputs)}"
        )
    inputs[key] = value
    return patched


def apply(workflow: dict, patches: dict[str, dict[str, object]]) -> dict:
    """Apply several title/key/value patches at once.

    Patches are applied to one copy so a failure part-way leaves the caller's
    template untouched rather than half-written.
    """
    patched = copy.deepcopy(workflow)
    for title, values in patches.items():
        for key, value in values.items():
            patched = set_input(patched, title, key, value)
    return patched


def describe(workflow: dict) -> str:
    """A readable summary of a template's handles, for the CLI."""
    handles = list_handles(workflow)
    if not handles:
        return (
            f"This template exposes no {HANDLE_PREFIX} handles. Title the nodes "
            "you want to drive in the ComfyUI UI and re-export."
        )
    lines = [f"{len(handles)} handle(s):"]
    for title, node_id in sorted(handles.items()):
        node = workflow[node_id]
        inputs = sorted(node.get("inputs", {}))
        lines.append(f"  {title:24s} node {node_id:>4s}  {node['class_type']}")
        lines.append(f"  {'':24s} inputs: {', '.join(inputs) or 'none'}")
    return "\n".join(lines)
