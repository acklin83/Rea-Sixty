// Parameter coverage (v3): distinct mapped params / functional param count.

import test from 'node:test';
import assert from 'node:assert/strict';
import { parseRea60Map } from '../src/lib/rea60map.js';

function envelope({ functionalParams, slots }) {
  const inner = {
    format_version: 10,
    plugins: [{
      match: 'Thing', domain: 'BusComp', uf8Mode: false,
      slots, paramSnapshot: slots.map((s) => ({ vst3Param: s.vst3Param, name: `p${s.vst3Param}` })),
    }],
  };
  const env = {
    format: 'rea-sixty-map', version: 3, plugin: 'Thing', original_name: 'Thing (x)',
    vendor: '', surfaces: 'uc1', author: 'a', description: '', licence: 'CC0-1.0',
    created_at: 0, map: JSON.stringify(inner),
  };
  if (functionalParams !== undefined) env.functional_params = functionalParams;
  return JSON.stringify(env);
}

test('parameter coverage = distinct mapped params / functional count', () => {
  const r = parseRea60Map(envelope({
    functionalParams: 11,
    slots: [{ linkIdx: 1, vst3Param: 1 }, { linkIdx: 2, vst3Param: 2 }, { linkIdx: 7, vst3Param: 3 }],
  }));
  assert.deepEqual(r.paramCoverage, { n: 3, d: 11 });
});

test('a param mapped to two controls counts once', () => {
  const r = parseRea60Map(envelope({
    functionalParams: 10,
    slots: [{ linkIdx: 1, vst3Param: 5 }, { linkIdx: 2, vst3Param: 5 }],
  }));
  assert.deepEqual(r.paramCoverage, { n: 1, d: 10 });
});

test('numerator is clamped to the denominator', () => {
  // If someone maps more distinct params than the functional count (e.g. wrapper
  // params the extension excluded from the denominator), don't exceed 100%.
  const r = parseRea60Map(envelope({
    functionalParams: 2,
    slots: [{ linkIdx: 1, vst3Param: 1 }, { linkIdx: 2, vst3Param: 2 }, { linkIdx: 7, vst3Param: 3 }],
  }));
  assert.deepEqual(r.paramCoverage, { n: 2, d: 2 });
});

test('no functional_params (pre-v3) -> paramCoverage null', () => {
  const r = parseRea60Map(envelope({
    slots: [{ linkIdx: 1, vst3Param: 1 }],
  }));
  assert.equal(r.paramCoverage, null);
});

test('functional_params of 0 or -1 -> null (Acustica / not captured)', () => {
  assert.equal(parseRea60Map(envelope({ functionalParams: 0, slots: [{ linkIdx: 1, vst3Param: 1 }] })).paramCoverage, null);
  assert.equal(parseRea60Map(envelope({ functionalParams: -1, slots: [{ linkIdx: 1, vst3Param: 1 }] })).paramCoverage, null);
});
