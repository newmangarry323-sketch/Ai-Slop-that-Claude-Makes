fx_version 'cerulean'
game 'gta5'
lua54 'yes'

name 'nyx_menu'
author 'newmangarry323-sketch'
description 'Nyx — locations, weapons and interface options menu'
version '1.0.0'

ui_page 'web/index.html'

shared_scripts {
    '@nyx_lib/shared/config.lua',   -- pulls nyx_lib's globals into this state
    '@nyx_lib/shared/util.lua',
    'config.lua'
}

client_scripts {
    'client/teleport.lua',
    'client/stats.lua',
    'client/main.lua'
}

server_scripts {
    'server/main.lua'
}

files {
    'web/index.html',
    'web/style.css',
    'web/app.js',
    'web/img/*.png',
    'web/img/*.jpg',
    'web/img/*.webp'
}

dependencies {
    'nyx_lib'
}
