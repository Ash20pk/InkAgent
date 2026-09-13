#!/usr/bin/env bash
# One-shot setup on a fresh Ubuntu droplet. Run as root:
#   bash setup-droplet.sh
# Expects the repo (relay/ and deploy/) already copied to /opt/inkagent.
set -euo pipefail
cd /opt/inkagent/deploy
if ! command -v docker >/dev/null; then
  apt-get update -qq && apt-get install -y -qq ca-certificates curl ufw >/dev/null
  curl -fsSL https://get.docker.com | sh >/dev/null
fi
ufw allow OpenSSH >/dev/null; ufw allow 80/tcp >/dev/null; ufw allow 443/tcp >/dev/null; ufw --force enable >/dev/null
[ -f .env ] || { cp .env.example .env; echo "edit /opt/inkagent/deploy/.env (OAuth client id and secret) then re-run"; exit 1; }
docker compose up -d --build
docker compose ps
