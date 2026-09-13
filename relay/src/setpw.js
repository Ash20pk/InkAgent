// Set an account's password from the server.
//
// Two situations need this and neither can be solved from the browser: an
// account created before passwords existed has no way to sign in, and a
// forgotten password has nowhere to go while the relay sends no email.
//
// Run it on the host that has the database:
//
//   docker exec -it deploy-relay-1 node --experimental-sqlite src/setpw.js you@example.com
//
// It prompts. The password is never an argument, because arguments end up in
// shell history and in the process list where anyone on the box can read them.
import { DatabaseSync } from 'node:sqlite';
import { createInterface } from 'node:readline';
import { hashPassword, verifyPassword, passwordProblem } from './password.js';

const email = String(process.argv[2] || '').trim().toLowerCase();
if (!email) {
  console.error('usage: node --experimental-sqlite src/setpw.js <email>');
  process.exit(2);
}

// A terminal and a pipe need different handling, and pretending otherwise is
// why this failed twice. On a terminal, readline prompts and the echo is
// suppressed. On a pipe every line arrives at once, so readline's question()
// misses all but the first — the input is read up front and handed out
// instead.
const interactive = Boolean(process.stdin.isTTY);
let piped = null;

async function readAllStdin() {
  const chunks = [];
  for await (const c of process.stdin) chunks.push(c);
  return Buffer.concat(chunks).toString('utf8').split(/\r?\n/);
}

const rl = interactive
  ? createInterface({ input: process.stdin, output: process.stdout, terminal: true })
  : null;

async function ask(prompt) {
  if (!interactive) {
    if (piped === null) piped = await readAllStdin();
    return String(piped.shift() ?? '').trim();
  }
  return new Promise((resolve) => {
    const hide = (ch) => {
      const c = ch.toString();
      if (c !== '\r' && c !== '\n') process.stdout.write('\u0008 \u0008');
    };
    process.stdin.on('data', hide);
    rl.question(prompt, (answer) => {
      process.stdin.off('data', hide);
      process.stdout.write('\n');
      resolve(answer.trim());
    });
  });
}

const done = (code) => { if (rl) rl.close(); process.exit(code); };

const db = new DatabaseSync(process.env.INK_DB || '/data/inkagent.sqlite');
const user = db.prepare('SELECT id, email FROM users WHERE email = ?').get(email);
if (!user) {
  console.error(`No account for ${email}. Accounts on this relay:`);
  for (const u of db.prepare('SELECT email FROM users ORDER BY email').all()) console.error(`  ${u.email}`);
  process.exit(1);
}

const password = await ask(`New password for ${user.email}: `);
const again = await ask('Again: ');

if (password !== again) {
  console.error('Those did not match. Nothing was changed.');
  done(1);
}
const problem = passwordProblem(password);
if (problem) {
  console.error(problem + ' Nothing was changed.');
  done(1);
}

const hash = await hashPassword(password);
db.prepare(`INSERT INTO passwords (user_id,hash,updated_at) VALUES (?,?,?)
            ON CONFLICT(user_id) DO UPDATE SET hash=excluded.hash, updated_at=excluded.updated_at`)
  .run(user.id, hash, Math.floor(Date.now() / 1000));

// Read it back rather than trusting the write: a password that does not verify
// locks the account out entirely, and this is the last chance to notice.
const stored = db.prepare('SELECT hash FROM passwords WHERE user_id = ?').get(user.id);
if (!(await verifyPassword(password, stored.hash))) {
  console.error('The password was written but does not verify. Nothing can sign in with it; investigate before relying on this.');
  done(1);
}

// Existing sessions survive on purpose: changing your own password from the
// server should not sign you out of the browser you are already using.
console.log(`Password set for ${user.email}. Sign in, then add a passkey under Account.`);
done(0);
