/* ==========================================================================
   nyx_menu — page controller
   --------------------------------------------------------------------------
   Renders three tabs from a payload Lua sends on open, and sends ids back.
   The page never learns a coordinate, a weapon hash or a price: it knows only
   what it has to draw.
   ========================================================================== */
(function () {
    'use strict';

    var shell = document.getElementById('shell');
    var els = {
        tabs: document.getElementById('tabs'),
        brand: document.getElementById('brand'),
        stats: document.getElementById('stats'),
        name: document.getElementById('playerName'),
        avatar: document.getElementById('avatar'),
        close: document.getElementById('closeBtn'),
        rail: document.getElementById('rail'),
        content: document.getElementById('content'),
        body: document.getElementById('body')
    };

    var TABS = [
        { id: 'locations', label: 'Locations', icon: 'diamond' },
        { id: 'weapons',   label: 'Weapons',   icon: 'crosshair' },
        { id: 'misc',      label: 'Miscellaneous', icon: 'grid' }
    ];

    var MISC_SECTIONS = [
        { id: 'hud', label: 'HUD Options' },
        { id: 'crosshair', label: 'Crosshair' }
    ];

    var HUD_TOGGLES = [
        { key: 'health', label: 'Health & Armor' },
        { key: 'announcements', label: 'Announcements' },
        { key: 'speedometer', label: 'Speedometer' },
        { key: 'watermark', label: 'Watermark' },
        { key: 'fps', label: 'FPS Counter' },
        { key: 'money', label: 'Money UI' },
        { key: 'crosshair', label: 'Crosshair' },
        { key: 'chat', label: 'Chat' },
        { key: 'map', label: 'Map' }
    ];

    var XHAIR_STYLES = [
        { id: 'cross', label: 'Cross' },
        { id: 'tshape', label: 'T-Shape' },
        { id: 'circle', label: 'Circle' },
        { id: 'dot', label: 'Dot' }
    ];

    var XHAIR_COLOURS = ['#ff2bd6', '#12e05a', '#ffc41f', '#2bb4ff', '#ff3b3b', '#8ce81d', '#ffffff', '#000000'];

    /* Current view. `section` remembers a position per tab so switching away
       and back does not dump the player at the top of the list again. */
    var state = {
        open: false,
        tab: 'locations',
        section: { locations: 0, weapons: 0, misc: 0 },
        data: {},
        hud: null
    };

    /* ----------------------------------------------------------------------
       Helpers
       ---------------------------------------------------------------------- */

    /* Stable hue from a string, so a card without art keeps one colour for
       the life of the config instead of changing every render. */
    function hueOf(text) {
        var h = 0;
        for (var i = 0; i < text.length; i++) h = (h * 31 + text.charCodeAt(i)) % 360;
        return h;
    }

    function initials(label) {
        return String(label || '?')
            .split(/\s+/)
            .slice(0, 2)
            .map(function (w) { return w[0] || ''; })
            .join('')
            .toUpperCase();
    }

    /* The art area of a card: a real image if config gave one, otherwise a
       generated tile. Both paths escape their inputs. */
    function tileHtml(item) {
        var url = item.image ? Nyx.escUrl(item.image) : '';
        if (url) return '<img src="' + url + '" alt="" loading="lazy">';

        var h = hueOf(item.id || item.label || '');
        return '<div class="tile-fallback" style="background:linear-gradient(140deg,' +
            'hsl(' + h + ',48%,26%),hsl(' + ((h + 40) % 360) + ',42%,13%))">' +
            Nyx.esc(initials(item.label)) + '</div>';
    }

    function el(html) {
        var d = document.createElement('div');
        d.innerHTML = html;
        return d.firstElementChild;
    }

    function click(node, fn) {
        node.addEventListener('click', fn);
        node.addEventListener('mouseenter', function () { Nyx.post('sound', { key: 'hover' }); });
        return node;
    }

    /* ----------------------------------------------------------------------
       Top bar
       ---------------------------------------------------------------------- */

    function renderTabs() {
        els.tabs.textContent = '';

        TABS.forEach(function (tab) {
            /* The Weapons tab disappears entirely when the player has access to
               nothing in it — an empty tab is worse than no tab. */
            if (tab.id === 'weapons' && !(state.data.weapons || []).length) return;

            var b = el('<button class="nyx-tab' + (state.tab === tab.id ? ' is-active' : '') + '">' +
                Nyx.icon(tab.icon) + '<span>' + Nyx.esc(tab.label) + '</span></button>');

            click(b, function () {
                if (state.tab === tab.id) return;
                state.tab = tab.id;
                Nyx.post('sound', { key: 'select' });
                render();
            });

            els.tabs.appendChild(b);
        });
    }

    function renderBrand() {
        var brand = state.data.brand || {};
        var logo = brand.logo ? Nyx.escUrl(brand.logo) : '';

        els.brand.innerHTML = logo
            ? '<img src="' + logo + '" alt="">'
            : '<div class="nyx-brand-text">' + Nyx.esc(brand.name || 'NYX') + '</div>';
    }

    function renderStats() {
        var s = state.data.stats;

        if (!s) {
            els.stats.classList.add('nyx-hidden');
            return;
        }

        els.stats.classList.remove('nyx-hidden');
        els.stats.innerHTML =
            '<div class="nyx-stat">' + Nyx.icon('skull') + '<span>' + Nyx.esc(s.kills) + '</span></div>' +
            '<div class="nyx-stat">' + Nyx.icon('frown') + '<span>' + Nyx.esc(s.deaths) + '</span></div>' +
            '<div class="nyx-stat">' + Nyx.icon('percent') + '<span>' + Nyx.esc(s.ratio) + '</span></div>';
    }

    function renderProfile() {
        var p = state.data.player || {};
        els.name.textContent = p.name || '';
        els.avatar.innerHTML = Nyx.icon('user', 'avatar-glyph');
        els.avatar.title = [p.job, p.grade].filter(Boolean).join(' — ');
        els.close.innerHTML = Nyx.icon('x');
    }

    /* ----------------------------------------------------------------------
       Rail
       ---------------------------------------------------------------------- */

    function sectionsFor(tab) {
        if (tab === 'misc') return MISC_SECTIONS;
        return (state.data[tab] || []).map(function (s) { return { id: s.id, label: s.label }; });
    }

    function renderRail() {
        var sections = sectionsFor(state.tab);
        var current = state.section[state.tab] || 0;

        els.rail.textContent = '';

        sections.forEach(function (section, index) {
            var b = el('<button class="nyx-rail-item' + (index === current ? ' is-active' : '') + '">' +
                '<span>' + Nyx.esc(section.label) + '</span></button>');

            click(b, function () {
                state.section[state.tab] = index;
                Nyx.post('sound', { key: 'select' });
                render();
            });

            els.rail.appendChild(b);
        });

        if (!sections.length) {
            els.rail.appendChild(el('<div class="nyx-empty">' + Nyx.icon('box') + '<span>Nothing here</span></div>'));
        }
    }

    /* ----------------------------------------------------------------------
       Locations / weapons
       ---------------------------------------------------------------------- */

    function cardNode(item, onPick) {
        var count = typeof item.count === 'number' ? item.count : null;

        var badge = count === null
            ? (item.kit ? '<span class="nyx-badge">' + Nyx.esc(item.kit) + ' ' + Nyx.icon('layers') + '</span>' : '')
            : '<span class="nyx-badge' + (count > 0 ? ' is-hot' : '') + '">' +
                  Nyx.esc(count) + ' ' + Nyx.icon('user') + '</span>';

        var node = el(
            '<button class="nyx-card">' +
                '<span class="nyx-card-shot">' + tileHtml(item) + '</span>' +
                '<span class="nyx-card-row">' +
                    '<span class="nyx-card-name">' + Nyx.esc(item.label) + '</span>' +
                    badge +
                '</span>' +
            '</button>'
        );

        click(node, function () {
            /* Mark the pick immediately. The menu closes on teleport, but on
               the weapons tab it stays open and the highlight is the only
               feedback that the click registered. */
            Array.prototype.forEach.call(
                els.content.querySelectorAll('.nyx-card.is-active'),
                function (c) { c.classList.remove('is-active'); }
            );
            node.classList.add('is-active');
            onPick(item);
        });

        return node;
    }

    function renderGroups(groups, key, onPick) {
        els.content.textContent = '';

        if (!groups || !groups.length) {
            els.content.appendChild(el('<div class="nyx-empty">' + Nyx.icon('box') +
                '<span>This section is empty.</span></div>'));
            return;
        }

        groups.forEach(function (group) {
            var wrap = el('<section class="group"></section>');
            wrap.appendChild(el('<div class="nyx-strip nyx-strip--full">' + Nyx.esc(group.label) + '</div>'));

            var grid = el('<div class="nyx-grid"></div>');
            (group[key] || []).forEach(function (item, i) {
                var card = cardNode(item, onPick);
                /* Stagger the entrance so a long list cascades instead of
                   snapping in as one block. Capped so card 40 is not late. */
                card.style.animationDelay = Math.min(i * 22, 260) + 'ms';
                grid.appendChild(card);
            });

            wrap.appendChild(grid);
            els.content.appendChild(wrap);
        });
    }

    /* ----------------------------------------------------------------------
       Miscellaneous — HUD options
       ---------------------------------------------------------------------- */

    var HUD_PREVIEWS = [
        '<div class="pv-pill">' +
            '<span class="pv-chip">' + Nyx.icon('heart') + '</span>' +
            '<span class="pv-chip pv-chip--muted">' + Nyx.icon('micOff') + '</span>' +
            '<span class="pv-chip pv-chip--outline">' + Nyx.icon('shield') + '</span>' +
        '</div>',

        '<div class="pv-rings">' +
            '<span class="pv-ring">' + Nyx.icon('heart') + '</span>' +
            '<span class="pv-ring pv-ring--muted">' + Nyx.icon('micOff') + '</span>' +
            '<span class="pv-ring">' + Nyx.icon('shield') + '</span>' +
        '</div>',

        '<div class="pv-bars">' +
            '<span class="pv-bar"><i>500,000,000</i>' + Nyx.icon('cash') + '</span>' +
            '<span class="pv-bar"><i>500,000,000</i>' + Nyx.icon('bank') + '</span>' +
            '<span class="pv-bar is-accent"><i>500,000,000</i>' + Nyx.icon('box') + '</span>' +
            '<span class="pv-bar"><i>Police — Recruit</i>' + Nyx.icon('briefcase') + '</span>' +
            '<span class="pv-bar"><i>Nyx — Owner</i>' + Nyx.icon('user') + '</span>' +
        '</div>',

        '<div class="pv-text">' +
            '<span>$ 500000000</span>' +
            '<span>$ 500000000</span>' +
            '<span class="is-accent">$ 500000000</span>' +
            '<span>POLICE — RECRUIT</span>' +
            '<span>NYX — OWNER</span>' +
        '</div>'
    ];

    function hudMissing() {
        els.content.textContent = '';
        els.content.appendChild(el('<div class="nyx-empty">' + Nyx.icon('alert') +
            '<span>nyx_hud is not running.<br>Start it to configure the HUD from here.</span></div>'));
    }

    function pushHud(field, payload) {
        Nyx.post('hud', payload).then(function (res) {
            if (res && res.ok && res.hud) {
                state.hud = res.hud;
                if (state.tab === 'misc') renderContent();
            }
        });
    }

    function renderHudOptions() {
        var hud = state.hud;
        if (!hud) return hudMissing();

        els.content.textContent = '';

        var grid = el('<div class="misc"></div>');

        /* --- column 1: HUD style ------------------------------------------ */
        var styleCol = el('<div class="misc-col"></div>');
        styleCol.appendChild(el('<div class="nyx-strip nyx-strip--full">HUD Customization</div>'));

        /* Cards 1-2 pick the status cluster, cards 3-4 pick the money panel.
           They are two independent choices sharing one grid, which is why two
           cards can be lit at once. */
        var cards = el('<div class="hud-styles"></div>');
        HUD_PREVIEWS.forEach(function (preview, i) {
            var n = i + 1;
            var field = n <= 2 ? 'style' : 'moneyStyle';
            var active = hud[field] === n;

            var card = el('<button class="hud-card' + (active ? ' is-active' : '') + '">' +
                '<div class="hud-preview">' + preview + '</div>' +
                '<span class="hud-card-tag">HUD ' + n + '</span></button>');

            click(card, function () { pushHud(field, { field: field, value: n }); });
            cards.appendChild(card);
        });
        styleCol.appendChild(cards);

        /* --- column 2: interface colour ----------------------------------- */
        var colourCol = el('<div class="misc-col"></div>');
        colourCol.appendChild(el('<div class="nyx-strip nyx-strip--full">Interface Color</div>'));

        var swatchPanel = el('<div class="nyx-panel pad"></div>');
        var swatches = el('<div class="nyx-swatches"></div>');

        (state.data.accents || []).forEach(function (accent) {
            var active = state.data.accent && state.data.accent.toLowerCase() === accent.hex.toLowerCase();
            var node = el('<button class="nyx-swatch' + (active ? ' is-active' : '') +
                (accent.locked ? ' is-locked' : '') + '" title="' + Nyx.esc(accent.label) + '"></button>');

            /* Validated hex from Lua, but set through style rather than
               interpolated into the class string all the same. */
            node.style.background = /^#[0-9a-f]{6}$/i.test(accent.hex) ? accent.hex : '#ff2bd6';
            node.style.color = node.style.background;

            if (accent.locked) {
                node.innerHTML = Nyx.icon('lock');
            } else {
                click(node, function () {
                    Nyx.post('setAccent', { id: accent.id }).then(function (res) {
                        if (res && res.ok) {
                            state.data.accent = accent.hex;
                            renderContent();
                        }
                    });
                });
            }

            swatches.appendChild(node);
        });

        swatchPanel.appendChild(swatches);
        colourCol.appendChild(swatchPanel);

        /* --- column 3: toggles -------------------------------------------- */
        var toggleCol = el('<div class="misc-col"></div>');
        toggleCol.appendChild(el('<div class="nyx-strip nyx-strip--full">Toggle HUDs</div>'));

        HUD_TOGGLES.forEach(function (item) {
            var on = !!(hud.toggles && hud.toggles[item.key]);
            var node = el('<button class="nyx-toggle' + (on ? ' is-on' : '') + '">' +
                '<span class="nyx-toggle-dot"></span><span>' + Nyx.esc(item.label) + '</span></button>');

            click(node, function () {
                pushHud('toggle', { field: 'toggle', key: item.key, value: !on });
            });

            toggleCol.appendChild(node);
        });

        grid.appendChild(styleCol);
        grid.appendChild(colourCol);
        grid.appendChild(toggleCol);
        els.content.appendChild(grid);
    }

    /* ----------------------------------------------------------------------
       Miscellaneous — crosshair
       ---------------------------------------------------------------------- */

    function renderCrosshair() {
        var hud = state.hud;
        if (!hud) return hudMissing();

        var x = hud.crosshair || {};
        els.content.textContent = '';

        var grid = el('<div class="misc"></div>');

        /* --- preview ------------------------------------------------------ */
        var previewCol = el('<div class="misc-col"></div>');
        previewCol.appendChild(el('<div class="nyx-strip nyx-strip--full">Preview</div>'));

        var preview = el('<div class="xhair-preview"><div class="xhair-stage" id="xstage"></div></div>');
        previewCol.appendChild(preview);

        /* --- shape and colour --------------------------------------------- */
        var shapeCol = el('<div class="misc-col"></div>');
        shapeCol.appendChild(el('<div class="nyx-strip nyx-strip--full">Shape</div>'));

        var shapePanel = el('<div class="nyx-panel pad nyx-col"></div>');
        var seg = el('<div class="seg"></div>');

        XHAIR_STYLES.forEach(function (style) {
            var b = el('<button' + (x.style === style.id ? ' class="is-active"' : '') + '>' +
                Nyx.esc(style.label) + '</button>');
            click(b, function () { commit({ style: style.id }); });
            seg.appendChild(b);
        });

        shapePanel.appendChild(seg);

        [
            { key: 'size', label: 'Length', min: 0, max: 30 },
            { key: 'thickness', label: 'Thickness', min: 1, max: 10 },
            { key: 'gap', label: 'Gap', min: 0, max: 30 },
            { key: 'opacity', label: 'Opacity', min: 10, max: 100 }
        ].forEach(function (slider) {
            var value = x[slider.key] === undefined ? slider.min : x[slider.key];
            var row = el('<div class="slider-row">' +
                '<div class="slider-head"><span>' + slider.label + '</span><b>' + Nyx.esc(value) + '</b></div>' +
                '<input class="nyx-range" type="range" min="' + slider.min + '" max="' + slider.max +
                '" value="' + Nyx.esc(value) + '"></div>');

            var input = row.querySelector('input');
            var readout = row.querySelector('b');

            /* Redraw the preview on every frame of the drag, but only tell Lua
               when the drag settles — otherwise a slider sends ~60 NUI
               callbacks a second. */
            input.addEventListener('input', function () {
                readout.textContent = input.value;
                x[slider.key] = Number(input.value);
                paint();
            });
            input.addEventListener('change', function () {
                commit({}, true);
            });

            shapePanel.appendChild(row);
        });

        var colourRow = el('<div class="nyx-swatches"></div>');
        XHAIR_COLOURS.forEach(function (hex) {
            var active = (x.colour || '').toLowerCase() === hex.toLowerCase();
            var node = el('<button class="nyx-swatch' + (active ? ' is-active' : '') + '"></button>');
            node.style.background = hex;
            node.style.color = hex;
            click(node, function () { commit({ colour: hex }); });
            colourRow.appendChild(node);
        });

        shapePanel.appendChild(el('<div class="nyx-strip">Color</div>'));
        shapePanel.appendChild(colourRow);
        shapeCol.appendChild(shapePanel);

        /* --- switches ------------------------------------------------------ */
        var optionCol = el('<div class="misc-col"></div>');
        optionCol.appendChild(el('<div class="nyx-strip nyx-strip--full">Options</div>'));

        [
            { key: 'outline', label: 'Outline' },
            { key: 'dot', label: 'Center Dot' },
            { key: 'dynamic', label: 'Dynamic Spread' },
            { key: 'hideInVehicle', label: 'Hide In Vehicle' },
            { key: 'onlyWhenArmed', label: 'Only When Armed' }
        ].forEach(function (option) {
            var on = !!x[option.key];
            var node = el('<button class="nyx-toggle' + (on ? ' is-on' : '') + '">' +
                '<span class="nyx-toggle-dot"></span><span>' + Nyx.esc(option.label) + '</span></button>');

            click(node, function () {
                var patch = {};
                patch[option.key] = !on;
                commit(patch);
            });

            optionCol.appendChild(node);
        });

        grid.appendChild(previewCol);
        grid.appendChild(shapeCol);
        grid.appendChild(optionCol);
        els.content.appendChild(grid);

        paint();

        function paint() {
            Nyx.crosshair(document.getElementById('xstage'), x);
        }

        /* `silent` redraws without rebuilding the panel, which would steal
           focus from the slider the player is still dragging. */
        function commit(patch, silent) {
            Object.keys(patch || {}).forEach(function (k) { x[k] = patch[k]; });
            hud.crosshair = x;
            Nyx.post('hud', { field: 'crosshair', value: x });
            if (silent) paint(); else renderContent();
        }
    }

    /* ----------------------------------------------------------------------
       Render
       ---------------------------------------------------------------------- */

    function renderContent() {
        var index = state.section[state.tab] || 0;

        if (state.tab === 'misc') {
            els.body.classList.add('is-misc');
            if (MISC_SECTIONS[index] && MISC_SECTIONS[index].id === 'crosshair') renderCrosshair();
            else renderHudOptions();
            return;
        }

        els.body.classList.remove('is-misc');

        var sections = state.data[state.tab] || [];
        var section = sections[index];

        if (!section) {
            els.content.textContent = '';
            els.content.appendChild(el('<div class="nyx-empty">' + Nyx.icon('box') +
                '<span>Nothing configured for this tab yet.</span></div>'));
            return;
        }

        if (state.tab === 'locations') {
            renderGroups(section.groups, 'spots', function (spot) {
                Nyx.post('teleport', { id: spot.id });
            });
        } else {
            renderGroups(section.groups, 'items', function (item) {
                Nyx.post('giveWeapon', { id: item.id });
            });
        }
    }

    function render() {
        /* Clamp the remembered rail position: a config reload can shrink a tab
           out from under an index we are still holding. */
        var sections = sectionsFor(state.tab);
        if (state.section[state.tab] >= sections.length) state.section[state.tab] = 0;

        renderTabs();
        renderBrand();
        renderStats();
        renderProfile();
        renderRail();
        renderContent();
    }

    /* ----------------------------------------------------------------------
       Lua messages
       ---------------------------------------------------------------------- */

    function close() {
        if (!state.open) return;
        state.open = false;
        shell.classList.add('nyx-hidden');
        Nyx.post('close');
    }

    Nyx.on('open', function (msg) {
        state.open = true;
        state.data = msg;
        state.hud = msg.hud || null;
        if (msg.accent) Nyx.setAccent(msg.accent);

        /* A tab that vanished between opens (weapons access revoked) would
           otherwise leave the menu on a blank page. */
        if (state.tab === 'weapons' && !(msg.weapons || []).length) state.tab = 'locations';

        shell.classList.remove('nyx-hidden');
        render();
    });

    Nyx.on('close', function () {
        state.open = false;
        shell.classList.add('nyx-hidden');
    });

    /* Live pushes while the menu is open. */
    Nyx.on('occupancy', function (msg) {
        var counts = msg.occupancy || {};
        (state.data.locations || []).forEach(function (section) {
            (section.groups || []).forEach(function (group) {
                (group.spots || []).forEach(function (spot) { spot.count = counts[spot.id] || 0; });
            });
        });
        if (state.open && state.tab === 'locations') renderContent();
    });

    Nyx.on('weapons', function (msg) {
        state.data.weapons = msg.weapons || [];
        if (state.open) render();
    });

    Nyx.on('stats', function (msg) {
        state.data.stats = msg.stats;
        if (state.open) renderStats();
    });

    els.close.addEventListener('click', close);
    Nyx.onEscape(close);
})();
