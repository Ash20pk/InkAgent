import { createServer } from 'node:http';

export function json(res, status, body, headers = {}) {
  const buf = Buffer.from(JSON.stringify(body));
  res.writeHead(status, { 'content-type': 'application/json', 'content-length': buf.length, ...headers });
  res.end(buf);
}
export function html(res, status, body, headers = {}) {
  res.writeHead(status, { 'content-type': 'text/html; charset=utf-8', ...headers });
  res.end(body);
}
export async function readBody(req, limit = 8192) {
  const chunks = []; let size = 0;
  for await (const c of req) { size += c.length; if (size > limit) throw new Error('body too large'); chunks.push(c); }
  const raw = Buffer.concat(chunks).toString('utf8');
  if (!raw) return {};
  const ct = req.headers['content-type'] || '';
  if (ct.includes('application/x-www-form-urlencoded')) return Object.fromEntries(new URLSearchParams(raw));
  return JSON.parse(raw);
}
export function cookies(req) {
  return Object.fromEntries((req.headers.cookie || '').split(';').map(s => s.trim()).filter(Boolean)
    .map(kv => { const i = kv.indexOf('='); return [kv.slice(0, i), decodeURIComponent(kv.slice(i + 1))]; }));
}

// Tiny router: routes[`${METHOD} ${pathname}`] = async (ctx) => void
export function makeServer(routes, { onError = console.error } = {}) {
  return createServer(async (req, res) => {
    const url = new URL(req.url, 'http://x');
    const key = `${req.method} ${url.pathname}`;
    const handler = routes[key];
    if (!handler) return json(res, 404, { error: 'not_found' });
    try {
      await handler({ req, res, url, query: Object.fromEntries(url.searchParams) });
    } catch (e) {
      onError(e);
      if (!res.headersSent) json(res, e.message === 'body too large' ? 413 : 500, { error: 'internal', text: 'Relay error. Try again.' });
    }
  });
}
