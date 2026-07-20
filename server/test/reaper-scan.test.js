// developerForMatch against a fake REAPER resource dir, so the test is
// hermetic (not dependent on what's installed on the machine).

import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtempSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { tmpdir } from 'node:os';

const { developerForMatch, _resetIndex } = await import('../src/lib/reaper-scan.js');

const dir = mkdtempSync(join(tmpdir(), 'reaper-scan-'));
// VST cache: file=ts,id,Display Name (Developer)
writeFileSync(join(dir, 'reaper-vstplugins_arm64.ini'), [
  '[vstcache]',
  'echoboy.vst3=1,111{guid},EchoBoy (Soundtoys)',
  'microshift.vst3=1,112{guid},Little MicroShift (Soundtoys)',
  'proc.vst3=1,113{guid},Pro-C 2 (FabFilter)',
  'eq.vst3=1,114{guid},EQ (Airwindows)',
  'tiger.vst3=1,115{guid},FIRETHETIGERT (Acqua)',
].join('\n'));
// JSFX cache: NAME <path> "JS: Name (Dev) [author/script]"
writeFileSync(join(dir, 'reaper-jsfx.ini'),
  'NAME loser/4BandEQ "JS: 4-Band EQ [loser/4BandEQ]"\n');
_resetIndex();

const dev = (m) => developerForMatch(m, { dir });

test('exact clean-name match', () => {
  assert.equal(dev('EchoBoy'), 'Soundtoys');
});

test('shortened match (user typed less than the real name)', () => {
  assert.equal(dev('MicroShift'), 'Soundtoys');   // ⊂ "Little MicroShift"
});

test('padded match (user typed more than the cache name)', () => {
  assert.equal(dev('FabFilter Pro-C 2'), 'FabFilter');  // ⊃ "Pro-C 2"
  assert.equal(dev('Pro-C 2'), 'FabFilter');
});

test('a short generic word does not cause a false match', () => {
  // "4-Band EQ" must NOT resolve to the Airwindows "EQ" plug-in.
  assert.notEqual(dev('JS: 4-Band EQ [loser/4BandEQ]'), 'Airwindows');
});

test('JSFX author comes from the [author/...] path', () => {
  assert.equal(dev('JS: 4-Band EQ [loser/4BandEQ]'), 'loser');
});

test('substantial substring still matches (shortened long name)', () => {
  assert.equal(dev('TIGERT'), 'Acqua');   // ⊂ "FIRETHETIGERT", 6 chars, >= floor
});

test('unknown plug-in returns null', () => {
  assert.equal(dev('Totally Unknown Thing'), null);
});
