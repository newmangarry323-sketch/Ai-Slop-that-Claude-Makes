/* ==========================================================================
   nyx_inventory — page
   --------------------------------------------------------------------------
   Two grids and an action column. Selection plus quantity drives Use, Give and
   Drop; dragging a slot onto another moves it. Every action is a request — the
   grids only ever redraw from what the server sends back, so a rejected move
   snaps back on its own without the page tracking a rollback.
   ========================================================================== */
(function () {
    'use strict';

    var shell = document.getElementById('shell');
    var panes = {
        primary: document.getElementById('primary'),
        secondary: document.getElementById('secondary'),
        middle: document.getElementById('middle')
    };

    var state = {
        open: false,
        primary: null,
        secondary: null,
        selected: null,   // { key, slot, item }
        quantity: 1
    };

    var tip = null;

    function el(html) {
        var d = document.createElement('div');
        d.innerHTML = html;
        return d.firstElementChild;
    }

    function initials(label) {
        return String(label || '?')
            .split(/\s+/).slice(0, 2)
            .map(function (w) { return w[0] || ''; })
            .join('').toUpperCase();
    }

    function viewFor(key) {
        if (state.primary && state.primary.key === key) return state.primary;
        if (state.secondary && state.secondary.key === key) return state.secondary;
        return null;
    }

    /* ----------------------------------------------------------------------
       Tooltip
       ---------------------------------------------------------------------- */

    function showTip(item, anchor) {
        hideTip();

        tip = el('<div class="tip"><b>' + Nyx.esc(item.label) + '</b>' +
            '<span>' + Nyx.esc(item.count) + '  ·  ' +
            Nyx.esc((item.weight / 1000).toFixed(2)) + ' kg</span>' +
            (item.usable ? '<span>Usable</span>' : '') + '</div>');

        document.body.appendChild(tip);

        /* Anchor above the slot, clamped into the viewport so a slot at the
           edge does not push the tooltip off screen. */
        var box = anchor.getBoundingClientRect();
        var w = tip.offsetWidth;
        var h = tip.offsetHeight;

        tip.style.left = Math.min(Math.max(box.left + box.width / 2 - w / 2, 8), window.innerWidth - w - 8) + 'px';
        tip.style.top = Math.max(box.top - h - 9, 8) + 'px';
    }

    function hideTip() {
        if (tip) { tip.remove(); tip = null; }
    }

    /* ----------------------------------------------------------------------
       Slots
       ---------------------------------------------------------------------- */

    function slotNode(view, index, item) {
        var selected = state.selected && state.selected.key === view.key && state.selected.slot === index;

        var node = el('<div class="nyx-slot' + (item ? ' is-filled' : '') +
            (selected ? ' is-selected' : '') + '">' +
            '<div class="nyx-slot-tile"></div>' +
            '<div class="nyx-slot-name">' + (item ? Nyx.esc(item.label) : '') + '</div>' +
        '</div>');

        var tile = node.querySelector('.nyx-slot-tile');

        if (item) {
            var url = item.image ? Nyx.escUrl(item.image) : '';
            tile.innerHTML =
                '<span class="nyx-slot-qty">' + Nyx.esc(item.count) + '</span>' +
                (url ? '<img src="' + url + '" alt="">' :
                    '<span class="slot-initials">' + Nyx.esc(initials(item.label)) + '</span>');

            node.draggable = true;

            node.addEventListener('dragstart', function (e) {
                /* dataTransfer is the only channel that survives a drop onto a
                   different element, so the whole source address goes in it. */
                e.dataTransfer.setData('text/plain', JSON.stringify({
                    key: view.key, slot: index, count: item.count
                }));
                e.dataTransfer.effectAllowed = 'move';
            });

            node.addEventListener('mouseenter', function () { showTip(item, node); });
            node.addEventListener('mouseleave', hideTip);

            node.addEventListener('click', function () {
                state.selected = { key: view.key, slot: index, item: item };
                state.quantity = Math.min(state.quantity, item.count) || 1;
                Nyx.post('sound', { key: 'select' });
                render();
            });

            /* Double click moves the whole stack to the other pane, which is
               the action people reach for most and the slowest to do by drag. */
            node.addEventListener('dblclick', function () {
                var other = view.key === (state.primary && state.primary.key)
                    ? state.secondary : state.primary;
                if (!other) return;

                Nyx.post('move', {
                    from: view.key, fromSlot: index,
                    to: other.key, toSlot: null,
                    count: item.count
                });
            });
        }

        node.addEventListener('dragover', function (e) {
            e.preventDefault();
            node.classList.add('is-drop-target');
        });

        node.addEventListener('dragleave', function () {
            node.classList.remove('is-drop-target');
        });

        node.addEventListener('drop', function (e) {
            e.preventDefault();
            node.classList.remove('is-drop-target');

            var payload;
            try {
                payload = JSON.parse(e.dataTransfer.getData('text/plain'));
            } catch (err) {
                return;
            }
            if (!payload || (payload.key === view.key && payload.slot === index)) return;

            Nyx.post('move', {
                from: payload.key, fromSlot: payload.slot,
                to: view.key, toSlot: index,
                count: payload.count
            });
        });

        return node;
    }

    function renderPane(node, view, icon) {
        node.textContent = '';

        if (!view) {
            node.classList.add('is-empty');
            node.appendChild(el('<div class="nyx-empty">' + Nyx.icon('box') +
                '<span>Nothing open.<br>Stand on a drop or at a stash.</span></div>'));
            return;
        }

        node.classList.remove('is-empty');

        var heavy = view.weight / view.maxWeight > 0.9;

        node.appendChild(el(
            '<div class="pane-head">' +
                '<span class="pane-icon">' + Nyx.icon(icon) + '</span>' +
                '<span class="pane-title">' + Nyx.esc(view.label) + '</span>' +
                '<span class="pane-count"><b>' + Nyx.esc(view.used) + '</b> / ' + Nyx.esc(view.slots) + '</span>' +
            '</div>'
        ));

        node.appendChild(el('<div class="pane-weight' + (heavy ? ' is-heavy' : '') + '">' +
            '<i style="width:' + Math.min(view.weight / view.maxWeight * 100, 100) + '%"></i></div>'));

        var grid = el('<div class="pane-grid nyx-scroll"><div class="nyx-slots"></div></div>');
        var slots = grid.firstElementChild;

        /* Index the items by slot once, rather than searching the list for
           every one of forty slots. */
        var bySlot = {};
        view.items.forEach(function (item) { bySlot[item.slot] = item; });

        for (var i = 1; i <= view.slots; i++) {
            slots.appendChild(slotNode(view, i, bySlot[i] || null));
        }

        node.appendChild(grid);
    }

    /* ----------------------------------------------------------------------
       Action column
       ---------------------------------------------------------------------- */

    function renderMiddle() {
        var sel = state.selected;
        var max = sel ? sel.item.count : 1;

        panes.middle.textContent = '';
        panes.middle.appendChild(el('<div class="middle-info">' + Nyx.icon('info') + '</div>'));

        var qty = el('<div class="qty"><input type="number" min="1" max="' + max +
            '" value="' + Math.min(state.quantity, max) + '"></div>');

        var input = qty.firstElementChild;
        input.addEventListener('input', function () {
            var n = parseInt(input.value, 10);
            state.quantity = (!n || n < 1) ? 1 : Math.min(n, max);
        });

        panes.middle.appendChild(qty);

        [
            { key: 'use', label: 'Use', icon: 'hand' },
            { key: 'give', label: 'Give', icon: 'gift' },
            { key: 'drop', label: 'Drop', icon: 'trash', danger: true }
        ].forEach(function (action) {
            var node = el('<button class="nyx-btn nyx-btn--stack nyx-btn--block' +
                (action.danger ? ' nyx-btn--danger' : '') + '">' +
                Nyx.icon(action.icon) + '<span>' + action.label + '</span></button>');

            /* Everything needs a selection; Use additionally only applies to
               the player's own inventory — you cannot eat out of a stash. */
            var usable = !!sel && (action.key !== 'use' ||
                (state.primary && sel.key === state.primary.key));

            if (!usable) {
                node.setAttribute('aria-disabled', 'true');
            } else {
                node.addEventListener('click', function () {
                    Nyx.post(action.key, {
                        slot: sel.slot,
                        count: Math.min(state.quantity, sel.item.count)
                    });
                    state.selected = null;
                    render();
                });
                node.addEventListener('mouseenter', function () { Nyx.post('sound', { key: 'hover' }); });
            }

            panes.middle.appendChild(node);
        });
    }

    function render() {
        renderPane(panes.primary, state.primary, 'box');
        renderPane(panes.secondary, state.secondary, 'layers');
        renderMiddle();
    }

    /* ----------------------------------------------------------------------
       Messages
       ---------------------------------------------------------------------- */

    function close() {
        if (!state.open) return;
        state.open = false;
        state.selected = null;
        hideTip();
        shell.classList.add('nyx-hidden');
        Nyx.post('close');
    }

    Nyx.on('open', function (msg) {
        state.open = true;
        state.selected = null;
        state.quantity = 1;
        if (msg.accent) Nyx.setAccent(msg.accent);
        shell.classList.remove('nyx-hidden');
    });

    Nyx.on('update', function (msg) {
        state.primary = msg.primary || null;
        state.secondary = msg.secondary || null;

        /* A selection can be invalidated by any update — the item may have been
           used, moved or taken by someone else sharing the container. */
        if (state.selected) {
            var view = viewFor(state.selected.key);
            var still = null;

            if (view) {
                view.items.forEach(function (item) {
                    if (item.slot === state.selected.slot) still = item;
                });
            }

            if (still) state.selected.item = still;
            else state.selected = null;
        }

        render();
    });

    Nyx.on('close', function () {
        state.open = false;
        hideTip();
        shell.classList.add('nyx-hidden');
    });

    /* Dropping onto the backdrop rather than a slot drops the item on the
       ground, which is what the gesture reads as. */
    shell.addEventListener('dragover', function (e) { e.preventDefault(); });

    shell.addEventListener('drop', function (e) {
        if (e.target.closest && e.target.closest('.nyx-slot')) return;
        e.preventDefault();

        var payload;
        try {
            payload = JSON.parse(e.dataTransfer.getData('text/plain'));
        } catch (err) {
            return;
        }
        if (!payload || !state.primary || payload.key !== state.primary.key) return;

        Nyx.post('drop', { slot: payload.slot, count: payload.count });
    });

    Nyx.onEscape(close);
})();
