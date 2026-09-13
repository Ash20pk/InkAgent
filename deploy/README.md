# Deploying the relay

1. DNS: `A relay.inkagent.dev → <droplet IP>`.
2. Copy the repo: `rsync -az --exclude node_modules --exclude '*.sqlite*' ~/inkagent/relay ~/inkagent/deploy root@<ip>:/opt/inkagent/`
3. On the droplet: `cp /opt/inkagent/deploy/.env.example /opt/inkagent/deploy/.env`, then
   `bash /opt/inkagent/deploy/setup-droplet.sh`.
4. Open the relay, create your account, and add a passkey under **Account**.
   Then set `INK_SIGNUP_CLOSED=1` in `.env` and `docker compose up -d` so nobody
   else can register.
5. Check: `curl https://relay.inkagent.dev/health` → `{"ok":true}`.

Locked out, or holding an account from before passwords existed? Set one from
the host that has the database — it prompts, and never takes the password as an
argument, because arguments land in shell history and the process list:

```
docker exec -it deploy-relay-1 node --experimental-sqlite src/setpw.js you@example.com
```

Caddy fetches and renews the Let's Encrypt certificate itself once DNS resolves.
Update: rsync again, then `docker compose up -d --build` in `/opt/inkagent/deploy`.

**Never add `--delete` to that rsync.** `.env` lives only on the droplet and is
gitignored, so `--delete` removes it. Compose then starts with an empty
`RELAY_HOST`, the Caddyfile's site block becomes a global options block, and
Caddy crash-loops on `unrecognized global option: encode` with the site down.
Recover by writing `.env` again (`RELAY_HOST` plus the code in `.access-code`)
and running `docker compose up -d`.

## Backup

The DB is the `deploy_relay-data` volume — the compose project is the directory
name, so the volume is `deploy_relay-data`, not `inkagent_relay-data`.

SQLite runs in WAL mode here: most recent writes sit in `inkagent.sqlite-wal`,
and the main file can be 4 KB while the real data is a few hundred KB beside it.
Copying just `inkagent.sqlite` therefore backs up almost nothing. Use the backup
API, which checkpoints the WAL into the copy:

```
docker run --rm -v deploy_relay-data:/d alpine sh -c \
  'apk add --no-cache sqlite >/dev/null && sqlite3 /d/inkagent.sqlite ".backup /d/_bk.sqlite"'
docker run --rm -v deploy_relay-data:/d alpine cat /d/_bk.sqlite > backup.sqlite
docker run --rm -v deploy_relay-data:/d alpine rm -f /d/_bk.sqlite
```

Sanity-check the result: a backup that comes back at 4 KB captured an empty
database, not an empty deployment.
