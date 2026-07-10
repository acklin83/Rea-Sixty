import { combineRgb } from '@companion-module/base'
import { renderMeterBar, meterValueForSource } from './meter.js'

// Dropdown choices for the metered targets the user configured.
function targetChoices(self) {
	const ts = self.meterTargets()
	if (!ts.length) return [{ id: '', label: '— enable metering in config —' }]
	return ts.map(({ target, prefix }) => ({
		id: target,
		label: prefix === 'sel' ? 'Selected track' : prefix === 'master' ? 'Master' : `Track ${target.slice(4)}`,
	}))
}

const SOURCE_CHOICES = [
	{ id: 'peak', label: 'Peak' },
	{ id: 'comp', label: 'Gain Reduction (channel strip)' },
	{ id: 'bc', label: 'Gain Reduction (Bus Comp)' },
]
const BAR_SOURCE_CHOICES = [
	...SOURCE_CHOICES,
	{ id: 'peak_comp', label: 'Peak + GR (channel strip)' },
	{ id: 'peak_bc', label: 'Peak + GR (Bus Comp)' },
]

export default function (self) {
	const targets = targetChoices(self)
	const firstTarget = targets[0].id

	self.setFeedbackDefinitions({
		layer_active: {
			type: 'boolean',
			name: 'Binding layer is active',
			defaultStyle: { bgcolor: combineRgb(224, 160, 32), color: combineRgb(0, 0, 0) },
			options: [
				{
					type: 'dropdown',
					id: 'layer',
					label: 'Layer',
					default: 1,
					choices: [
						{ id: 1, label: 'Layer 1' },
						{ id: 2, label: 'Layer 2' },
						{ id: 3, label: 'Layer 3' },
					],
				},
			],
			callback: (fb) => Number(self.state.layer) === Number(fb.options.layer),
		},

		flip_active: {
			type: 'boolean',
			name: 'Flip mode is on',
			defaultStyle: { bgcolor: combineRgb(63, 176, 255), color: combineRgb(0, 0, 0) },
			options: [],
			callback: () => self.state.flip === true,
		},

		meter_over: {
			type: 'boolean',
			name: 'Meter over threshold',
			description: 'True when a metered value crosses a threshold. Peak uses dBFS (e.g. -6). GR uses dB of reduction (e.g. 3).',
			defaultStyle: { bgcolor: combineRgb(224, 64, 47), color: combineRgb(255, 255, 255) },
			options: [
				{ type: 'dropdown', id: 'target', label: 'Track', default: firstTarget, choices: targets },
				{ type: 'dropdown', id: 'source', label: 'Source', default: 'peak', choices: SOURCE_CHOICES },
				{ type: 'number', id: 'threshold', label: 'Threshold (dB)', default: -6, min: -60, max: 24, step: 1 },
			],
			callback: (fb) => {
				const v = meterValueForSource(self.meters[fb.options.target], fb.options.source)
				if (v === null) return false
				return v >= Number(fb.options.threshold)
			},
		},

		meter_bar: {
			type: 'advanced',
			name: 'Meter bar (graphical)',
			description: 'Draws a live Peak / Gain-Reduction bar on the button.',
			options: [
				{ type: 'dropdown', id: 'target', label: 'Track', default: firstTarget, choices: targets },
				{ type: 'dropdown', id: 'source', label: 'Source', default: 'peak', choices: BAR_SOURCE_CHOICES },
			],
			callback: (fb) => renderMeterBar(self.meters[fb.options.target], fb.options.source, fb.image),
		},
	})
}
