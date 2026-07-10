// Action definitions. The builtin dropdown is populated LIVE from the Rea-Sixty
// catalogue (the same {"cmd":"list"} -> {"ev":"builtins"} exchange the Stream
// Deck plugin uses), so it always matches the installed extension version.
// updateActions() is re-run when the catalogue arrives, so the dropdown fills in
// as soon as REAPER connects.

// Build grouped, sorted dropdown choices from the catalogue.
// Each choice: { id: builtinName, label: "Category · Display Name" }.
function builtinChoices(self) {
	const items = Array.isArray(self.builtins) ? self.builtins : []
	if (!items.length) {
		// Not connected yet — a placeholder keeps the dropdown valid, and
		// allowCustom lets the user type a builtin name before REAPER is up.
		return [{ id: '', label: '— connect to REAPER to load actions —' }]
	}
	const catOrder = Array.isArray(self.builtinCats) ? self.builtinCats : []
	const rank = new Map(catOrder.map((c, i) => [c, i]))
	const sorted = items.slice().sort((a, b) => {
		const ra = rank.has(a.c) ? rank.get(a.c) : 9999
		const rb = rank.has(b.c) ? rank.get(b.c) : 9999
		if (ra !== rb) return ra - rb
		return String(a.d || a.n).localeCompare(String(b.d || b.n))
	})
	return sorted.map((it) => ({
		id: it.n,
		label: it.c ? `${it.c} · ${it.d || it.n}` : it.d || it.n,
	}))
}

export default function (self) {
	const choices = builtinChoices(self)
	const firstId = choices.length ? choices[0].id : ''

	self.setActionDefinitions({
		builtin: {
			name: 'Rea-Sixty action',
			description: 'Fire a Rea-Sixty built-in (the list matches your installed version).',
			options: [
				{
					type: 'dropdown',
					id: 'builtin',
					label: 'Action',
					default: firstId,
					choices,
					allowCustom: true,
					minChoicesForSearch: 0,
				},
				{
					type: 'number',
					id: 'param',
					label: 'Parameter (0 unless the action needs one, e.g. layer/bank index)',
					default: 0,
					min: 0,
					max: 100000,
				},
			],
			callback: async (action) => {
				const name = String(action.options.builtin || '').trim()
				if (!name) return
				const param = Number(action.options.param) || 0
				if (!self.bridge.send({ cmd: 'action', name, param })) {
					self.log('warn', `not connected — dropped action ${name}`)
				}
			},
		},

		reaper_command_id: {
			name: 'REAPER command (by ID)',
			description: 'Run a REAPER command by its numeric command ID (e.g. 40044 = Play/Stop).',
			options: [
				{
					type: 'number',
					id: 'id',
					label: 'Command ID',
					default: 1007,
					min: 1,
					max: 2000000000,
				},
			],
			callback: async (action) => {
				const id = parseInt(action.options.id, 10)
				if (!id || id <= 0) return
				if (!self.bridge.send({ cmd: 'reaper', id })) {
					self.log('warn', `not connected — dropped reaper id ${id}`)
				}
			},
		},

		reaper_command_action: {
			name: 'REAPER command (by action string)',
			description: 'Run a named REAPER/SWS/custom action, e.g. _SWS_ABOUT or _RS<hash>.',
			options: [
				{
					type: 'textinput',
					id: 'action',
					label: 'Action string',
					default: '',
				},
			],
			callback: async (action) => {
				const str = String(action.options.action || '').trim()
				if (!str) return
				if (!self.bridge.send({ cmd: 'reaper', action: str })) {
					self.log('warn', `not connected — dropped reaper action ${str}`)
				}
			},
		},
	})
}
