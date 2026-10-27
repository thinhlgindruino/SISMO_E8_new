/*
 * e8_control.js - Widget ThingsBoard "E8+ Control": bảng điều khiển 1 máy E8+ (giống mẫu E8 cũ)
 *
 * Datasource: 1 thiết bị E8+ (alias theo trạng thái dashboard). Khóa dữ liệu cần có trong datasource:
 *   attribute  firmware, access_mode, motor_mode, card_timer, language, motor_ratio, remote_lock, sessions_total,
 *              mac, ip, ssid, active, lastConnectTime, lastDisconnectTime, customer_name, customer_info, customer_notes
 *   timeseries state, kind, remaining_s, planned_s, program, fault, rssi, fw_state, fw_progress, fw_error
 * Online = active và lần ngắt kết nối cuối cũ hơn lần kết nối cuối.
 * Nút: Play / Pause / Stop (sáng = trạng thái hiện tại), khóa / mở điều khiển từ xa (sáng = trạng thái hiện tại),
 * nút người -> bảng thông tin (sửa tên / thông tin / ghi chú khách = server attributes, chọn phiên bản firmware =
 * gán gói OTA cho thiết bị). 5 dòng cài đặt: bấm để đổi giá trị chờ (chấm cam), OK -> RPC setSettings
 * (chấm xanh khi máy đã nhận). Mọi lệnh là RPC two-way, máy trả lời {"ok", "error"}.
 */
var LANG_NAMES = ['Français', 'English', 'Español', 'Italiano', 'Deutsch', 'Português'];
var ERRORS = {
    locked: 'Commande à distance verrouillée',
    not_ready: 'La machine n\'est pas sur le menu ou une séance',
    not_running: 'Aucune séance en cours',
    motor_running: 'Impossible pendant que le moteur tourne',
    bad_mode_ratio: 'Combinaison Mode / Ratio non autorisée',
    out_of_range: 'Valeur hors limites',
    busy: 'Machine occupée (séance en cours)',
    save_failed: 'Erreur d\'enregistrement sur la machine',
    offline: 'Machine hors ligne'
};
var FW_STATES = {
    QUEUED: 'En attente', INITIATED: 'Demandée', DOWNLOADING: 'Téléchargement', DOWNLOADED: 'Téléchargé',
    VERIFIED: 'Vérifié', UPDATING: 'Installation', UPDATED: 'À jour', FAILED: 'Échec'
};

function comboOk(mode, ratio) {
    var step = mode <= 3 ? 12 : (mode === 4 ? 15 : 20);
    var shift = step * (mode - 3) + 3 * (ratio - 9);
    return 42 + shift >= 0 && 79 + shift <= 180;
}

function two(n) { return (n < 10 ? '0' : '') + n; }
function hms(s) {
    s = Math.max(0, Math.round(s));
    return two(Math.floor(s / 3600)) + ':' + two(Math.floor(s / 60) % 60) + ':' + two(s % 60);
}

self.onInit = function () {
    var ctx = self.ctx;
    var $ = ctx.$container;
    var st = { v: {}, ts: {}, pending: {}, sending: false, fwArmed: false, fwList: [] };
    self.st = st;
    var el = {
        root: $.find('.e8w')[0], name: $.find('.e8w-name')[0], time: $.find('.e8w-time')[0], sub: $.find('.e8w-sub')[0],
        online: $.find('.e8w-online')[0], ok: $.find('.e8w-ok')[0], msg: $.find('.e8w-main .e8w-msg')[0],
        pmsg: $.find('.e8w-panel-msg')[0], info: $.find('.e8w-info')[0], fwSel: $.find('.e8w-fw-select')[0],
        fwGo: $.find('.e8w-fw-go')[0], fwState: $.find('.e8w-fw-state')[0], save: $.find('.e8w-save')[0]
    };
    self.el = el;

    function device() {
        var ds = ctx.datasources && ctx.datasources[0];
        return ds && ds.entityId ? { id: ds.entityId, name: ds.entityName || ds.name } : null;
    }
    function num(k, d) { var x = parseFloat(st.v[k]); return isNaN(x) ? d : x; }
    function bool(k) { return st.v[k] === true || st.v[k] === 'true'; }
    function online() {
        if (!bool('active')) return false;
        return !(num('lastDisconnectTime', 0) > num('lastConnectTime', 0));
    }
    function say(text, panel) {
        var e = panel ? el.pmsg : el.msg;
        e.textContent = text || '';
        clearTimeout(e._t);
        if (text) e._t = setTimeout(function () { e.textContent = ''; }, 6000);
    }
    function errText(e) {
        if (e && e.error && e.error.message) return e.error.message;
        if (e && e.status === 504) return ERRORS.offline;
        if (e && e.status === 409) return ERRORS.offline;
        return 'Erreur ' + (e && e.status ? e.status : '');
    }
    function rpc(method, params, done) {
        var d = device();
        if (!d) return;
        if (!online()) { say(ERRORS.offline); return; }
        st.sending = true;
        render();
        ctx.http.post('/api/rpc/twoway/' + d.id, { method: method, params: params || {}, timeout: 10000 }).subscribe(
            function (r) {
                st.sending = false;
                if (r && r.ok === false) say(ERRORS[r.error] || r.error);
                else say('');
                if (r) ['access_mode', 'motor_mode', 'card_timer', 'language', 'motor_ratio', 'remote_lock'].forEach(function (k) {
                    if (r[k] !== undefined) st.v[k] = r[k];
                });
                if (r && r.state) st.v.state = r.state;
                if (done) done(r);
                render();
            },
            function (e) { st.sending = false; say(errText(e)); render(); });
    }
    self.rpc = rpc;

    function value(k) { return st.pending[k] !== undefined ? st.pending[k] : num(k, undefined); }
    function label(k) {
        var x = value(k);
        if (x === undefined) return '-';
        switch (k) {
        case 'access_mode': return x === 1 ? 'Accès Badgeuse' : 'Accès Libre-service';
        case 'motor_mode': return 'Mode: ' + x;
        case 'card_timer': return x === 1 ? 'Timer Sismo' : 'Timer Badgeuse';
        case 'language': return LANG_NAMES[x] || '?';
        case 'motor_ratio': return 'Ratio: ' + x;
        }
        return String(x);
    }
    function cycle(k) {
        var x = value(k);
        if (x === undefined) return;
        var mode = value('motor_mode'), ratio = value('motor_ratio');
        if (k === 'access_mode' || k === 'card_timer') x = x ? 0 : 1;
        else if (k === 'language') x = (x + 1) % LANG_NAMES.length;
        else if (k === 'motor_mode') { do { x = x >= 5 ? 1 : x + 1; } while (!comboOk(x, ratio)); }
        else if (k === 'motor_ratio') { do { x = x >= 17 ? 2 : x + 1; } while (!comboOk(mode, x)); }
        if (x === num(k, undefined)) delete st.pending[k]; else st.pending[k] = x;
        render();
    }

    function remaining() {
        var r = num('remaining_s', 0);
        if (st.v.state === 'RUN' && st.ts.remaining_s) r -= (Date.now() - st.ts.remaining_s) / 1000;
        return Math.max(0, r);
    }

    function render() {
        var d = device();
        var on = online();
        var locked = bool('remote_lock');
        var state = st.v.state || 'IDLE';
        el.name.textContent = d ? d.name : '-';
        var running = state !== 'IDLE';
        el.time.textContent = running || st.v.kind ? hms(running ? remaining() : num('planned_s', 0)) : '--:--:--';
        var sub = [];
        if (!on) sub.push('Hors ligne');
        else if (running) sub.push((state === 'RUN' ? 'En séance' : state === 'PAUSE' ? 'En pause' : 'Démarrage') +
                                   (st.v.program ? ' · ' + st.v.program : ''));
        var fault = num('fault', 0);
        if (on && fault) sub.push('ERR.0' + fault);
        el.sub.textContent = sub.join(' · ');
        el.online.className = 'e8w-dot e8w-online' + (on ? (fault ? ' bad' : ' on') : '');
        var blocked = !on || locked || st.sending;
        $.find('.e8w-btn').each(function () {
            var c = this.getAttribute('data-cmd'), active = false, dis = false;
            if (c === 'play')  { active = state === 'RUN' || state === 'COUNTDOWN'; dis = blocked; }
            if (c === 'pause') { active = state === 'PAUSE'; dis = blocked; }
            if (c === 'stop')  { active = state === 'IDLE'; dis = blocked; }
            if (c === 'lock')  { active = locked; dis = !on || st.sending; }
            if (c === 'unlock'){ active = !locked; dis = !on || st.sending; }
            this.classList.toggle('active', active);
            this.disabled = dis;
        });
        $.find('.e8w-row').each(function () {
            var k = this.getAttribute('data-key');
            this.querySelector('.e8w-pill').textContent = label(k);
            var dot = this.querySelector('.e8w-dot');
            dot.className = 'e8w-dot' + (!on || st.v[k] === undefined ? '' : st.pending[k] !== undefined ? ' wait' : ' on');
            this.classList.toggle('disabled', blocked);
        });
        el.ok.disabled = blocked || !Object.keys(st.pending).length;
        renderPanel();
    }
    self.render = render;

    function renderPanel() {
        if (!el.root.classList.contains('show-panel')) return;
        var rows = [
            ['Firmware', st.v.firmware || '-'],
            ['Séances', st.v.sessions_total !== undefined ? st.v.sessions_total : '-'],
            ['État', (st.v.state || '-') + (st.v.kind ? ' (' + st.v.kind + ')' : '')],
            ['Défaut', num('fault', 0) ? 'ERR.0' + num('fault', 0) : 'Aucun'],
            ['WiFi', (st.v.ssid || '-') + (st.v.rssi ? ' (' + st.v.rssi + ' dBm)' : '')],
            ['IP', st.v.ip || '-'],
            ['MAC', st.v.mac || '-'],
            ['Dernière connexion', st.v.lastConnectTime ? new Date(num('lastConnectTime', 0)).toLocaleString() : '-']
        ];
        el.info.innerHTML = '';
        rows.forEach(function (r) {
            var tr = document.createElement('tr');
            var a = document.createElement('td'), b = document.createElement('td');
            a.textContent = r[0]; b.textContent = String(r[1]);
            tr.appendChild(a); tr.appendChild(b); el.info.appendChild(tr);
        });
        var fs = st.v.fw_state;
        var line = fs ? (FW_STATES[fs] || fs) : '';
        if (fs === 'DOWNLOADING' && st.v.fw_progress !== undefined) line += ' ' + st.v.fw_progress + ' %';
        if (fs === 'FAILED' && st.v.fw_error) line += ' : ' + st.v.fw_error;
        el.fwState.textContent = line;
        el.fwGo.textContent = st.fwArmed ? 'Confirmer ?' : 'Mettre à jour';
        el.fwGo.disabled = !el.fwSel.value || el.fwSel.value === st.currentPkg || !online();
    }

    function openPanel() {
        el.root.classList.add('show-panel');
        st.fwArmed = false;
        $.find('.e8w-in').each(function () {
            var k = this.getAttribute('data-attr');
            this.value = st.v[k] !== undefined ? st.v[k] : '';
        });
        loadFirmware();
        renderPanel();
    }

    function loadFirmware() {
        var d = device();
        if (!d) return;
        ctx.http.get('/api/device/' + d.id).subscribe(function (dev) {
            st.dev = dev;
            st.currentPkg = dev.firmwareId ? dev.firmwareId.id : '';
            var url = '/api/otaPackages/' + dev.deviceProfileId.id + '/FIRMWARE?pageSize=100&page=0&sortProperty=createdTime&sortOrder=DESC';
            ctx.http.get(url).subscribe(function (page) {
                st.fwList = (page && page.data) || [];
                el.fwSel.innerHTML = '';
                var none = document.createElement('option');
                none.value = ''; none.textContent = st.fwList.length ? 'Choisir une version' : 'Aucune version disponible';
                el.fwSel.appendChild(none);
                st.fwList.forEach(function (p) {
                    var o = document.createElement('option');
                    o.value = p.id.id;
                    o.textContent = p.version + (p.version === st.v.firmware ? ' (installée)' : '') +
                                    (p.id.id === st.currentPkg && p.version !== st.v.firmware ? ' (demandée)' : '');
                    el.fwSel.appendChild(o);
                });
                renderPanel();
            }, function (e) { say(errText(e), true); });
        }, function (e) { say(errText(e), true); });
    }

    function updateFirmware() {
        if (!st.fwArmed) { st.fwArmed = true; renderPanel(); return; }
        st.fwArmed = false;
        var id = el.fwSel.value;
        if (!id || !st.dev) return;
        var dev = JSON.parse(JSON.stringify(st.dev));
        dev.firmwareId = { entityType: 'OTA_PACKAGE', id: id };
        el.fwGo.disabled = true;
        ctx.http.post('/api/device', dev).subscribe(function (saved) {
            st.dev = saved;
            st.currentPkg = id;
            say('Mise à jour demandée. La machine installera la version dès qu\'elle sera libre.', true);
            loadFirmware();
        }, function (e) { say(errText(e), true); renderPanel(); });
    }

    function saveCustomer() {
        var d = device();
        if (!d) return;
        var body = {};
        $.find('.e8w-in').each(function () { body[this.getAttribute('data-attr')] = this.value.trim(); });
        el.save.disabled = true;
        ctx.http.post('/api/plugins/telemetry/DEVICE/' + d.id + '/attributes/SERVER_SCOPE', body).subscribe(function () {
            el.save.disabled = false;
            Object.keys(body).forEach(function (k) { st.v[k] = body[k]; });
            say('Enregistré', true);
        }, function (e) { el.save.disabled = false; say(errText(e), true); });
    }

    $.find('.e8w-btn').on('click', function () {
        var c = this.getAttribute('data-cmd');
        if (c === 'info') { openPanel(); return; }
        if (c === 'lock' || c === 'unlock') { rpc('setRemoteLock', { locked: c === 'lock' }); return; }
        rpc(c);
    });
    $.find('.e8w-row').on('click', function () {
        if (this.classList.contains('disabled')) return;
        cycle(this.getAttribute('data-key'));
    });
    $.find('.e8w-ok').on('click', function () {
        var p = st.pending;
        rpc('setSettings', p, function (r) {
            if (r && r.ok) { st.pending = {}; say('Paramètres enregistrés'); }
        });
    });
    $.find('.e8w-close').on('click', function () { el.root.classList.remove('show-panel'); });
    $.find('.e8w-save').on('click', saveCustomer);
    $.find('.e8w-fw-go').on('click', updateFirmware);
    $.find('.e8w-fw-select').on('change', function () { st.fwArmed = false; renderPanel(); });

    st.timer = setInterval(function () { if (st.v.state === 'RUN') render(); }, 1000);
    self.onDataUpdated();
};

self.onDataUpdated = function () {
    var st = self.st;
    if (!st) return;
    (self.ctx.data || []).forEach(function (d) {
        if (!d.data || !d.data.length) return;
        var p = d.data[d.data.length - 1];
        if (p[1] === undefined || p[1] === null || p[1] === '') return;
        var v = p[1];
        if (typeof v === 'string' && /^-?\d+(\.\d+)?$/.test(v)) v = parseFloat(v);
        st.v[d.dataKey.name] = v;
        st.ts[d.dataKey.name] = p[0];
    });
    Object.keys(st.pending).forEach(function (k) {
        if (st.pending[k] === parseFloat(st.v[k])) delete st.pending[k];
    });
    self.render();
};

self.onResize = function () {};

self.onDestroy = function () {
    if (self.st && self.st.timer) clearInterval(self.st.timer);
};
