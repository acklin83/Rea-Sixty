import { combineRgb } from '@companion-module/base'

// v2 signature: setPresetDefinitions(structure, presets).
//   structure = [{ id, name, definitions: [presetId, ...] }, ...]  (sections)
//   presets   = { presetId: { type:'simple', name, style, steps, feedbacks } }
//
// The builtin-action presets are generated LIVE from the Rea-Sixty catalogue so
// their action names always match the installed extension. Meter/status presets
// use only feedbacks + variables, so they never reference a wrong action id.

const WHITE = combineRgb(255, 255, 255)
const BLACK = combineRgb(0, 0, 0)
const DARK = combineRgb(20, 20, 24)

export default function (self) {
	const structure = []
	const presets = {}
	// Variable references in button text use the connection's own label as the
	// prefix (e.g. "$(Rea-Sixty:sel_track_name)"). self.label is the user's
	// chosen connection name; presets are rebuilt on configUpdated when it can
	// change, so the prefix stays correct.
	const P = self.label || 'Rea-Sixty'

	// ---- Rea-Sixty action presets, grouped by category ---------------------
	const items = Array.isArray(self.builtins) ? self.builtins : []
	const catOrder = Array.isArray(self.builtinCats) ? self.builtinCats : []
	const byCat = new Map()
	for (const it of items) {
		const cat = it.c || 'Other'
		if (!byCat.has(cat)) byCat.set(cat, [])
		byCat.get(cat).push(it)
	}
	const orderedCats = [...catOrder.filter((c) => byCat.has(c)), ...[...byCat.keys()].filter((c) => !catOrder.includes(c))]
	orderedCats.forEach((cat, ci) => {
		const ids = []
		for (const it of byCat.get(cat).sort((a, b) => String(a.d || a.n).localeCompare(String(b.d || b.n)))) {
			const pid = `builtin_${it.n}`
			presets[pid] = {
				type: 'simple',
				name: it.d || it.n,
				style: { text: it.d || it.n, size: 'auto', color: WHITE, bgcolor: DARK },
				steps: [{ down: [{ actionId: 'builtin', options: { builtin: it.n, param: 0 } }], up: [] }],
				feedbacks: [],
			}
			ids.push(pid)
		}
		if (ids.length) structure.push({ id: `cat_${ci}`, name: cat, definitions: ids })
	})

	// ---- Meter presets, one per configured target --------------------------
	const meterIds = []
	for (const { target, prefix } of self.meterTargets()) {
		const human = prefix === 'sel' ? 'Selected track' : prefix === 'master' ? 'Master' : `Track ${target.slice(4)}`
		const nameVar = prefix === 'sel' ? 'sel_track_name' : `${prefix}_name`
		const mk = (suffix, source, label) => {
			const pid = `meter_${prefix}_${suffix}`
			presets[pid] = {
				type: 'simple',
				name: `${human} — ${label}`,
				style: { text: `$(${P}:${nameVar})`, size: '14', color: WHITE, bgcolor: DARK },
				steps: [{ down: [], up: [] }],
				feedbacks: [{ feedbackId: 'meter_bar', options: { target, source } }],
			}
			meterIds.push(pid)
		}
		mk('peak', 'peak', 'Peak meter')
		mk('peakcomp', 'peak_comp', 'Peak + GR meter')
	}
	if (meterIds.length) structure.push({ id: 'meters', name: 'Meters', definitions: meterIds })

	// ---- Status presets (feedback + variable only) -------------------------
	presets['status_connection'] = {
		type: 'simple',
		name: 'Bridge connection status',
		style: { text: `Rea-Sixty\n$(${P}:connection)`, size: '14', color: WHITE, bgcolor: DARK },
		steps: [{ down: [], up: [] }],
		feedbacks: [],
	}
	presets['status_seltrack'] = {
		type: 'simple',
		name: 'Selected track name',
		style: { text: `$(${P}:sel_track_name)`, size: '14', color: WHITE, bgcolor: DARK },
		steps: [{ down: [], up: [] }],
		feedbacks: [],
	}
	presets['status_flip'] = {
		type: 'simple',
		name: 'Flip indicator',
		style: { text: 'Flip', size: '18', color: WHITE, bgcolor: DARK },
		steps: [{ down: [], up: [] }],
		feedbacks: [{ feedbackId: 'flip_active', options: {}, style: { bgcolor: combineRgb(63, 176, 255), color: BLACK } }],
	}
	const statusIds = ['status_connection', 'status_seltrack', 'status_flip']
	for (const layer of [1, 2, 3]) {
		const pid = `status_layer${layer}`
		presets[pid] = {
			type: 'simple',
			name: `Layer ${layer} indicator`,
			style: { text: `Layer ${layer}`, size: '18', color: WHITE, bgcolor: DARK },
			steps: [{ down: [], up: [] }],
			feedbacks: [{ feedbackId: 'layer_active', options: { layer }, style: { bgcolor: combineRgb(224, 160, 32), color: BLACK } }],
		}
		statusIds.push(pid)
	}
	structure.push({ id: 'status', name: 'Status', definitions: statusIds })

	self.setPresetDefinitions(structure, presets)
}
