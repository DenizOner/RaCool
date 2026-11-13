// Presentation only: embedded Home Assistant cards retain their own service handling.
export function matchesSearch(title, cards, query) {
    const normalize = value => value.normalize('NFKD').replace(/\p{M}/gu, '').toLocaleLowerCase('tr').replace(/ı/g, 'i');
    const words = normalize(query.trim()).split(/\s+/).filter(Boolean);
    const labels = [];
    const visit = value => {
        if (!value || typeof value !== 'object') return;
        for (const [key, child] of Object.entries(value)) {
            if (['title', 'heading', 'name', 'label', 'content'].includes(key) && typeof child === 'string') labels.push(child);
            else if (child && typeof child === 'object') visit(child);
        }
    };
    visit(cards);
    const text = normalize([title, ...labels].join(' '));
    return words.every(word => text.includes(word));
}

const ElementBase = globalThis.HTMLElement ?? class {};
export class RaCoolSettingsCard extends ElementBase {
    constructor() {
        super();
        this._generation = 0;
        this._children = [];
        this._query = '';
        this.attachShadow?.({mode: 'open'});
    }
    setConfig(config) {
        if (!Array.isArray(config.groups) || !config.groups.length || config.groups.length > 20 ||
            config.groups.some(group => typeof group.title !== 'string' || !group.title.trim() ||
                !Array.isArray(group.cards) || !group.cards.length)) throw Error('Geçersiz ayar grupları.');
        this._config = structuredClone(config);
        this._query = '';
        this._render(++this._generation);
    }
    set hass(value) {
        this._hass = value;
        for (const card of this._children) card.hass = value;
    }
    getCardSize() { return 8; }
    async _render(generation) {
        const root = this.shadowRoot;
        if (!root) return;
        root.replaceChildren();
        this._children = [];
        const style = document.createElement('style');
        style.textContent = ':host{display:block}ha-card{padding:16px}input{box-sizing:border-box;width:100%;padding:12px;border:1px solid var(--divider-color);border-radius:8px;background:var(--card-background-color);color:var(--primary-text-color);font:inherit}details{border-top:1px solid var(--divider-color);margin-top:12px}summary{cursor:pointer;padding:16px 0;font-weight:600}summary:focus-visible{outline:2px solid var(--primary-color)}.cards{display:grid;gap:12px;padding-bottom:12px}.overview{margin-top:16px}.empty{padding:16px;color:var(--secondary-text-color)}';
        const container = document.createElement('ha-card');
        const search = document.createElement('input');
        search.type = 'search'; search.placeholder = 'Ayar ara…'; search.setAttribute('aria-label', 'RaCool ayarlarında ara');
        container.append(search); root.append(style, container);
        const rows = this._config.groups.map(group => {
            const section = document.createElement(group.overview ? 'section' : 'details');
            if (group.overview) section.className = 'overview';
            else section.open = group.expanded === true;
            const heading = document.createElement(group.overview ? 'h3' : 'summary');
            heading.textContent = group.title;
            const body = document.createElement('div'); body.className = 'cards';
            section.append(heading, body); container.append(section);
            return {group, section, body};
        });
        const empty = document.createElement('p'); empty.className = 'empty'; empty.textContent = 'Eşleşen ayar bulunamadı.'; empty.hidden = true; container.append(empty);
        search.addEventListener('input', () => {
            this._query = search.value;
            let count = 0;
            for (const row of rows) {
                const matches = matchesSearch(row.group.title, row.group.cards, this._query);
                row.section.hidden = !row.group.overview && !matches;
                if (matches) count++;
                if (!row.group.overview) {
                    if (this._query && row.savedOpen === undefined) row.savedOpen = row.section.open;
                    if (this._query && matches) row.section.open = true;
                    if (!this._query && row.savedOpen !== undefined) { row.section.open = row.savedOpen; delete row.savedOpen; }
                }
            }
            empty.hidden = count !== 0;
        });
        try {
            if (typeof window.loadCardHelpers !== 'function') throw Error('HA kart yükleyicisi kullanılamıyor.');
            const helpers = await window.loadCardHelpers();
            if (generation !== this._generation) return;
            for (const row of rows) for (const config of row.group.cards) {
                const card = helpers.createCardElement(config);
                if (this._hass) card.hass = this._hass;
                this._children.push(card); row.body.append(card);
            }
        } catch (error) {
            if (generation !== this._generation) return;
            const message = document.createElement('p'); message.setAttribute('role', 'alert');
            message.textContent = `Kartlar yüklenemedi: ${error.message}`; container.append(message);
        }
    }
}
if (globalThis.customElements && !customElements.get('racool-settings-card')) customElements.define('racool-settings-card', RaCoolSettingsCard);
