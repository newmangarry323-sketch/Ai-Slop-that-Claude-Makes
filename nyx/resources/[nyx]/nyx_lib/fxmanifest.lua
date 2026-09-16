fx_version 'cerulean'
game 'gta5'
lua54 'yes'

name 'nyx_lib'
author 'newmangarry323-sketch'
description 'Nyx — shared design system, framework bridge and storage for the Nyx UI suite'
version '1.0.0'
repository 'https://github.com/newmangarry323-sketch/Ai-Slop-that-Claude-Makes'

-- nyx_lib's own NUI page is the notification stack. It never takes focus.
ui_page 'web/index.html'

shared_scripts {
    'shared/config.lua',
    'shared/util.lua'
}

client_scripts {
    'client/bridge.lua',
    'client/api.lua'
}

server_scripts {
    'server/store.lua',
    'server/bridge.lua',
    'server/api.lua'
}

-- Declared files are reachable from any resource's NUI as
-- nui://nyx_lib/<path>, which is how the whole suite shares one stylesheet.
files {
    'web/index.html',
    'web/theme.css',
    'web/nyx.js',
    'web/notify.js',
    'web/fonts/*.woff2'
}
