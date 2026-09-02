#!/usr/bin/env bash
# Ensure the ComfyUI port-forward to derry is up, and that ComfyUI is running
# behind it. Safe to run repeatedly.
#
# ComfyUI binds loopback on derry and is reached over SSH rather than by opening
# port 8188 on the LAN: it has no authentication, so anything that can reach the
# port can queue jobs and read outputs.
set -euo pipefail

HOST="${ZEBES_COMFY_HOST:-derry}"
PORT="${ZEBES_COMFY_PORT:-8188}"

if curl -sf -m 3 "http://127.0.0.1:${PORT}/system_stats" > /dev/null 2>&1; then
  echo "tunnel already up"
  exit 0
fi

pkill -f "ssh -f -N -L ${PORT}:127.0.0.1:${PORT}" 2>/dev/null || true

if ! ssh -o BatchMode=yes -o ConnectTimeout=8 "$HOST" true 2>/dev/null; then
  echo "cannot reach ${HOST} over SSH. Is the machine powered on?" >&2
  exit 1
fi

if ! ssh -o BatchMode=yes "$HOST" \
     "curl -sf -m 3 http://127.0.0.1:${PORT}/system_stats > /dev/null"; then
  echo "starting ComfyUI on ${HOST}"
  ssh -o BatchMode=yes "$HOST" bash -s <<REMOTE
cd ~/ComfyUI && . venv/bin/activate
setsid nohup python main.py --listen 127.0.0.1 --port ${PORT} \
  > ~/comfyui.log 2>&1 < /dev/null &
for i in \$(seq 1 60); do
  curl -sf -m 2 http://127.0.0.1:${PORT}/system_stats > /dev/null 2>&1 && exit 0
  sleep 1
done
echo "ComfyUI did not come up; see ~/comfyui.log on ${HOST}" >&2
exit 1
REMOTE
fi

ssh -f -N -L "${PORT}:127.0.0.1:${PORT}" "$HOST"
for _ in $(seq 1 15); do
  if curl -sf -m 2 "http://127.0.0.1:${PORT}/system_stats" > /dev/null 2>&1; then
    echo "tunnel up on 127.0.0.1:${PORT}"
    exit 0
  fi
  sleep 1
done

echo "forward opened but ComfyUI did not answer through it" >&2
exit 1
