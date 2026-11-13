// Uses Home Assistant's own HS picker. No virtual lamps or fabricated power state.
const SCALE = 65536;
const LIMIT = 65279;
const clamp = (value, low, high) => Math.max(low, Math.min(high, value));

export function validXY(value) {
    return !!value && typeof value === 'object' && !Array.isArray(value) && Object.keys(value).length === 2 &&
        Number.isInteger(value.x) && value.x >= 0 && value.x <= LIMIT &&
        Number.isInteger(value.y) && value.y >= 1 && value.y <= LIMIT;
}

export function parseXYState(value) {
    if (typeof value !== 'string' || value.length > 120) return null;
    let parsed;
    try { parsed = JSON.parse(value); } catch {
        // HA/Jinja can render a dictionary with single quotes; never eval it.
        const match = value.match(/^\{\s*['"]([xy])['"]\s*:\s*(\d+(?:\.\d+)?(?:[eE][+-]?\d+)?)\s*,\s*['"]([xy])['"]\s*:\s*(\d+(?:\.\d+)?(?:[eE][+-]?\d+)?)\s*\}$/);
        if (!match || match[1] === match[3]) return null;
        parsed = {[match[1]]: Number(match[2]), [match[3]]: Number(match[4])};
    }
    if (parsed && typeof parsed === 'object' && !Array.isArray(parsed) && Object.keys(parsed).length === 2 &&
        Number.isFinite(parsed.x) && Number.isFinite(parsed.y) && parsed.x >= 0 && parsed.x <= LIMIT / SCALE &&
        parsed.y > 0 && parsed.y <= LIMIT / SCALE) {
        parsed = {x: Math.round(parsed.x * SCALE), y: Math.round(parsed.y * SCALE)};
    }
    return validXY(parsed) ? parsed : null;
}

export function hsToXY(value) {
    if (!Array.isArray(value) || value.length !== 2 || !value.every(Number.isFinite) ||
        value[0] < 0 || value[0] > 360 || value[1] < 0 || value[1] > 1) throw Error('Geçersiz renk.');
    const hue = value[0] / 60, saturation = value[1];
    const channel = offset => 1 - saturation * clamp(Math.min((offset + hue) % 6, 4 - (offset + hue) % 6), 0, 1);
    const linear = channel => channel <= 0.04045 ? channel / 12.92 : ((channel + 0.055) / 1.055) ** 2.4;
    const [r, g, b] = [channel(5), channel(3), channel(1)].map(linear);
    const X = 0.4124 * r + 0.3576 * g + 0.1805 * b;
    const Y = 0.2126 * r + 0.7152 * g + 0.0722 * b;
    const Z = 0.0193 * r + 0.1192 * g + 0.9505 * b;
    return {x: clamp(Math.round(X / (X + Y + Z) * SCALE), 0, LIMIT),
        y: clamp(Math.round(Y / (X + Y + Z) * SCALE), 1, LIMIT)};
}

export function xyToHS(value) {
    if (!validXY(value)) throw Error('Geçersiz XY.');
    const x = value.x / SCALE, y = value.y / SCALE;
    const X = x / y, Z = (1 - x - y) / y;
    let rgb = [3.2406 * X - 1.5372 - 0.4986 * Z,
        -0.9689 * X + 1.8758 + 0.0415 * Z, 0.0557 * X - 0.2040 + 1.0570 * Z].map(v => Math.max(0, v));
    const maximum = Math.max(...rgb);
    if (!(maximum > 0)) throw Error('Görüntülenemeyen renk.');
    rgb = rgb.map(v => v / maximum).map(v => v <= 0.0031308 ? 12.92 * v : 1.055 * v ** (1 / 2.4) - 0.055);
    const [r, g, b] = rgb, high = Math.max(...rgb), low = Math.min(...rgb), delta = high - low;
    let hue = 0;
    if (delta > 0.0001) hue = 60 * (high === r ? ((g - b) / delta) % 6 : high === g ? (b - r) / delta + 2 : (r - g) / delta + 4);
    return [(hue + 360) % 360, delta / high];
}

export function validateColorConfig(config) {
    if (!config || typeof config.command_topic !== 'string' || config.command_topic.length > 512 ||
        !/^[^#+\x00-\x1f]+\/set$/.test(config.command_topic) || !Array.isArray(config.zones) ||
        config.zones.length < 1 || config.zones.length > 5) throw Error('Geçersiz renk kartı yapılandırması.');
    const entities = new Set(), properties = new Set();
    const zones = config.zones.map(zone => {
        if (!zone || typeof zone.label !== 'string' || !zone.label.trim() || zone.label.length > 64 ||
            !/^sensor\.[a-z0-9_]+$/.test(zone.entity) || !/^[a-z][a-z0-9_]*_xy(?:_[a-z][a-z0-9_]*)?$/.test(zone.property) ||
            entities.has(zone.entity) || properties.has(zone.property)) throw Error('Geçersiz bölge renk kaynağı.');
        entities.add(zone.entity); properties.add(zone.property);
        return {...zone};
    });
    return {command_topic: config.command_topic, zones};
}

if (typeof HTMLElement !== 'undefined' && typeof customElements !== 'undefined') {
    class RaCoolColorCard extends HTMLElement {
        constructor() {
            super(); this.attachShadow({mode: 'open'}); this._generation = 0; this._rows = [];
        }
        setConfig(config) {
            this.disconnectedCallback(); this._config = validateColorConfig(config); this._active = 0;
            this.shadowRoot.replaceChildren();
            const style = document.createElement('style');
            style.textContent = ':host{display:block}ha-card{padding:16px}h2{font-size:18px;margin:0 0 12px}.zones{display:flex;flex-wrap:wrap;gap:8px}button{font:inherit;color:var(--primary-text-color);background:var(--card-background-color);border:1px solid var(--divider-color);padding:8px 12px;border-radius:12px;cursor:pointer}button[aria-pressed="true"]{border:2px solid var(--primary-color)}.picker{display:flex;justify-content:center;margin:16px 0}.status{font-size:13px;color:var(--secondary-text-color);min-height:20px}';
            const card = document.createElement('ha-card'), title = document.createElement('h2');
            title.textContent = 'Bölge renkleri';
            const tabs = document.createElement('div'); tabs.className = 'zones';
            this._rows = this._config.zones.map((zone, index) => {
                const button = document.createElement('button'); button.textContent = zone.label;
                button.addEventListener('click', () => {this._active = index; this._refresh();});
                tabs.append(button); return {zone, button, pending: null, timer: null};
            });
            const area = document.createElement('div'); area.className = 'picker';
            this._picker = document.createElement('ha-hs-color-picker'); this._picker.renderSize = 240;
            this._picker.addEventListener('value-changed', event => this._send(event.detail?.value));
            area.append(this._picker);
            this._status = document.createElement('div'); this._status.className = 'status';
            this._status.setAttribute('role', 'status');
            card.append(title, tabs, area, this._status); this.shadowRoot.append(style, card);
            this._refresh(); this._loadPicker();
        }
        set hass(value) {this._hass = value; this._refresh();}
        getCardSize() {return 5;}
        disconnectedCallback() {
            ++this._generation;
            for (const row of this._rows) {clearTimeout(row.timer); row.timer = null; row.pending = null;}
        }
        async _loadPicker() {
            const generation = this._generation;
            try {
                if (!customElements.get('ha-hs-color-picker')) {
                    const helpers = await window.loadCardHelpers();
                    if (typeof helpers.importMoreInfoControl !== 'function') throw Error('Picker loader unavailable');
                    helpers.importMoreInfoControl('light');
                    let timer;
                    try {await Promise.race([customElements.whenDefined('ha-hs-color-picker'),
                        new Promise((resolve, reject) => {timer = setTimeout(() => reject(Error('Picker load timeout')), 8000);})]);}
                    finally {clearTimeout(timer);}
                }
                if (generation === this._generation) this._refresh();
            } catch {
                if (generation === this._generation) this._status.textContent = 'Home Assistant renk seçicisi yüklenemedi.';
            }
        }
        _refresh() {
            if (!this._config || !this._picker) return;
            for (const [index, row] of this._rows.entries()) {
                row.button.setAttribute('aria-pressed', String(index === this._active));
                const state = this._hass?.states?.[row.zone.entity];
                row.actual = parseXYState(state?.state);
                if (row.pending && row.actual && row.actual.x === row.pending.x && row.actual.y === row.pending.y &&
                    Date.parse(state.last_updated) >= row.sentAt) {
                    clearTimeout(row.timer); row.timer = null; row.pending = null;
                    row.message = 'Cihazın renk değeri güncellendi.';
                }
            }
            const row = this._rows[this._active]; this._picker.disabled = !row.actual;
            if (row.actual && !row.pending) this._picker.value = xyToHS(row.actual);
            this._status.textContent = row.pending ? 'Ayar gönderildi; cihaz yanıtı bekleniyor.' :
                row.actual ? row.message || 'Renk çemberinden seçim yapın. Parlaklık ayrı ayardır.' : 'Cihazın renk değeri alınamadı.';
        }
        async _send(value) {
            const row = this._rows[this._active];
            if (!row?.actual || !this._hass) return;
            let desired;
            try {desired = hsToXY(value);} catch {return;}
            if (desired.x === row.actual.x && desired.y === row.actual.y) return;
            const generation = this._generation; clearTimeout(row.timer);
            row.pending = desired; row.sentAt = Date.now(); this._refresh();
            try {
                await this._hass.callService('mqtt', 'publish', {topic: this._config.command_topic,
                    payload: JSON.stringify({[row.zone.property]: {x:desired.x / SCALE,y:desired.y / SCALE}}), qos: 0, retain: false});
                if (generation !== this._generation || row.pending !== desired) return;
                row.timer = setTimeout(() => {
                    row.pending = null; row.message = 'Yanıt alınamadı; cihaz ayarını kontrol edin.'; this._refresh();
                }, 10000);
            } catch {
                if (generation !== this._generation || row.pending !== desired) return;
                row.pending = null; row.message = 'Değişiklik gönderilemedi.'; this._refresh();
            }
        }
    }
    if (!customElements.get('racool-color-card')) customElements.define('racool-color-card', RaCoolColorCard);
    window.customCards = window.customCards || [];
    window.customCards.push({type: 'racool-color-card', name: 'RaCool bölge renkleri', description: 'Home Assistant renk seçicisiyle kalıcı bölge renklerini düzenler.'});
}
