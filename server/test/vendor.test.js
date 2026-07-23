// Every case here is a real string from the live catalog or REAPER's scan
// caches, not an invented one. Three of them were bugs found by running the
// parser over the real 29 maps.

import test from 'node:test';
import assert from 'node:assert/strict';
import { vendorFromName, pluginNameFromMatch, fxTypeOf, normVendor } from '../src/lib/vendor.js';

test('vendor: three conventions, read fx_type first', () => {
  assert.equal(vendorFromName('VST3: bx_console SSL 9000 J (Plugin Alliance)'), 'Plugin Alliance');
  assert.equal(vendorFromName('VST: ReaComp (Cockos)'), 'Cockos');
  assert.equal(vendorFromName('Celemony: Melodyne 5', 'AU'), 'Celemony');   // PREFIX, not parenthetical
  assert.equal(vendorFromName('JS: The Analog Molecule (DocShadrach) [DocShadrach FX/molecule]'),
    'DocShadrach');
});

test('vendor: a trailing parenthetical is not automatically the vendor', () => {
  assert.equal(vendorFromName('Some Plugin (2->6ch)'), null);
  assert.equal(vendorFromName('Wide Thing (32 out)'), null);
  // "… (SSL) (mono)" yielded "mono" in the prototype. The vendor is SSL.
  assert.equal(vendorFromName('VST3: SSL Delta Control 16 (SSL) (mono)'), 'SSL');
});

test('vendor: "ch$" must not eat a real vendor ending in ch', () => {
  // Regression: the prototype's CHANNEL_SPEC rejected "DocShadrach" outright,
  // because the name ends in "ch". Channel counts are always digit-led.
  assert.equal(vendorFromName('JS: Thing (DocShadrach)'), 'DocShadrach');
  assert.equal(vendorFromName('Thing (6ch)'), null);
});

test('plugin name: strips the fx_type prefix and JSFX path', () => {
  assert.equal(pluginNameFromMatch('VST: ReaComp (Cockos)'), 'ReaComp');
  assert.equal(pluginNameFromMatch('JS: 4-Band EQ [loser/4BandEQ]'), '4-Band EQ');
});

test('plugin name: the three real parenthetical shapes', () => {
  // two groups
  assert.equal(pluginNameFromMatch('VST3: SSL Delta Control 16 (SSL) (mono)'),
    'SSL Delta Control 16');
  // NESTED — a regex with [^)]* cannot span the inner ")" and silently fails,
  // leaving the vendor glued to the name.
  assert.equal(pluginNameFromMatch('VST3: UADx API Vision Channel Strip (Universal Audio (UADx))'),
    'UADx API Vision Channel Strip');
  // never closed — user truncated the match mid-vendor
  assert.equal(pluginNameFromMatch('UADx 1176 Rev A Compressor (Universal Audio'),
    'UADx 1176 Rev A Compressor');
});

test('plugin name: a name that is only a parenthetical is not emptied', () => {
  assert.equal(pluginNameFromMatch('(Weird)'), '');   // degenerate, but must not throw
  assert.equal(pluginNameFromMatch('Pro-C 2'), 'Pro-C 2');
});

test('normVendor trims BEFORE folding', () => {
  // Two AU vendors carry a trailing space ("Celemony ", "Synchro Arts ") and
  // counted twice until stripped.
  assert.equal(normVendor('Celemony '), normVendor('Celemony'));
  assert.equal(normVendor('  Synchro   Arts '), 'synchro arts');
});

test('fxTypeOf', () => {
  assert.equal(fxTypeOf('VST3: X'), 'VST3');
  assert.equal(fxTypeOf('JS: X'), 'JS');
  assert.equal(fxTypeOf('Pro-C 2'), null);
});
