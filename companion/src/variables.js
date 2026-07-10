// Variable definitions (v2 = object keyed by variableId). The static set is
// always present; the meter set is rebuilt from config (selected / master /
// fixed track numbers) so button text can reference exactly the metered tracks.
export default function (self) {
	const defs = {
		connection: { name: 'Bridge connection (connected/disconnected)' },
		sel_track_name: { name: 'Selected track — name' },
		sel_track_number: { name: 'Selected track — number' },
		active_layer: { name: 'Active binding layer (1-3)' },
		flip: { name: 'Flip mode (on/off)' },
	}

	// Per-metered-target value variables.
	for (const { target, prefix } of self.meterTargets()) {
		const human =
			prefix === 'sel' ? 'Selected track' : prefix === 'master' ? 'Master' : `Track ${target.slice(4)}`
		defs[`${prefix}_peak_db`] = { name: `${human} — peak (dB)` }
		defs[`${prefix}_gr_db`] = { name: `${human} — gain reduction (dB)` }
		defs[`${prefix}_bc_gr_db`] = { name: `${human} — Bus-Comp GR (dB)` }
		if (prefix !== 'sel') defs[`${prefix}_name`] = { name: `${human} — name` }
	}

	self.setVariableDefinitions(defs)

	// Seed sensible defaults so buttons don't show "?" before the first push.
	self.setVariableValues({
		connection: 'disconnected',
		sel_track_name: '',
		sel_track_number: '',
		active_layer: 1,
		flip: 'off',
	})
}
