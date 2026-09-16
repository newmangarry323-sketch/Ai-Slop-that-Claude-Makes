fx_version 'cerulean'
game 'gta5'
lua54 'yes'

name 'nyx_dealership'
author 'newmangarry323-sketch'
description 'Nyx — vehicle dealership with live preview, test drives and respray'
version '1.0.0'

ui_page 'web/index.html'

shared_scripts {
    '@nyx_lib/shared/config.lua',
    '@nyx_lib/shared/util.lua',
    'config.lua'
}

client_scripts {
    'client/preview.lua',
    'client/main.lua'
}

server_scripts {
    'server/main.lua'
}

files {
    'web/index.html',
    'web/style.css',
    'web/app.js'
}

dependencies {
    'nyx_lib'
}
