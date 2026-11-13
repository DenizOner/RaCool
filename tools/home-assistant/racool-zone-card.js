// Self-contained Home Assistant card; no third-party frontend dependency.
export function validateZones(config) {
    if (!config || !Array.isArray(config.zones) || config.zones.length < 1 || config.zones.length > 3) throw Error('Bir ila üç bölge tanımlayın.');
    const used = new Set();
    return config.zones.map(zone => {
        if (!zone || typeof zone.label !== 'string' || !zone.label.trim() ||
            !Number.isFinite(zone.min) || !Number.isFinite(zone.max) || zone.min >= zone.max) throw Error('Geçersiz bölge sınırları.');
        const step = zone.step ?? 1, gap = zone.min_gap ?? step;
        if (!Number.isFinite(step) || step <= 0 || step > zone.max-zone.min ||
            !Number.isFinite(gap) || gap < step || gap > zone.max-zone.min) throw Error('Geçersiz adım veya sınır açıklığı.');
        for (const entity of [zone.low, zone.high]) {
            if (typeof entity !== 'string' || !/^number\.[a-z0-9_]+$/.test(entity) || used.has(entity)) throw Error('Her sınır ayrı bir number varlığı olmalı.');
            used.add(entity);
        }
        return {...zone};
    });
}

export function readZone(hass, zone) {
    const low = hass?.states?.[zone.low], high = hass?.states?.[zone.high];
    if (!low || !high || ['unknown', 'unavailable', ''].includes(low.state) || ['unknown', 'unavailable', ''].includes(high.state)) return null;
    const values = [Number(low.state), Number(high.state)];
    if (values.some(value => !Number.isFinite(value)) || values[0] < zone.min || values[1] > zone.max || values[0] >= values[1]) return null;
    return values;
}

export function nextBoundary(zone, values, axis, requested) {
    if (!values || ![0, 1].includes(axis) || !Number.isFinite(requested)) throw Error('Geçersiz sınır değişikliği.');
    const step = zone.step ?? 1, gap = zone.min_gap ?? step;
    const minimum = axis === 0 ? zone.min : values[0] + gap;
    const maximum = axis === 0 ? values[1] - gap : zone.max;
    const rounded = zone.min + Math.round((requested-zone.min)/step)*step;
    return Number(Math.min(maximum, Math.max(minimum, rounded)).toFixed(6));
}

class RaCoolZoneCard extends HTMLElement {
    constructor() {
        super();
        this.attachShadow({mode: 'open'});
        this._rows = [];
        this._children = [];
        this._generation = 0;
    }
    setConfig(config) {
        const zones = validateZones(config);
        for (const position of ['before', 'after']) {
            if (config[position] !== undefined && (!Array.isArray(config[position]) || config[position].length > 4)) throw Error('Geçersiz ek kontroller.');
        }
        const generation = ++this._generation;
        this._children = [];
        this._cancelTimers();
        this._config = {title: config.title ?? 'RGB sıcaklık bölgeleri', zones};
        this.shadowRoot.replaceChildren();
        const style = document.createElement('style');
        style.textContent = `
          ha-card { padding: 20px; } h2 { margin: 0 0 18px; font-size: 20px; font-weight: 500; }
          section { margin: 22px 0; } .caption { display: flex; justify-content: space-between; gap: 12px; }
          .value { font-variant-numeric: tabular-nums; white-space: nowrap; }
          .track { position: relative; height: 40px; margin: 6px 12px; }
          .rail, .selected { position: absolute; top: 19px; height: 4px; border-radius: 4px; }
          .rail { left: 0; right: 0; background: var(--divider-color); }
          .selected { background: var(--primary-color); }
          input { position: absolute; inset: 0; width: 100%; height: 40px; margin: 0; background: none; pointer-events: none; appearance: none; }
          input::-webkit-slider-thumb { appearance: none; width: 22px; height: 22px; border-radius: 50%; background: var(--primary-color); border: 2px solid var(--card-background-color); pointer-events: auto; cursor: pointer; }
          input::-moz-range-thumb { width: 18px; height: 18px; border-radius: 50%; background: var(--primary-color); border: 2px solid var(--card-background-color); pointer-events: auto; cursor: pointer; }
          input:focus-visible::-webkit-slider-thumb { outline: 3px solid var(--primary-text-color); outline-offset: 2px; }
          input:disabled { opacity: .45; } input:disabled::-webkit-slider-thumb { cursor: default; }
          .status { display: block; min-height: 18px; font-size: 12px; color: var(--secondary-text-color); }
        `;
        const card = document.createElement('ha-card'), heading = document.createElement('h2');
        heading.textContent = this._config.title;
        card.append(heading);
        const before = document.createElement('div'); card.append(before);
        this._rows = zones.map(zone => {
            const section = document.createElement('section'), caption = document.createElement('div');
            caption.className = 'caption';
            const label = document.createElement('span'), value = document.createElement('span');
            label.textContent = zone.label; value.className = 'value'; caption.append(label, value);
            const track = document.createElement('div'), rail = document.createElement('div'), selected = document.createElement('div');
            track.className = 'track'; rail.className = 'rail'; selected.className = 'selected'; track.append(rail, selected);
            const row = {zone, value, selected, inputs: [], status: document.createElement('span'), pending: null};
            row.status.className = 'status'; row.status.setAttribute('role', 'status');
            for (let axis = 0; axis < 2; ++axis) {
                const input = document.createElement('input');
                input.type = 'range'; input.min = zone.min; input.max = zone.max; input.step = zone.step ?? 1;
                input.setAttribute('aria-label', `${zone.label} ${axis === 0 ? 'alt' : 'üst'} sınır`);
                input.addEventListener('input', () => {
                    const values = readZone(this._hass, zone);
                    if (!values || row.pending) return;
                    const next = nextBoundary(zone, values, axis, Number(input.value));
                    input.value = next;
                    const preview = [...values]; preview[axis] = next; this._paint(row, preview);
                });
                input.addEventListener('change', () => this._commit(row, axis, Number(input.value)));
                track.append(input); row.inputs.push(input);
            }
            section.append(caption, track, row.status); card.append(section);
            return row;
        });
        const after = document.createElement('div'); card.append(after);
        this.shadowRoot.append(style, card); this._refresh();
        if (config.before?.length || config.after?.length) {
            this._loadControls(config, before, after, generation);
        }
    }
    async _loadControls(config, before, after, generation) {
        try {
            if (typeof window.loadCardHelpers !== 'function') throw Error('HA kart yükleyicisi kullanılamıyor.');
            const helpers = await window.loadCardHelpers();
            if (generation !== this._generation) return;
            for (const [position, container] of [['before', before], ['after', after]]) {
                for (const childConfig of config[position] ?? []) {
                    const child = helpers.createCardElement(childConfig);
                    child.style?.setProperty('--ha-card-border-width','0px');
                    child.style?.setProperty('--ha-card-box-shadow','none');
                    child.style?.setProperty('--ha-card-background','transparent');
                    if (this._hass) child.hass = this._hass;
                    this._children.push(child); container.append(child);
                }
            }
        } catch (error) {
            if (generation !== this._generation) return;
            const message = document.createElement('span'); message.setAttribute('role','alert');
            message.textContent = `Ek kontroller yüklenemedi: ${error.message}`; before.append(message);
        }
    }
    set hass(hass) { this._hass = hass; for(const child of this._children) child.hass=hass; this._refresh(); }
    getCardSize() { return 1 + this._rows.length * 2; }
    _paint(row, values) {
        row.value.textContent = `${values[0]}–${values[1]} °C`;
        const percent = value => 100 * (value - row.zone.min) / (row.zone.max - row.zone.min);
        row.selected.style.left = `${percent(values[0])}%`;
        row.selected.style.width = `${percent(values[1]) - percent(values[0])}%`;
    }
    _refresh() {
        for (const row of this._rows) {
            const values = readZone(this._hass, row.zone);
            if (row.pending && values?.[row.pending.axis] === row.pending.value) {
                clearTimeout(row.timer); row.pending = null; row.status.textContent = 'Güncel değer alındı.';
            }
            row.inputs.forEach(input => { input.disabled = !values || !!row.pending; });
            if (!values) { row.value.textContent = 'Kullanılamıyor'; row.status.textContent = 'Geçerli cihaz değerleri bekleniyor.'; continue; }
            if (!row.pending) {
                if (row.status.textContent === 'Geçerli cihaz değerleri bekleniyor.') row.status.textContent = '';
                row.inputs.forEach((input, axis) => { input.value = values[axis]; });
                this._paint(row, values);
            }
        }
    }
    async _commit(row, axis, requested) {
        const values = readZone(this._hass, row.zone);
        if (!values || row.pending) { this._refresh(); return; }
        const next = nextBoundary(row.zone, values, axis, requested);
        if (next === values[axis]) { this._refresh(); return; }
        const pending = {axis, value: next}; row.pending = pending;
        row.status.textContent = 'Cihaz değeri bekleniyor…'; this._refresh();
        row.timer = setTimeout(() => {
            if (row.pending !== pending) return;
            row.pending = null; this._refresh(); row.status.textContent = 'Değer doğrulanamadı. Güncel durumu kontrol edin.';
        }, 10000);
        try {
            await this._hass.callService('number', 'set_value', {entity_id: axis === 0 ? row.zone.low : row.zone.high, value: next});
        } catch (error) {
            if (row.pending !== pending) return;
            clearTimeout(row.timer); row.pending = null; this._refresh(); row.status.textContent = 'Değişiklik gönderilemedi.';
        }
    }
    _cancelTimers() { for (const row of this._rows) clearTimeout(row.timer); }
    disconnectedCallback() { this._cancelTimers(); for (const row of this._rows) row.pending = null; }
    connectedCallback() { this._refresh(); }
}

if (!customElements.get('racool-zone-card')) customElements.define('racool-zone-card', RaCoolZoneCard);
window.customCards ??= [];
if (!window.customCards.some(card => card.type === 'racool-zone-card')) window.customCards.push({type: 'racool-zone-card', name: 'RaCool bölge aralıkları', description: 'Aynı çizgi üzerinde alt ve üst sıcaklık sınırları.'});
