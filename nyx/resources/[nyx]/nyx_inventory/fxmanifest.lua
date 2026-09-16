fx_version 'cerulean'
game 'gta5'
lua54 'yes'

name 'nyx_inventory'
author 'newmangarry323-sketch'
description 'Nyx — slot inventory with ground drops, stashes and player-to-player transfers'
version '1.0.0'

ui_page 'web/index.html'

shared_scripts {
    '@nyx_lib/shared/config.lua',
    '@nyx_lib/shared/util.lua',
    'config.lua'
}

client_scripts {
    'client/main.lua'
}

server_scripts {
    'server/inventory.lua',
    'server/main.lua'
}

files {
    'web/index.html',
    'web/style.css',
    'web/app.js',
    'web/img/*.png',
    'web/img/*.webp'
}

dependencies {
    'nyx_lib'
}
