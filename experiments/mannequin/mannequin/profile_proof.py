"""Generate a bounded four-pose proof from one bound profile identity."""

from __future__ import annotations

import hashlib
import json
from dataclasses import dataclass
from pathlib import Path

from . import workflow
from .comfy_client import ComfyClient
from .profile_bind import POSES


class ProfileProofError(ValueError):
    """Raised when a four-pose proof contract is incomplete."""


@dataclass(frozen=True)
class ProfileProofConfig:
    prompt: str
    seed: int = 42
    control_strength: float = 0.5
    control_end_percent: float = 0.6
    identity_weight: float = 0.7


def _digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def generate(
    client: ComfyClient,
    binding_dir: Path,
    identity: Path,
    workflow_path: Path,
    out: Path,
    config: ProfileProofConfig,
) -> None:
    """Generate exactly neutral/contact/passing/airborne, with one identity."""
    if out.exists():
        raise ProfileProofError(f"{out} already exists; proof directories are immutable")
    if not identity.is_file():
        raise ProfileProofError(f"identity image does not exist: {identity}")
    template = workflow.load(workflow_path)
    guide_paths = {
        pose: binding_dir / f"pose-{pose}-control.png" for pose in POSES
    }
    missing = [str(path) for path in guide_paths.values() if not path.is_file()]
    if missing:
        raise ProfileProofError(f"profile binding is missing guides: {missing}")

    uploaded_identity = client.upload_image(identity)
    records = []
    for pose in POSES:
        guide = guide_paths[pose]
        uploaded_guide = client.upload_image(guide)
        patched = workflow.apply(
            template,
            {
                workflow.CONTROL_IMAGE: {"image": uploaded_guide.reference},
                workflow.IDENTITY_IMAGE: {"image": uploaded_identity.reference},
                workflow.POSITIVE_PROMPT: {"text": config.prompt},
                workflow.SEED: {"seed": config.seed},
                "ZEBES_CONTROL_STRENGTH": {
                    "strength": config.control_strength,
                    "end_percent": config.control_end_percent,
                },
                "ZEBES_IDENTITY_STRENGTH": {
                    "weight": config.identity_weight,
                    "weight_type": "linear",
                },
            },
        )
        generated = client.run(patched, out / "generated")[0]
        final = out / "generated" / f"{pose}.png"
        generated.rename(final)
        records.append(
            {
                "pose": pose,
                "guide": str(guide),
                "guide_sha256": _digest(guide),
                "output": str(final.relative_to(out)),
                "output_sha256": _digest(final),
            }
        )
    manifest = {
        "version": 1,
        "goal": (
            "Test whether four different bound poses retain one source identity; "
            "this does not test a complete animation cycle."
        ),
        "identity": str(identity),
        "identity_sha256": _digest(identity),
        "workflow": str(workflow_path),
        "workflow_sha256": _digest(workflow_path),
        "prompt": config.prompt,
        "seed": config.seed,
        "control_strength": config.control_strength,
        "control_end_percent": config.control_end_percent,
        "identity_weight": config.identity_weight,
        "poses": records,
    }
    out.mkdir(parents=True, exist_ok=True)
    (out / "manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True), encoding="utf-8"
    )
