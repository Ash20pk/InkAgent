# Deploying the relay

1. DNS: `A relay.inkagent.dev → <droplet IP>`.
2. Copy the repo: `rsync -az --exclude node_modules --exclude '*.sqlite*' ~/inkagent/relay ~/inkagent/deploy root@<ip>:/opt/inkagent/`
3. On the droplet: `cp /opt/inkagent/deploy/.env.example /opt/inkagent/deploy/.env`, set `INK_ACCESS_CODE`, then `bash /opt/inkagent/deploy/setup-droplet.sh`.
4. Check: `curl https://relay.inkagent.dev/health` → `{"ok":true}`.

Caddy fetches and renews the Let's Encrypt certificate itself once DNS resolves.
Update: rsync again, then `docker compose up -d --build` in `/opt/inkagent/deploy`.
Backup: the SQLite DB is the `relay-data` volume (`docker run --rm -v inkagent_relay-data:/d alpine cat /d/inkagent.sqlite > backup.sqlite`).
