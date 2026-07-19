import Database from 'better-sqlite3';
import { readFileSync, mkdirSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';

export const DATA_DIR = resolve(process.env.DATA_DIR ?? join(process.cwd(), 'var'));
export const DB_PATH = process.env.DB_PATH ?? join(DATA_DIR, 'exchange.db');
export const MAPS_DIR = join(DATA_DIR, 'maps');

let db;

export function getDb() {
  if (db) return db;
  mkdirSync(dirname(DB_PATH), { recursive: true });
  mkdirSync(MAPS_DIR, { recursive: true });
  db = new Database(DB_PATH);
  db.pragma('journal_mode = WAL');
  db.pragma('foreign_keys = ON');
  return db;
}

export function migrate() {
  const d = getDb();
  const schema = readFileSync(new URL('./schema.sql', import.meta.url), 'utf8');
  d.exec(schema);
  return d;
}

export function now() {
  return Math.floor(Date.now() / 1000);
}
