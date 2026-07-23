// UF8 extraction — the V-Pot grid and the NESTED strip bindings.
//
// Regression: strip coverage read zero forever because uf8Coverage checked flat
// keys (faderVst3Param) while the on-disk shape is nested ({fader:{vst3Param}}) —
// UserPluginCatalog.cpp:567. These fixtures mirror the real serialised shape.

import test from 'node:test';
import assert from 'node:assert/strict';
import { uf8Coverage, extractUf8 } from '../src/lib/rea60map.js';

// A UF8 map: fader bank 0 has two bound V-Pots and one fully-bound strip;
// everything else empty. Shapes match the extension's serializer exactly.
function uf8Map() {
  const emptyVpotBank = () => Array.from({ length: 8 }, () => ({ vst3Param: -1 }));
  const banks = [
    Array.from({ length: 8 }, () => emptyVpotBank()),  // fader bank 0
    Array.from({ length: 8 }, () => emptyVpotBank()),  // fader bank 1
  ];
  banks[0][0][0] = { vst3Param: 1, label: 'InputGain' };
  banks[0][0][1] = { vst3Param: 2, label: 'Mix' };

  const emptyStrip = () => ({
    fader: { vst3Param: -1, label: '' }, solo: { vst3Param: -1 },
    cut: { vst3Param: -1 }, sel: { vst3Param: -1 },
  });
  const strips = [
    Array.from({ length: 8 }, () => emptyStrip()),
    Array.from({ length: 8 }, () => emptyStrip()),
  ];
  strips[0][0] = {
    fader: { vst3Param: 10, label: 'Vol' }, solo: { vst3Param: 11 },
    cut: { vst3Param: 12 }, sel: { vst3Param: 13 },
  };

  return {
    domain: 'None', uf8Mode: true,
    paramSnapshot: [
      { vst3Param: 1, name: 'Input Gain' }, { vst3Param: 2, name: 'Dry/Wet' },
      { vst3Param: 10, name: 'Volume' }, { vst3Param: 11, name: 'Solo' },
      { vst3Param: 12, name: 'Cut' }, { vst3Param: 13, name: 'Select' },
    ],
    uf8: { banksByFaderBank: banks, stripsByFaderBank: strips },
  };
}

test('uf8Coverage counts nested strips (regression: was always 0)', () => {
  const { vpots, strips } = uf8Coverage(uf8Map());
  assert.equal(vpots, 2);
  assert.equal(strips, 1, 'one strip has bindings — must not read zero');
});

test('extractUf8 returns V-Pot slots with coordinates + label + param', () => {
  const { vpots } = extractUf8(uf8Map());
  assert.equal(vpots.length, 2);
  const first = vpots.find((v) => v.strip === 0);
  assert.deepEqual(
    { fb: first.faderBank, vb: first.vpotBank, s: first.strip, label: first.label, param: first.paramName },
    { fb: 0, vb: 0, s: 0, label: 'InputGain', param: 'Input Gain' },
  );
});

test('extractUf8 returns one row per bound strip control (4 kinds)', () => {
  const { strips } = extractUf8(uf8Map());
  assert.equal(strips.length, 4);
  assert.deepEqual(strips.map((s) => s.kind).sort(), ['cut', 'fader', 'sel', 'solo']);
  const fader = strips.find((s) => s.kind === 'fader');
  assert.equal(fader.paramName, 'Volume');
  assert.equal(fader.faderBank, 0);
  assert.equal(fader.strip, 0);
});
