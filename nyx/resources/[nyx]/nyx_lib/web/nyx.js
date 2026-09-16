/* ==========================================================================
   Nyx — shared NUI runtime
   --------------------------------------------------------------------------
   Loaded by every Nyx resource with:

       <script src="nui://nyx_lib/web/nyx.js"></script>

   Provides four things and nothing else: an icon set, a typed bridge to Lua,
   accent swapping, and string helpers. Deliberately dependency-free — no
   framework, no build step, no npm. Edit it and reload the resource.
   ========================================================================== */
(function (global) {
    'use strict';

    /* ----------------------------------------------------------------------
       Icons
       ----------------------------------------------------------------------
       A 24x24 stroked set in the Lucide idiom. Nyx.icon('user') returns an
       SVG string; unknown names return an empty string rather than throwing,
       so a typo degrades to a missing glyph instead of a dead page.
       ---------------------------------------------------------------------- */
    var PATHS = {
        target:     '<circle cx="12" cy="12" r="9"/><circle cx="12" cy="12" r="5"/><circle cx="12" cy="12" r="1.5"/>',
        diamond:    '<path d="M12 2 22 12 12 22 2 12Z"/>',
        crosshair:  '<circle cx="12" cy="12" r="8"/><line x1="12" y1="1" x2="12" y2="5"/><line x1="12" y1="19" x2="12" y2="23"/><line x1="1" y1="12" x2="5" y2="12"/><line x1="19" y1="12" x2="23" y2="12"/>',
        grid:       '<rect x="3" y="3" width="7" height="7" rx="1.5"/><rect x="14" y="3" width="7" height="7" rx="1.5"/><rect x="3" y="14" width="7" height="7" rx="1.5"/><rect x="14" y="14" width="7" height="7" rx="1.5"/>',
        skull:      '<path d="M8 20v-2.4a7 7 0 1 1 8 0V20a1.5 1.5 0 0 1-1.5 1.5h-5A1.5 1.5 0 0 1 8 20Z"/><circle cx="9.2" cy="12" r="1.4"/><circle cx="14.8" cy="12" r="1.4"/>',
        frown:      '<circle cx="12" cy="12" r="9"/><path d="M8.5 15.8a5 5 0 0 1 7 0"/><line x1="9" y1="9.5" x2="9.01" y2="9.5"/><line x1="15" y1="9.5" x2="15.01" y2="9.5"/>',
        percent:    '<line x1="19" y1="5" x2="5" y2="19"/><circle cx="6.5" cy="6.5" r="2.5"/><circle cx="17.5" cy="17.5" r="2.5"/>',
        settings:   '<circle cx="12" cy="12" r="3.2"/><path d="M12 2v3M12 19v3M2 12h3M19 12h3M4.9 4.9 7 7M17 17l2.1 2.1M19.1 4.9 17 7M7 17l-2.1 2.1"/>',
        user:       '<path d="M20 21v-2a4 4 0 0 0-4-4H8a4 4 0 0 0-4 4v2"/><circle cx="12" cy="7" r="4"/>',
        users:      '<path d="M17 21v-2a4 4 0 0 0-4-4H6a4 4 0 0 0-4 4v2"/><circle cx="9.5" cy="7" r="4"/><path d="M22 21v-2a4 4 0 0 0-3-3.87"/>',
        search:     '<circle cx="11" cy="11" r="7"/><line x1="16.2" y1="16.2" x2="21" y2="21"/>',
        box:        '<path d="M21 8v8a2 2 0 0 1-1 1.73l-7 4a2 2 0 0 1-2 0l-7-4A2 2 0 0 1 3 16V8a2 2 0 0 1 1-1.73l7-4a2 2 0 0 1 2 0l7 4A2 2 0 0 1 21 8Z"/><path d="m3.3 7 8.7 5 8.7-5"/><line x1="12" y1="22" x2="12" y2="12"/>',
        info:       '<circle cx="12" cy="12" r="9"/><line x1="12" y1="11" x2="12" y2="16"/><line x1="12" y1="8" x2="12.01" y2="8"/>',
        hand:       '<path d="M18 11.5V6.5a1.5 1.5 0 0 0-3 0"/><path d="M15 10.5v-6a1.5 1.5 0 0 0-3 0v6"/><path d="M12 10.5v-5a1.5 1.5 0 0 0-3 0v8"/><path d="M9 13.5v-4a1.5 1.5 0 0 0-3 0V15a6.5 6.5 0 0 0 6.5 6.5h1A5.5 5.5 0 0 0 19 16v-4.5"/>',
        gift:       '<rect x="3" y="8" width="18" height="4" rx="1"/><path d="M5 12v8a1 1 0 0 0 1 1h12a1 1 0 0 0 1-1v-8"/><line x1="12" y1="8" x2="12" y2="21"/><path d="M12 8S10.6 4 8.6 4a2.4 2.4 0 0 0 0 4.8"/><path d="M12 8s1.4-4 3.4-4a2.4 2.4 0 0 1 0 4.8"/>',
        trash:      '<path d="M3 6h18"/><path d="M8 6V4a1 1 0 0 1 1-1h6a1 1 0 0 1 1 1v2"/><path d="M19 6v14a1 1 0 0 1-1 1H6a1 1 0 0 1-1-1V6"/><line x1="10" y1="11" x2="10" y2="17"/><line x1="14" y1="11" x2="14" y2="17"/>',
        x:          '<line x1="18" y1="6" x2="6" y2="18"/><line x1="6" y1="6" x2="18" y2="18"/>',
        check:      '<polyline points="20 6 9 17 4 12"/>',
        alert:      '<path d="M10.3 3.9 1.8 18a2 2 0 0 0 1.7 3h17a2 2 0 0 0 1.7-3L13.7 3.9a2 2 0 0 0-3.4 0Z"/><line x1="12" y1="9.5" x2="12" y2="13.5"/><line x1="12" y1="17" x2="12.01" y2="17"/>',
        lock:       '<rect x="4" y="10" width="16" height="11" rx="2"/><path d="M8 10V7a4 4 0 0 1 8 0v3"/>',
        car:        '<path d="M5 17H3v-5l2-5h14l2 5v5h-2"/><circle cx="7.5" cy="17" r="2"/><circle cx="16.5" cy="17" r="2"/><line x1="9.5" y1="17" x2="14.5" y2="17"/>',
        palette:    '<path d="M12 21a9 9 0 1 1 9-9c0 1.7-1.3 3-3 3h-1.6a2.4 2.4 0 0 0-1.7 4.1A1.9 1.9 0 0 1 12 21Z"/><circle cx="7.5" cy="12.5" r="1.2"/><circle cx="9.5" cy="8.5" r="1.2"/><circle cx="14.5" cy="8" r="1.2"/>',
        left:       '<polyline points="15 18 9 12 15 6"/>',
        right:      '<polyline points="9 18 15 12 9 6"/>',
        spray:      '<path d="M9 21h6a1 1 0 0 0 1-1v-9a1 1 0 0 0-1-1H9a1 1 0 0 0-1 1v9a1 1 0 0 0 1 1Z"/><path d="M10 10V6a1 1 0 0 1 1-1h2a1 1 0 0 1 1 1v4"/><line x1="17" y1="4" x2="17.01" y2="4"/><line x1="20" y1="6" x2="20.01" y2="6"/><line x1="17" y1="8" x2="17.01" y2="8"/>',
        gauge:      '<path d="M3.5 17a9 9 0 1 1 17 0"/><line x1="12" y1="14" x2="16.2" y2="9.4"/><circle cx="12" cy="14" r="1.5"/>',
        heart:      '<path d="M20.8 5.6a5 5 0 0 0-7.1 0L12 7.3l-1.7-1.7a5 5 0 1 0-7.1 7.1l8.8 8.8 8.8-8.8a5 5 0 0 0 0-7.1Z"/>',
        shield:     '<path d="M12 22s8-4 8-10V5.5L12 2 4 5.5V12c0 6 8 10 8 10Z"/>',
        mic:        '<rect x="9" y="2" width="6" height="11" rx="3"/><path d="M5 11a7 7 0 0 0 14 0"/><line x1="12" y1="18" x2="12" y2="22"/>',
        micOff:     '<rect x="9" y="2" width="6" height="11" rx="3"/><path d="M5 11a7 7 0 0 0 14 0"/><line x1="12" y1="18" x2="12" y2="22"/><line x1="3" y1="3" x2="21" y2="21"/>',
        exit:       '<path d="M9 21H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h4"/><polyline points="16 17 21 12 16 7"/><line x1="21" y1="12" x2="9" y2="12"/>',
        map:        '<path d="m9 4-6 2v14l6-2 6 2 6-2V4l-6 2Z"/><line x1="9" y1="4" x2="9" y2="18"/><line x1="15" y1="6" x2="15" y2="20"/>',
        bolt:       '<polygon points="13 2 3 14 11 14 10 22 21 10 13 10 13 2"/>',
        swap:       '<polyline points="16 3 20 7 16 11"/><line x1="20" y1="7" x2="4" y2="7"/><polyline points="8 13 4 17 8 21"/><line x1="4" y1="17" x2="20" y2="17"/>',
        plus:       '<line x1="12" y1="5" x2="12" y2="19"/><line x1="5" y1="12" x2="19" y2="12"/>',
        minus:      '<line x1="5" y1="12" x2="19" y2="12"/>',
        cash:       '<rect x="2" y="6" width="20" height="12" rx="2"/><circle cx="12" cy="12" r="2.6"/><line x1="6" y1="12" x2="6.01" y2="12"/><line x1="18" y1="12" x2="18.01" y2="12"/>',
        bank:       '<path d="m3 10 9-6 9 6"/><line x1="4" y1="21" x2="20" y2="21"/><line x1="6" y1="21" x2="6" y2="10"/><line x1="10" y1="21" x2="10" y2="10"/><line x1="14" y1="21" x2="14" y2="10"/><line x1="18" y1="21" x2="18" y2="10"/>',
        briefcase:  '<rect x="2" y="7" width="20" height="14" rx="2"/><path d="M8 7V5a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2"/>',
        layers:     '<path d="m12 2 9 5-9 5-9-5 9-5Z"/><path d="m3 12 9 5 9-5"/><path d="m3 17 9 5 9-5"/>',
        chat:       '<path d="M21 15a2 2 0 0 1-2 2H8l-5 4V5a2 2 0 0 1 2-2h14a2 2 0 0 1 2 2Z"/>',
        eye:        '<path d="M2 12s3.6-7 10-7 10 7 10 7-3.6 7-10 7-10-7-10-7Z"/><circle cx="12" cy="12" r="3"/>',
        key:        '<circle cx="8" cy="15" r="4"/><line x1="10.9" y1="12.1" x2="20" y2="3"/><line x1="17" y1="6" x2="19.5" y2="8.5"/><line x1="14.5" y1="8.5" x2="17" y2="11"/>',
        weapon:     '<path d="M3 8h18v3h-4l-2 4h-4l-1-4H3Z"/><path d="M7 15v3a2 2 0 0 0 2 2h1"/><line x1="15" y1="8" x2="15" y2="5"/>'
    };

    function icon(name, extraClass) {
        var body = PATHS[name];
        if (!body) return '';
        return '<svg class="' + (extraClass || '') + '" viewBox="0 0 24 24" fill="none" stroke="currentColor" ' +
               'stroke-width="2" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true">' + body + '</svg>';
    }

    /* ----------------------------------------------------------------------
       Escaping
       ----------------------------------------------------------------------
       Anything that came from a player — names, item labels, chat, vehicle
       names typed into a config — must go through this before it touches
       innerHTML. A player called <img src=x onerror=...> is a real thing that
       happens, and NUI is a browser: it will run that script.
       ---------------------------------------------------------------------- */
    var ESCAPES = { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' };

    function esc(value) {
        if (value === null || value === undefined) return '';
        return String(value).replace(/[&<>"']/g, function (c) { return ESCAPES[c]; });
    }

    /* Same idea for anything interpolated into a url(...) or src attribute. */
    function escUrl(value) {
        var s = String(value || '');
        if (!/^(https?:|nui:|data:image\/|\/|[\w./-])/i.test(s)) return '';
        return esc(s).replace(/[()]/g, '');
    }

    /* ----------------------------------------------------------------------
       Lua bridge
       ---------------------------------------------------------------------- */
    var RESOURCE = (typeof GetParentResourceName === 'function') ? GetParentResourceName() : 'nyx';

    /* Fire a NUI callback. Resolves with the Lua cb() payload, or null if the
       page is being previewed in a normal browser (no NUI host). */
    function post(name, data) {
        return fetch('https://' + RESOURCE + '/' + name, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json; charset=UTF-8' },
            body: JSON.stringify(data === undefined ? {} : data)
        }).then(function (r) {
            return r.json();
        }).catch(function () {
            return null;
        });
    }

    /* Register a handler for SendNUIMessage({ action = 'name', ... }). */
    var handlers = {};

    function on(action, fn) {
        (handlers[action] || (handlers[action] = [])).push(fn);
    }

    window.addEventListener('message', function (event) {
        var msg = event.data;
        if (!msg || typeof msg.action !== 'string') return;

        /* Theme pushes are handled centrally so no resource has to care. */
        if (msg.action === 'nyx:theme' && msg.accent) setAccent(msg.accent);

        var list = handlers[msg.action];
        if (!list) return;
        for (var i = 0; i < list.length; i++) {
            try {
                list[i](msg);
            } catch (err) {
                console.error('[nyx] handler for "' + msg.action + '" threw:', err);
            }
        }
    });

    /* Esc closes whatever is open. Every resource opts in by passing a fn. */
    function onEscape(fn) {
        window.addEventListener('keydown', function (e) {
            if (e.key === 'Escape' || e.key === 'Backspace') {
                /* Backspace inside a text field means "delete", not "close". */
                if (e.key === 'Backspace' && /^(INPUT|TEXTAREA)$/.test(document.activeElement.tagName)) return;
                e.preventDefault();
                fn();
            }
        });
    }

    /* ----------------------------------------------------------------------
       Accent
       ----------------------------------------------------------------------
       Accepts '#ff2bd6', 'ff2bd6' or '255,43,214' and writes the channel
       triplet the stylesheet derives everything else from.
       ---------------------------------------------------------------------- */
    function toRgb(input) {
        if (!input) return null;
        var s = String(input).trim();

        if (s.indexOf(',') !== -1) {
            var parts = s.split(',').map(function (n) { return parseInt(n, 10); });
            if (parts.length === 3 && parts.every(function (n) { return n >= 0 && n <= 255; })) return parts.join(', ');
            return null;
        }

        s = s.replace('#', '');
        if (s.length === 3) s = s[0] + s[0] + s[1] + s[1] + s[2] + s[2];
        if (!/^[0-9a-f]{6}$/i.test(s)) return null;

        return [
            parseInt(s.slice(0, 2), 16),
            parseInt(s.slice(2, 4), 16),
            parseInt(s.slice(4, 6), 16)
        ].join(', ');
    }

    function setAccent(input) {
        var rgb = toRgb(input);
        if (!rgb) return false;
        document.documentElement.style.setProperty('--nyx-accent-rgb', rgb);
        return true;
    }

    /* ----------------------------------------------------------------------
       Formatting
       ---------------------------------------------------------------------- */
    function money(amount, symbol) {
        var n = Number(amount) || 0;
        return (symbol === undefined ? '$' : symbol) + Math.round(n).toLocaleString('en-US');
    }

    /* 1400 -> "1.4K", 2300000 -> "2.3M". Used for slot counts and cash HUDs. */
    function compact(amount) {
        var n = Number(amount) || 0;
        var abs = Math.abs(n);
        if (abs >= 1e9) return (n / 1e9).toFixed(1).replace(/\.0$/, '') + 'B';
        if (abs >= 1e6) return (n / 1e6).toFixed(1).replace(/\.0$/, '') + 'M';
        if (abs >= 1e3) return (n / 1e3).toFixed(1).replace(/\.0$/, '') + 'K';
        return String(Math.round(n));
    }

    /* Trailing-edge debounce, for search boxes that would otherwise hammer Lua. */
    function debounce(fn, wait) {
        var t;
        return function () {
            var args = arguments, self = this;
            clearTimeout(t);
            t = setTimeout(function () { fn.apply(self, args); }, wait || 150);
        };
    }

    /* ----------------------------------------------------------------------
       Crosshair
       ----------------------------------------------------------------------
       Shared so the editor preview in nyx_menu and the live overlay in
       nyx_hud are literally the same drawing code. If they were two
       implementations they would drift, and the setting the player tuned would
       stop matching what they see in a fight.

       settings: { style, size, thickness, gap, colour, opacity, outline, dot }
       style: 'cross' | 'tshape' | 'circle' | 'dot'
       ---------------------------------------------------------------------- */
    function crosshair(container, s) {
        if (!container) return;
        s = s || {};

        var style = s.style || 'cross';
        var size = Math.max(Number(s.size) || 10, 0);
        var thick = Math.max(Number(s.thickness) || 2, 1);
        var gap = Math.max(Number(s.gap) || 4, 0);
        var colour = s.colour || s.color || '#ff2bd6';
        var opacity = Math.min(Math.max(Number(s.opacity === undefined ? 100 : s.opacity) / 100, 0), 1);
        var outline = s.outline !== false;
        var dot = s.dot === true;

        /* A hex value goes into a style attribute, so validate rather than
           trust: an arbitrary string here would be an injection point. */
        if (!/^#?[0-9a-f]{3}([0-9a-f]{3})?$/i.test(String(colour))) colour = '#ff2bd6';
        if (colour[0] !== '#') colour = '#' + colour;

        container.textContent = '';

        /* The bars are positioned against this box, so it must establish a
           containing block. Read the COMPUTED position, not the inline one:
           nyx_hud's overlay gets `position: fixed` from a stylesheet, and
           writing `relative` inline would override it and collapse the whole
           overlay to a zero-height box in normal flow. */
        if (getComputedStyle(container).position === 'static') {
            container.style.position = 'relative';
        }

        var shadow = outline ? '0 0 0 1px rgba(0,0,0,.85)' : 'none';

        function bar(w, h, dx, dy) {
            var el = document.createElement('div');
            el.style.cssText =
                'position:absolute;left:50%;top:50%;pointer-events:none;' +
                'width:' + w + 'px;height:' + h + 'px;' +
                'background:' + colour + ';opacity:' + opacity + ';' +
                'box-shadow:' + shadow + ';' +
                'transform:translate(-50%,-50%) translate(' + dx + 'px,' + dy + 'px);';
            container.appendChild(el);
        }

        if (style === 'circle') {
            var ring = document.createElement('div');
            var d = (gap + size) * 2;
            ring.style.cssText =
                'position:absolute;left:50%;top:50%;pointer-events:none;box-sizing:border-box;' +
                'width:' + d + 'px;height:' + d + 'px;border-radius:50%;' +
                'border:' + thick + 'px solid ' + colour + ';opacity:' + opacity + ';' +
                'box-shadow:' + shadow + ';transform:translate(-50%,-50%);';
            container.appendChild(ring);
        } else if (style !== 'dot') {
            var offset = gap + size / 2;
            if (style !== 'tshape') bar(thick, size, 0, -offset);   /* top */
            bar(thick, size, 0, offset);                            /* bottom */
            bar(size, thick, -offset, 0);                           /* left */
            bar(size, thick, offset, 0);                            /* right */
        }

        if (dot || style === 'dot') {
            var d2 = document.createElement('div');
            d2.style.cssText =
                'position:absolute;left:50%;top:50%;pointer-events:none;' +
                'width:' + thick + 'px;height:' + thick + 'px;border-radius:50%;' +
                'background:' + colour + ';opacity:' + opacity + ';' +
                'box-shadow:' + shadow + ';transform:translate(-50%,-50%);';
            container.appendChild(d2);
        }
    }

    global.Nyx = {
        icon: icon,
        icons: PATHS,
        esc: esc,
        escUrl: escUrl,
        post: post,
        on: on,
        onEscape: onEscape,
        setAccent: setAccent,
        toRgb: toRgb,
        money: money,
        compact: compact,
        debounce: debounce,
        crosshair: crosshair,
        resource: RESOURCE
    };
})(window);
