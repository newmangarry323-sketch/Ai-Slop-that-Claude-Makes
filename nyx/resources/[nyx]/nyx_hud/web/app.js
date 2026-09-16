/* ==========================================================================
   nyx_hud — page
   --------------------------------------------------------------------------
   Pure render target. It holds no policy: Lua decides what is on, what the
   numbers are and what the crosshair looks like, and this file draws it.
   ========================================================================== */
(function () {
    'use strict';

    var els = {
        fps: document.getElementById('fps'),
        money: document.getElementById('money'),
        status: document.getElementById('status'),
        vehicle: document.getElementById('vehicle'),
        watermark: document.getElementById('watermark'),
        voice: document.getElementById('voice'),
        crosshair: document.getElementById('crosshair'),
        announce: document.getElementById('announce')
    };

    var prefs = null;
    var last = { status: {}, player: {}, vehicle: { inVehicle: false } };
    var watermark = null;
    var playerCount = 0;

    function show(node, on) { node.classList.toggle('nyx-hidden', !on); }
    function enabled(key) { return prefs && prefs.toggles && prefs.toggles[key]; }

    /* ----------------------------------------------------------------------
       FPS
       ----------------------------------------------------------------------
       Measured in the page rather than read from Lua: the browser already has
       a frame clock, and NUI renders in step with the game, so this tracks the
       game's frame rate closely without a native call per frame.
       ---------------------------------------------------------------------- */
    (function fpsLoop() {
        var frames = 0;
        var since = performance.now();

        function tick(now) {
            frames++;
            if (now - since >= 1000) {
                els.fps.textContent = Math.round(frames * 1000 / (now - since)) + 'fps';
                frames = 0;
                since = now;
            }
            show(els.fps, enabled('fps'));
            requestAnimationFrame(tick);
        }

        requestAnimationFrame(tick);
    })();

    /* ----------------------------------------------------------------------
       Money / job panel
       ---------------------------------------------------------------------- */

    function renderMoney() {
        if (!prefs || !enabled('money')) {
            show(els.money, false);
            return;
        }

        var p = last.player;
        var pixel = prefs.moneyStyle === 4;

        els.money.className = 'hud-money ' + (pixel ? 'is-pixel' : 'is-bars');
        show(els.money, true);

        /* The third row is the accent one in both styles — it is the row the
           reference screenshots highlight, and having exactly one highlighted
           row is what stops the panel reading as a wall of numbers. */
        var rows = [
            { icon: 'cash', text: pixel ? '$ ' + Math.round(p.cash || 0) : Nyx.money(p.cash) },
            { icon: 'bank', text: pixel ? '$ ' + Math.round(p.bank || 0) : Nyx.money(p.bank) },
            { icon: 'box', text: pixel ? '$ ' + Math.round(p.black || 0) : Nyx.money(p.black), accent: true }
        ];

        if (p.job) {
            rows.push({ icon: 'briefcase', text: [p.job, p.grade].filter(Boolean).join(' — ') });
        }
        if (p.gang) {
            rows.push({ icon: 'user', text: [p.gang, p.gangGrade].filter(Boolean).join(' — ') });
        }

        els.money.innerHTML = rows.map(function (row) {
            return '<div class="m-row' + (row.accent ? ' is-accent' : '') + '">' +
                '<span>' + Nyx.esc(pixel ? String(row.text).toUpperCase() : row.text) + '</span>' +
                Nyx.icon(row.icon, 'm-icon') +
            '</div>';
        }).join('');
    }

    /* ----------------------------------------------------------------------
       Status cluster
       ---------------------------------------------------------------------- */

    function renderStatus() {
        if (!prefs || !enabled('health')) {
            show(els.status, false);
            return;
        }

        var s = last.status;
        var health = s.health || 0;
        var armour = s.armour || 0;

        els.status.className = 'hud-status ' + (prefs.style === 2 ? 'is-rings' : 'is-pill') +
            (health > 0 && health <= 20 ? ' is-critical' : '');
        show(els.status, true);

        function chip(icon, value, extra) {
            return '<div class="s-chip ' + extra + (value > 0 ? ' is-on' : '') + '">' +
                '<div class="s-fill" style="height:' + Math.max(0, Math.min(100, value)) + '%"></div>' +
                Nyx.icon(icon) +
            '</div>';
        }

        els.status.innerHTML = '<div class="s-wrap">' +
            chip('heart', health, 'is-health') +
            chip(s.talking ? 'mic' : 'micOff', s.talking ? 100 : 0, 'is-voice') +
            chip('shield', armour, 'is-armour') +
        '</div>';

        show(els.voice, !!s.voiceRange && enabled('health'));
        if (s.voiceRange) els.voice.textContent = String(s.voiceRange) + ' [Range]';
    }

    /* ----------------------------------------------------------------------
       Vehicle
       ---------------------------------------------------------------------- */

    function renderVehicle() {
        var v = last.vehicle;

        if (!prefs || !enabled('speedometer') || !v.inVehicle) {
            show(els.vehicle, false);
            return;
        }

        show(els.vehicle, true);

        function bar(cls, value, lowAt) {
            var low = lowAt !== undefined && value <= lowAt;
            return '<div class="v-bar ' + cls + (low ? ' is-low' : '') + '">' +
                '<i style="width:' + Math.max(0, Math.min(100, value)) + '%"></i></div>';
        }

        els.vehicle.innerHTML =
            '<div class="v-speed">' +
                '<span class="v-number">' + Nyx.esc(v.speed || 0) + '</span>' +
                '<span class="v-units">' + Nyx.esc(v.units === 'kmh' ? 'km/h' : 'mph') + '</span>' +
            '</div>' +
            '<div class="v-bars">' +
                bar('is-rpm', (v.rpm || 0) * 100) +
                bar('is-fuel', v.fuel || 0, 20) +
                bar('is-engine', v.engine || 0, 30) +
            '</div>';
    }

    /* ----------------------------------------------------------------------
       Watermark
       ---------------------------------------------------------------------- */

    function renderWatermark() {
        if (!prefs || !enabled('watermark') || !watermark || !watermark.text) {
            show(els.watermark, false);
            return;
        }

        show(els.watermark, true);

        /* Split on the last slash so "discord.gg/name" renders with the name in
           the accent colour, the way the reference does. */
        var text = String(watermark.text);
        var cut = text.lastIndexOf('/');
        var head = cut === -1 ? text : text.slice(0, cut + 1);
        var tail = cut === -1 ? '' : text.slice(cut + 1);

        els.watermark.innerHTML =
            '<span>' + Nyx.esc(head) + '<b>' + Nyx.esc(tail) + '</b></span>' +
            (watermark.showPlayerCount ? '<span class="w-count">' + Nyx.esc(playerCount) + '</span>' : '');
    }

    /* ----------------------------------------------------------------------
       Crosshair
       ---------------------------------------------------------------------- */

    var spread = 0;

    function renderCrosshair() {
        if (!prefs || !enabled('crosshair')) {
            show(els.crosshair, false);
            return;
        }

        var x = prefs.crosshair || {};

        if (x.hideInVehicle && last.vehicle.inVehicle) {
            show(els.crosshair, false);
            return;
        }

        show(els.crosshair, true);

        /* Dynamic spread widens the gap while the vehicle or player is moving.
           `spread` is eased in the animation loop below so it never snaps. */
        var drawn = x;
        if (x.dynamic && spread > 0.01) {
            drawn = {};
            Object.keys(x).forEach(function (k) { drawn[k] = x[k]; });
            drawn.gap = (Number(x.gap) || 0) + spread * 12;
        }

        Nyx.crosshair(els.crosshair, drawn);
    }

    (function spreadLoop() {
        function tick() {
            if (prefs && prefs.crosshair && prefs.crosshair.dynamic) {
                /* Speed is the only movement signal the page has; it is enough
                   for a visible, honest spread without another native call. */
                var target = Math.min((last.vehicle.speed || 0) / 90, 1);
                var next = spread + (target - spread) * 0.12;

                if (Math.abs(next - spread) > 0.004) {
                    spread = next;
                    renderCrosshair();
                }
            }
            requestAnimationFrame(tick);
        }
        requestAnimationFrame(tick);
    })();

    /* ----------------------------------------------------------------------
       Announcements
       ---------------------------------------------------------------------- */

    var announceTimer = null;

    function announce(msg) {
        clearTimeout(announceTimer);

        els.announce.className = 'hud-announce';
        els.announce.innerHTML =
            '<div class="a-title">' + Nyx.esc(msg.title || 'Announcement') + '</div>' +
            '<div class="a-text">' + Nyx.esc(msg.text || '') + '</div>';

        announceTimer = setTimeout(function () {
            els.announce.classList.add('is-leaving');
            setTimeout(function () { show(els.announce, false); }, 240);
        }, Number(msg.duration) || 7000);
    }

    /* ----------------------------------------------------------------------
       Render everything
       ---------------------------------------------------------------------- */

    function renderAll() {
        show(els.fps, enabled('fps'));
        renderMoney();
        renderStatus();
        renderVehicle();
        renderWatermark();
        renderCrosshair();
    }

    /* ----------------------------------------------------------------------
       Messages from Lua
       ---------------------------------------------------------------------- */

    Nyx.on('boot', function (msg) {
        watermark = msg.watermark || null;
        if (msg.accent) Nyx.setAccent(msg.accent);
        renderAll();
    });

    Nyx.on('prefs', function (msg) {
        prefs = msg.prefs;
        renderAll();
    });

    Nyx.on('status', function (msg) {
        last.status = msg;
        renderStatus();
    });

    Nyx.on('player', function (msg) {
        last.player = msg;
        renderMoney();
    });

    Nyx.on('vehicle', function (msg) {
        var wasIn = last.vehicle.inVehicle;
        last.vehicle = msg;
        renderVehicle();
        /* Getting in or out can change whether the crosshair should show. */
        if (wasIn !== msg.inVehicle) renderCrosshair();
    });

    Nyx.on('playerCount', function (msg) {
        playerCount = msg.count || 0;
        renderWatermark();
    });

    Nyx.on('announce', announce);
})();
