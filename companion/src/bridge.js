// BridgeClient — TCP client to the Rea-Sixty bridge (StreamDeckBridge.cpp).
//
// The REAPER extension runs a newline-delimited-JSON TCP server (default
// 127.0.0.1:49900). This is exactly the same wire protocol the Stream Deck
// plugin speaks — the bridge does not care whether the client is a Stream Deck
// plugin or a Companion module.
//
//   module -> REAPER
//     {"cmd":"subscribe"}                          start receiving state pushes
//     {"cmd":"list"}                               request the builtin catalogue
//     {"cmd":"action","name":"flip","param":0}     fire a Rea-Sixty builtin
//     {"cmd":"reaper","id":40044}                  fire a REAPER command id
//     {"cmd":"reaper","action":"_SWS_ABOUT"}       fire a named REAPER command
//     {"cmd":"meters","targets":["sel","num:0"]}   set the metered target set
//     {"cmd":"ping"}
//
//   REAPER -> module
//     {"ev":"hello","app":"Rea-Sixty","proto":1}
//     {"ev":"builtins","cats":[...],"items":[{n,d,c,p}]}
//     {"ev":"state","sel":{num,name,rgb},"layer":L,"flip":bool}
//     {"ev":"meter","t":[{id,peak:[L,R],comp,bc,nm,rgb}]}
//     {"ev":"pong"}
//
// Emits: 'connect', 'close', 'error'(msg), and one event per bridge message
// keyed by its ev field ('hello','builtins','state','meter','pong').

import net from 'net'
import { EventEmitter } from 'events'

const RECONNECT_MS = 1500

export class BridgeClient extends EventEmitter {
	constructor(log) {
		super()
		this.log = log || (() => {})
		this.host = '127.0.0.1'
		this.port = 49900
		this.sock = null
		this.buf = ''
		this.connected = false
		this.reconnectTimer = null
		this.wantOpen = false
	}

	// (Re)configure and (re)connect. Safe to call on config change.
	open(host, port) {
		this.host = host || '127.0.0.1'
		this.port = parseInt(port, 10) || 49900
		this.wantOpen = true
		this._teardown()
		this._connect()
	}

	// Permanent close (module destroy / config change before reopen).
	close() {
		this.wantOpen = false
		if (this.reconnectTimer) {
			clearTimeout(this.reconnectTimer)
			this.reconnectTimer = null
		}
		this._teardown()
	}

	_teardown() {
		if (this.sock) {
			this.sock.removeAllListeners()
			this.sock.destroy()
			this.sock = null
		}
		this.buf = ''
		this.connected = false
	}

	_scheduleReconnect() {
		if (!this.wantOpen || this.reconnectTimer) return
		this.reconnectTimer = setTimeout(() => {
			this.reconnectTimer = null
			if (this.wantOpen) this._connect()
		}, RECONNECT_MS)
	}

	_connect() {
		const sock = net.createConnection({ host: this.host, port: this.port }, () => {
			this.connected = true
			this.log('debug', `bridge connected ${this.host}:${this.port}`)
			this.emit('connect')
		})
		sock.setEncoding('utf8')
		sock.on('data', (d) => this._onData(d))
		sock.on('error', (e) => {
			this.emit('error', e && e.code ? e.code : String(e))
		})
		sock.on('close', () => {
			const was = this.connected
			this._teardown()
			if (was) this.emit('close')
			this._scheduleReconnect()
		})
		this.sock = sock
	}

	_onData(d) {
		this.buf += d
		let i
		while ((i = this.buf.indexOf('\n')) >= 0) {
			const line = this.buf.slice(0, i)
			this.buf = this.buf.slice(i + 1)
			if (!line.trim().length) continue
			let m
			try {
				m = JSON.parse(line)
			} catch {
				continue
			}
			if (m && m.ev) this.emit(m.ev, m)
		}
		// Guard against a runaway peer growing the buffer without a newline.
		if (this.buf.length > 256 * 1024) this.buf = ''
	}

	// Send one command object. Returns false if not currently connected.
	send(obj) {
		if (this.sock && this.connected && !this.sock.destroyed && this.sock.writable) {
			this.sock.write(JSON.stringify(obj) + '\n')
			return true
		}
		return false
	}
}
