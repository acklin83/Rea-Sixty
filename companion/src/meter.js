// Meter rendering for the advanced 'meter_bar' feedback.
//
// Companion hands the feedback callback the target buffer dimensions
// (feedback.image = {width,height}). We fill a raw RGBA pixel buffer — no PNG
// encoder, no dependency — and return it as a base64 string. This mirrors the
// Stream Deck meter tile (peak bar green→amber→red, GR bar fills from the top).

// -------------------------------------------------------------- colour ramps
function peakColor(db) {
	if (db >= -6) return [224, 64, 47] // red
	if (db >= -18) return [224, 160, 32] // amber
	return [53, 192, 90] // green
}
function grColor(kind) {
	return kind === 'comp' ? [63, 176, 255] : [240, 160, 48] // blue / orange
}

// Map a source id to the list of bars to draw from a meter entry.
// entry = { peak:[L,R], comp, bc } (comp/bc are dB of reduction, >=0).
function barsForSource(entry, source) {
	const bars = []
	const wantPeak = source === 'peak' || source === 'peak_comp' || source === 'peak_bc'
	const grKind = source === 'comp' || source === 'peak_comp' ? 'comp' : source === 'bc' || source === 'peak_bc' ? 'bc' : null
	if (wantPeak) {
		const db = Math.max(entry.peak[0], entry.peak[1])
		bars.push({
			frac: Math.max(0, Math.min(1, (db + 60) / 60)),
			color: peakColor(db),
			topDown: false,
			label: db <= -150 ? '-inf' : String(Math.round(db)),
		})
	}
	if (grKind) {
		const gr = grKind === 'comp' ? entry.comp : entry.bc
		bars.push({
			frac: Math.max(0, Math.min(1, gr / 20)),
			color: grColor(grKind),
			topDown: true, // reduction → fills from the top down
			label: gr > 0 ? `-${Math.round(gr)}` : '0',
		})
	}
	if (!bars.length) {
		const db = Math.max(entry.peak[0], entry.peak[1])
		bars.push({ frac: Math.max(0, Math.min(1, (db + 60) / 60)), color: peakColor(db), topDown: false, label: String(Math.round(db)) })
	}
	return bars
}

// Draw the bars into an RGBA buffer of W×H. Returns { buffer, text }.
function drawBars(bars, W, H) {
	const buf = Buffer.alloc(W * H * 4)
	const bg = [22, 22, 26]
	const track = [46, 46, 51]
	const put = (x, y, rgb) => {
		if (x < 0 || y < 0 || x >= W || y >= H) return
		const o = (y * W + x) * 4
		buf[o] = rgb[0]
		buf[o + 1] = rgb[1]
		buf[o + 2] = rgb[2]
		buf[o + 3] = 255
	}
	const fillRect = (x0, y0, w, h, rgb) => {
		for (let y = y0; y < y0 + h; y++) for (let x = x0; x < x0 + w; x++) put(x, y, rgb)
	}
	// Background.
	fillRect(0, 0, W, H, bg)

	// Bar geometry: leave a margin, split width across bars.
	const marginY = Math.round(H * 0.12)
	const barTop = marginY
	const barH = H - marginY * 2
	const n = bars.length
	const slotW = Math.floor(W / (n + 1))
	const barW = Math.max(4, Math.round(slotW * 0.55))
	bars.forEach((b, i) => {
		const cx = Math.round(slotW * (i + 1))
		const x0 = cx - Math.round(barW / 2)
		// Track.
		fillRect(x0, barTop, barW, barH, track)
		// Fill.
		const fillH = Math.round(barH * b.frac)
		const y0 = b.topDown ? barTop : barTop + barH - fillH
		fillRect(x0, y0, barW, fillH, b.color)
	})
	return { buffer: buf }
}

// Public: build an advanced-feedback result for a meter entry.
// entry may be undefined (no data yet / not connected).
export function renderMeterBar(entry, source, image) {
	if (!entry) return {} // no override — leaves the button's own style
	const bars = barsForSource(entry, source)
	const text = bars.map((b) => b.label).join('\n')

	// If the control supports a pixel buffer, draw the bar(s); otherwise fall
	// back to a coloured text readout.
	if (image && image.width && image.height) {
		const { buffer } = drawBars(bars, image.width, image.height)
		return {
			imageBuffer: buffer.toString('base64'),
			imageBufferEncoding: { pixelFormat: 'RGBA' },
			text,
			size: 'auto',
			color: 0xffffff,
		}
	}
	const c = bars[0].color
	return { text, color: 0xffffff, bgcolor: (c[0] << 16) | (c[1] << 8) | c[2] }
}

// Exposed for meter_over: the comparable value for a source.
export function meterValueForSource(entry, source) {
	if (!entry) return null
	if (source === 'comp') return entry.comp
	if (source === 'bc') return entry.bc
	return Math.max(entry.peak[0], entry.peak[1]) // peak
}
