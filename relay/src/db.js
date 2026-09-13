// SQLite store. node:sqlite is built in from Node 22.13; in-memory for tests.
import { DatabaseSync } from 'node:sqlite';
import { randomBytes, createHash } from 'node:crypto';

export function openDb(path = process.env.INK_DB || 'inkagent.sqlite') {
  const db = new DatabaseSync(path);
  db.exec(`
    PRAGMA journal_mode = WAL;
    CREATE TABLE IF NOT EXISTS users (
      id TEXT PRIMARY KEY, email TEXT UNIQUE NOT NULL, created_at INTEGER NOT NULL);
    CREATE TABLE IF NOT EXISTS sessions (
      id TEXT PRIMARY KEY, user_id TEXT NOT NULL, created_at INTEGER NOT NULL);
    CREATE TABLE IF NOT EXISTS pairings (
      device_code TEXT PRIMARY KEY, user_code TEXT UNIQUE NOT NULL, hw TEXT NOT NULL,
      fw TEXT, budget INTEGER NOT NULL, status TEXT NOT NULL, user_id TEXT,
      device_id TEXT, expires_at INTEGER NOT NULL, created_at INTEGER NOT NULL);
    CREATE TABLE IF NOT EXISTS devices (
      id TEXT PRIMARY KEY, user_id TEXT NOT NULL, hw TEXT NOT NULL, name TEXT NOT NULL,
      token_hash TEXT UNIQUE NOT NULL, budget INTEGER NOT NULL, revoked INTEGER NOT NULL DEFAULT 0,
      created_at INTEGER NOT NULL, last_seen INTEGER);
    CREATE TABLE IF NOT EXISTS providers (
      user_id TEXT PRIMARY KEY, kind TEXT NOT NULL, base_url TEXT NOT NULL,
      api_key TEXT, model TEXT NOT NULL, updated_at INTEGER NOT NULL);
    CREATE TABLE IF NOT EXISTS apps (
      id TEXT PRIMARY KEY, user_id TEXT NOT NULL, name TEXT NOT NULL, icon TEXT NOT NULL,
      manifest TEXT NOT NULL, updated_at INTEGER NOT NULL);
    CREATE INDEX IF NOT EXISTS apps_by_user ON apps (user_id);
    CREATE TABLE IF NOT EXISTS turns (
      sid TEXT PRIMARY KEY, device_id TEXT NOT NULL, kind TEXT NOT NULL, request TEXT NOT NULL,
      full_text TEXT NOT NULL, sent_text TEXT NOT NULL, truncated INTEGER NOT NULL,
      model TEXT, latency_ms INTEGER, created_at INTEGER NOT NULL);
  `);
  return db;
}

export const now = () => Math.floor(Date.now() / 1000);
export const id = (n = 12) => randomBytes(n).toString('base64url');
export const sha = (s) => createHash('sha256').update(s).digest('hex');

// User codes avoid 0/O/1/I so they survive being read off e-ink.
const ALPHABET = 'ABCDEFGHJKLMNPQRSTUVWXYZ23456789';
export function userCode() {
  const b = randomBytes(8);
  let s = '';
  for (let i = 0; i < 8; i++) s += ALPHABET[b[i] % ALPHABET.length];
  return s.slice(0, 4) + '-' + s.slice(4);
}
