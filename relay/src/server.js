import { openDb } from './db.js';
import { makeServer } from './http.js';
import { deviceRoutes } from './routes/device.js';
import { dashboardRoutes } from './routes/dashboard.js';

export function createApp({ dbPath, publicUrl, fetchImpl = fetch } = {}) {
  const db = openDb(dbPath);
  const dev = deviceRoutes(db, { publicUrl, fetchImpl });
  const dash = dashboardRoutes(db, { devTokens: dev._internal.pendingToken, publicUrl, fetchImpl });
  const { _internal, ...devPublic } = dev;
  return { db, server: makeServer({ ...devPublic, ...dash }) };
}

if (import.meta.url === `file://${process.argv[1]}`) {
  const port = Number(process.env.PORT || 8787);
  const publicUrl = process.env.PUBLIC_URL || `http://localhost:${port}`;
  const { server } = createApp({ publicUrl });
  server.listen(port, () => console.log(`inkagent relay on ${publicUrl}`));
}
