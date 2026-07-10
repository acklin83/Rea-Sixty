import { InstanceBase, Regex, InstanceStatus } from '@companion-module/base'
import { BridgeClient } from './bridge.js'
import UpdateActions from './actions.js'
import UpdateFeedbacks from './feedbacks.js'
import UpdateVariableDefinitions from './variables.js'
import UpdatePresets from './presets.js'
import UpgradeScripts from './upgrades.js'

export default class ReaSixtyInstance extends InstanceBase {
	constructor(internal) {
		super(internal)
	}

	async init(config, _isFirstInit, _secrets) {
		this.config = config

		// Live catalogue of Rea-Sixty builtins, fetched from the bridge.
		this.builtins = [] // [{ n, d, c, p }]
		this.builtinCats = [] // ordered category names
		// Latest surface state and per-target meter entries.
		this.state = { sel: { num: -1, name: '', rgb: [0, 0, 0] }, layer: 1, flip: false }
		this.meters = {} // id -> { peak:[L,R], comp, bc, nm, rgb }
		this.meterBallistics = {} // id -> { peak:{sm,q}, comp:{...}, bc:{...} }

		this.bridge = new BridgeClient((level, msg) => this.log(level, msg))
		this.bridge.on('connect', () => this._onBridgeConnect())
		this.bridge.on('close', () => this._onBridgeClose('REAPER closed the connection'))
		this.bridge.on('error', (code) => this._onBridgeError(code))
		this.bridge.on('hello', (m) => this.log('debug', `bridge hello proto ${m.proto}`))
		this.bridge.on('builtins', (m) => this._onBuiltins(m))
		this.bridge.on('state', (m) => this._onState(m))
		this.bridge.on('meter', (m) => this._onMeter(m))

		this.updateActions()
		this.updateFeedbacks()
		this.updateVariableDefinitions()
		this.updatePresets()

		this._openBridge()
	}

	async destroy() {
		if (this.bridge) this.bridge.close()
		this.log('debug', 'destroy')
	}

	async configUpdated(config, _secrets) {
		this.config = config
		// The metered-target set may have changed → rebuild dependent defs.
		this.updateVariableDefinitions()
		this.updateFeedbacks()
		this.updatePresets()
		this._openBridge()
	}

	getConfigFields() {
		return [
			{
				type: 'static-text',
				id: 'info',
				width: 12,
				label: 'Rea-Sixty bridge',
				value:
					'Connects to the Rea-Sixty bridge inside REAPER (default 127.0.0.1:49900). ' +
					'If Companion runs on a DIFFERENT machine than REAPER, open the bridge to the LAN by ' +
					'running this once in REAPER (ReaScript console) and restarting REAPER:<br>' +
					'<code>reaper.SetExtState("rea_sixty","sd_bridge_bind","lan",true)</code>',
			},
			{
				type: 'textinput',
				id: 'host',
				label: 'REAPER host',
				width: 8,
				default: '127.0.0.1',
				regex: Regex.HOSTNAME,
			},
			{
				type: 'textinput',
				id: 'port',
				label: 'Bridge port',
				width: 4,
				default: '49900',
				regex: Regex.PORT,
			},
			{
				type: 'static-text',
				id: 'meterInfo',
				width: 12,
				label: 'Metering',
				value:
					'Choose which tracks report live Peak / Gain-Reduction as variables and feedbacks. ' +
					'Keep this tight — every metered track is polled ~15×/s.',
			},
			{
				type: 'checkbox',
				id: 'meterSelected',
				label: 'Meter the selected track',
				width: 6,
				default: true,
			},
			{
				type: 'checkbox',
				id: 'meterMaster',
				label: 'Meter the master',
				width: 6,
				default: false,
			},
			{
				type: 'textinput',
				id: 'meterTracks',
				label: 'Also meter these track numbers (comma-separated, e.g. 1,2,5)',
				width: 12,
				default: '',
			},
		]
	}

	updateActions() {
		UpdateActions(this)
	}
	updateFeedbacks() {
		UpdateFeedbacks(this)
	}
	updateVariableDefinitions() {
		UpdateVariableDefinitions(this)
	}
	updatePresets() {
		UpdatePresets(this)
	}

	// --------------------------------------------------------------- bridge

	_openBridge() {
		this.updateStatus(InstanceStatus.Connecting)
		this.builtins = []
		this.builtinCats = []
		this.bridge.open(this.config.host, this.config.port)
	}

	_onBridgeConnect() {
		this.updateStatus(InstanceStatus.Ok)
		this.bridge.send({ cmd: 'subscribe' })
		this.bridge.send({ cmd: 'list' })
		this.recomputeMeters()
		this.setVariableValues({ connection: 'connected' })
	}

	_onBridgeClose(reason) {
		this.updateStatus(InstanceStatus.Disconnected, reason)
		this.meters = {}
		this.meterBallistics = {}
		this.setVariableValues({ connection: 'disconnected' })
		this.checkAllFeedbacks() // clear meter / state feedbacks
	}

	_onBridgeError(code) {
		// ECONNREFUSED etc. are normal while REAPER is down — keep it quiet but
		// reflect it in status.
		this.updateStatus(InstanceStatus.Disconnected, code)
	}

	_onBuiltins(m) {
		this.builtins = Array.isArray(m.items) ? m.items : []
		this.builtinCats = Array.isArray(m.cats) ? m.cats : []
		this.log('info', `bridge builtins ${this.builtins.length} cats ${this.builtinCats.length}`)
		// Rebuild the action dropdowns now that we have the real catalogue.
		this.updateActions()
		this.updatePresets()
	}

	_onState(m) {
		if (m.sel) this.state.sel = m.sel
		if (typeof m.layer === 'number') this.state.layer = m.layer
		if (typeof m.flip === 'boolean') this.state.flip = m.flip
		const sel = this.state.sel
		this.setVariableValues({
			sel_track_name: sel.name || '',
			sel_track_number: sel.num > 0 ? sel.num : '',
			active_layer: this.state.layer,
			flip: this.state.flip ? 'on' : 'off',
		})
		this.checkFeedbacks('layer_active', 'flip_active')
	}

	_onMeter(m) {
		const entries = Array.isArray(m.t) ? m.t : []
		const vals = {}
		for (const e of entries) {
			this.meters[e.id] = e
			const prefix = this._varPrefixForTarget(e.id)
			if (!prefix) continue
			const peak = Math.max(e.peak[0], e.peak[1])
			vals[`${prefix}_peak_db`] = this._fmtDb(peak)
			vals[`${prefix}_gr_db`] = this._fmtGr(e.comp)
			vals[`${prefix}_bc_gr_db`] = this._fmtGr(e.bc)
			if (prefix !== 'sel') vals[`${prefix}_name`] = e.nm || ''
		}
		this.setVariableValues(vals)
		this.checkFeedbacks('meter_over', 'meter_bar')
	}

	// ---------------------------------------------------------- metering set

	// Parse the config into a de-duplicated set of bridge target strings and a
	// parallel list of { target, prefix } for variable naming.
	meterTargets() {
		const out = []
		const seen = new Set()
		const add = (target, prefix) => {
			if (seen.has(target)) return
			seen.add(target)
			out.push({ target, prefix })
		}
		if (this.config.meterSelected) add('sel', 'sel')
		if (this.config.meterMaster) add('num:0', 'master')
		const raw = String(this.config.meterTracks || '')
		for (const tok of raw.split(',')) {
			const n = parseInt(tok.trim(), 10)
			if (!isNaN(n) && n > 0) add(`num:${n}`, `trk_${n}`)
		}
		return out
	}

	_varPrefixForTarget(id) {
		if (id === 'sel') return 'sel'
		if (id === 'num:0') return 'master'
		const mm = /^num:(\d+)$/.exec(id)
		if (mm) return `trk_${mm[1]}`
		return null
	}

	// Tell the bridge which targets to meter. Called on connect + config change.
	recomputeMeters() {
		const targets = this.meterTargets().map((t) => t.target)
		this.bridge.send({ cmd: 'meters', targets })
	}

	// --------------------------------------------------------------- helpers

	_fmtDb(db) {
		if (db <= -150) return '-inf'
		return db.toFixed(1)
	}
	_fmtGr(gr) {
		// GR arrives as a positive number of dB of reduction; 0 = none.
		return gr > 0 ? `-${gr.toFixed(1)}` : '0.0'
	}
}

// v2 entry: Companion imports the default export (the instance class) and the
// named `UpgradeScripts` export from this file.
export { UpgradeScripts }
