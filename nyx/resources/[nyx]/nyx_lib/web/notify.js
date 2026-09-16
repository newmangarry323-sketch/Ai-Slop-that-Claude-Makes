/* Nyx notification stack — driven from Lua by exports['nyx_lib']:Notify{...} */
(function () {
    'use strict';

    var stack = document.getElementById('stack');
    var MAX_ON_SCREEN = 5;

    var ICONS = {
        success: 'check',
        error: 'alert',
        warn: 'alert',
        info: 'info'
    };

    function dismiss(el) {
        if (el.dataset.leaving) return;
        el.dataset.leaving = '1';
        el.classList.add('is-leaving');
        /* Matches --nyx-mid; the element is gone either way once it fires. */
        setTimeout(function () { el.remove(); }, 260);
    }

    Nyx.on('notify', function (msg) {
        var kind = ICONS[msg.type] ? msg.type : 'default';
        var el = document.createElement('div');

        el.className = 'nyx-notify' + (kind === 'default' ? '' : ' nyx-notify--' + kind);

        /* Everything below is escaped: notification text routinely carries
           player names and item labels that originate off the server. */
        el.innerHTML =
            Nyx.icon(ICONS[kind] || 'info', 'nyx-notify-icon') +
            '<div class="nyx-notify-body">' +
                (msg.title ? '<div class="nyx-notify-title">' + Nyx.esc(msg.title) + '</div>' : '') +
                (msg.description ? '<div class="nyx-notify-text">' + Nyx.esc(msg.description) + '</div>' : '') +
            '</div>';

        stack.appendChild(el);

        /* Oldest out first once the stack is full, so a burst of notifications
           can never push the screen full of cards. */
        while (stack.children.length > MAX_ON_SCREEN) dismiss(stack.firstElementChild);

        var ms = Math.min(Math.max(Number(msg.duration) || 4000, 900), 20000);
        setTimeout(function () { dismiss(el); }, ms);
    });

    Nyx.on('notify:clear', function () {
        while (stack.firstElementChild) stack.firstElementChild.remove();
    });
})();
