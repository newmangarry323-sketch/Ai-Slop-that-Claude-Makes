/* ==========================================================================
   nyx_dealership — page
   ========================================================================== */
(function () {
    'use strict';

    var shell = document.getElementById('shell');
    var els = {
        title: document.getElementById('title'),
        titleIcon: document.getElementById('titleIcon'),
        searchIcon: document.getElementById('searchIcon'),
        close: document.getElementById('closeBtn'),
        search: document.getElementById('search'),
        list: document.getElementById('list'),
        bar: document.getElementById('bar'),
        testDrive: document.getElementById('testDrive')
    };

    var state = {
        open: false,
        categories: [],
        category: null,     // open category id, or null for the category list
        page: 0,
        perPage: 12,
        selected: null,
        stats: null,
        respray: null,
        resprayIndex: 0,
        testDrive: false,
        query: ''
    };

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

    function categoryById(id) {
        for (var i = 0; i < state.categories.length; i++) {
            if (state.categories[i].id === id) return state.categories[i];
        }
        return null;
    }

    /* A search spans every category; without a query we stay inside whichever
       category is open. */
    function visibleVehicles() {
        var query = state.query.trim().toLowerCase();

        if (query) {
            var hits = [];
            state.categories.forEach(function (cat) {
                cat.vehicles.forEach(function (v) {
                    if (v.label.toLowerCase().indexOf(query) !== -1) hits.push(v);
                });
            });
            return hits;
        }

        var cat = categoryById(state.category);
        return cat ? cat.vehicles : [];
    }

    /* ----------------------------------------------------------------------
       Left rail
       ---------------------------------------------------------------------- */

    function renderList() {
        els.list.textContent = '';

        var inList = !state.category && !state.query.trim();

        if (inList) {
            state.categories.forEach(function (cat) {
                var node = el('<button class="cat"><span>' + Nyx.esc(cat.label) + '</span>' +
                    '<i>' + Nyx.esc(cat.count) + '</i></button>');

                click(node, function () {
                    if (!cat.count) return;
                    state.category = cat.id;
                    state.page = 0;
                    Nyx.post('sound', { key: 'select' });
                    renderList();
                });

                if (!cat.count) node.style.opacity = '.4';
                els.list.appendChild(node);
            });
            return;
        }

        var vehicles = visibleVehicles();
        var pages = Math.max(Math.ceil(vehicles.length / state.perPage), 1);
        if (state.page >= pages) state.page = pages - 1;

        var cat = categoryById(state.category);
        var heading = state.query.trim() ? 'Results' : (cat ? cat.label : '');

        /* Pager: "1 of N" on the left, "shown / total" plus arrows on the
           right, matching the reference layout. */
        var pager = el(
            '<div class="pager">' +
                '<span>' + Nyx.esc(state.page + 1) + ' of ' + Nyx.esc(pages) + '</span>' +
                '<span class="pager-nav">' +
                    '<button data-nav="prev">' + Nyx.icon('left') + '</button>' +
                    '<span>' + Nyx.esc(Math.min(vehicles.length, (state.page + 1) * state.perPage)) +
                        ' / ' + Nyx.esc(vehicles.length) + '</span>' +
                    '<button data-nav="next">' + Nyx.icon('right') + '</button>' +
                '</span>' +
            '</div>'
        );

        pager.querySelector('[data-nav="prev"]').disabled = state.page === 0;
        pager.querySelector('[data-nav="next"]').disabled = state.page >= pages - 1;

        pager.addEventListener('click', function (e) {
            var nav = e.target.closest ? e.target.closest('[data-nav]') : null;
            if (!nav) return;
            state.page += nav.getAttribute('data-nav') === 'next' ? 1 : -1;
            renderList();
        });

        els.list.appendChild(pager);

        if (heading) {
            var back = el('<button class="cat"><span>&lsaquo; ' + Nyx.esc(heading) + '</span></button>');
            click(back, function () {
                state.category = null;
                state.query = '';
                els.search.value = '';
                state.page = 0;
                renderList();
            });
            els.list.appendChild(back);
        }

        var slice = vehicles.slice(state.page * state.perPage, (state.page + 1) * state.perPage);

        if (!slice.length) {
            els.list.appendChild(el('<div class="nyx-empty">' + Nyx.icon('car') +
                '<span>Nothing here.</span></div>'));
            return;
        }

        slice.forEach(function (vehicle) {
            var active = state.selected && state.selected.id === vehicle.id;
            var node = el('<button class="veh' + (active ? ' is-active' : '') +
                (vehicle.owned ? ' is-owned' : '') + '">' +
                '<span class="veh-name">' + Nyx.esc(vehicle.label) + '</span>' +
                '<span class="veh-price">' + Nyx.esc(Nyx.money(vehicle.price)) + '</span></button>');

            click(node, function () { select(vehicle); });
            els.list.appendChild(node);
        });
    }

    /* ----------------------------------------------------------------------
       Selection
       ---------------------------------------------------------------------- */

    function select(vehicle) {
        state.selected = vehicle;
        state.stats = null;
        renderList();
        renderBar();

        Nyx.post('select', { id: vehicle.id }).then(function (res) {
            if (!res || !res.ok) {
                /* The model failed to stream. Say so in the bar rather than
                   leaving four meters sitting at zero with no explanation. */
                state.stats = null;
                state.selected.unavailable = true;
            } else {
                state.stats = res.stats;
            }
            renderBar();
        });
    }

    /* ----------------------------------------------------------------------
       Bottom bar
       ---------------------------------------------------------------------- */

    var METERS = [
        { key: 'power', label: 'Power' },
        { key: 'acceleration', label: 'Acceleration' },
        { key: 'braking', label: 'Braking' },
        { key: 'handling', label: 'Handling' }
    ];

    function renderBar() {
        var v = state.selected;

        if (!v) {
            els.bar.innerHTML = '<div class="bar-empty">Pick a vehicle to preview it.</div>';
            return;
        }

        var stats = state.stats || {};

        var meters = METERS.map(function (meter) {
            var value = stats[meter.key] === undefined ? 0 : stats[meter.key];
            return '<div class="nyx-meter">' +
                '<div class="nyx-meter-head">' +
                    '<span class="nyx-meter-label">' + meter.label + '</span>' +
                    '<span class="nyx-meter-value">' + Nyx.esc(value) + '</span>' +
                '</div>' +
                '<div class="nyx-meter-track"><div class="nyx-meter-fill" style="width:' + value + '%"></div></div>' +
            '</div>';
        }).join('');

        els.bar.innerHTML =
            '<button class="nyx-btn nyx-btn--primary buy" id="buyBtn">' + (v.owned ? 'Owned' : 'Buy') + '</button>' +
            '<div class="bar-name">' +
                '<b>' + Nyx.esc(v.label) + '</b>' +
                '<span>' + Nyx.esc(Nyx.money(v.price)) + '</span>' +
            '</div>' +
            '<div class="bar-meters">' + meters + '</div>' +
            '<div class="bar-actions">' +
                (state.testDriveEnabled
                    ? '<button class="nyx-btn" id="tdBtn">' + Nyx.icon('gauge') + '<span>Test Drive</span></button>'
                    : '') +
                (state.respray
                    ? '<button class="nyx-btn" id="rsBtn">' + Nyx.icon('spray') + '<span>Respray</span></button>'
                    : '') +
            '</div>';

        var buy = document.getElementById('buyBtn');
        if (v.owned || v.unavailable) {
            buy.setAttribute('aria-disabled', 'true');
        } else {
            click(buy, function () { Nyx.post('buy', { id: v.id }); });
        }

        var td = document.getElementById('tdBtn');
        if (td) click(td, function () { Nyx.post('testDrive', { id: v.id }); });

        var rs = document.getElementById('rsBtn');
        if (rs) {
            click(rs, function () {
                /* Respray cycles the palette rather than opening a sub-menu —
                   one button, one visible change, no extra chrome. */
                state.resprayIndex = (state.resprayIndex % state.respray.length) + 1;
                Nyx.post('respray', { index: state.resprayIndex });
            });
        }
    }

    /* ----------------------------------------------------------------------
       Messages
       ---------------------------------------------------------------------- */

    function close() {
        if (!state.open) return;
        state.open = false;
        shell.classList.add('nyx-hidden');
        Nyx.post('close');
    }

    Nyx.on('open', function (msg) {
        state.open = true;
        state.categories = msg.categories || [];
        state.perPage = msg.perPage || 12;
        state.respray = msg.respray || null;
        state.testDriveEnabled = !!msg.testDrive;
        state.category = null;
        state.page = 0;
        state.selected = null;
        state.stats = null;
        state.query = '';

        if (msg.accent) Nyx.setAccent(msg.accent);

        els.title.textContent = msg.title || 'Dealership';
        els.titleIcon.innerHTML = Nyx.icon('car');
        els.searchIcon.innerHTML = Nyx.icon('search');
        els.search.value = '';

        shell.classList.remove('nyx-hidden');
        renderList();
        renderBar();
    });

    Nyx.on('close', function () {
        state.open = false;
        shell.classList.add('nyx-hidden');
    });

    Nyx.on('catalogue', function (msg) {
        state.categories = msg.categories || [];

        /* Keep the selection pointing at the refreshed record so the Buy button
           flips to "Owned" straight after a purchase. */
        if (state.selected) {
            var found = null;
            state.categories.forEach(function (cat) {
                cat.vehicles.forEach(function (v) { if (v.id === state.selected.id) found = v; });
            });
            state.selected = found;
        }

        renderList();
        renderBar();
    });

    Nyx.on('refused', function (msg) {
        var buy = document.getElementById('buyBtn');
        if (!buy) return;
        var original = buy.textContent;
        buy.textContent = msg.reason || 'Declined';
        setTimeout(function () { buy.textContent = original; }, 1800);
    });

    Nyx.on('testDrive', function (msg) {
        els.testDrive.classList.remove('nyx-hidden');
        els.testDrive.innerHTML = Nyx.icon('gauge') + '<span>Test drive</span><b>' +
            Nyx.esc(msg.seconds) + 's</b>';
    });

    Nyx.on('testDriveEnd', function () {
        els.testDrive.classList.add('nyx-hidden');
    });

    /* Debounced so typing does not re-render the list on every keystroke. */
    els.search.addEventListener('input', Nyx.debounce(function () {
        state.query = els.search.value;
        state.page = 0;
        renderList();
    }, 140));

    els.close.addEventListener('click', close);
    Nyx.onEscape(close);
})();
