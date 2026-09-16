--[[ ==========================================================================
     Nyx — client framework bridge
     --------------------------------------------------------------------------
     Detects ESX, QBCore, Qbox or nothing at all, and flattens whichever one it
     finds into a single shape the rest of the suite reads. Nothing outside this
     file knows which framework is running.

     Normalised player data:
         { name, job, jobLabel, grade, gradeLabel,
           gang, gangLabel, gangGrade, gangGradeLabel,
           cash, bank, black, loaded }
     ========================================================================== ]]

Nyx = Nyx or {}
Nyx.Bridge = {}

local Bridge = Nyx.Bridge
local Util = Nyx.Util

local framework = 'standalone'
local core = nil
local loaded = false

local data = {
    name = 'Player',
    job = 'unemployed',  jobLabel = 'Unemployed',  grade = 0, gradeLabel = '',
    gang = 'none',       gangLabel = '',           gangGrade = 0, gangGradeLabel = '',
    cash = 0, bank = 0, black = 0,
    loaded = false
}

-- ---------------------------------------------------------------------------
-- Detection
-- ---------------------------------------------------------------------------

local function started(name)
    local state = GetResourceState(name)
    return state == 'started' or state == 'starting'
end

local function detect()
    local pinned = Nyx.Config and Nyx.Config.framework or 'auto'
    if pinned ~= 'auto' then return pinned end

    -- Qbox first: it also ships a qb-core shim, so checking qb-core first would
    -- misidentify every Qbox server as legacy QBCore.
    if started('qbx_core') then return 'qbx' end
    if started('es_extended') then return 'esx' end
    if started('qb-core') then return 'qb' end
    return 'standalone'
end

local function resolveCore()
    if framework == 'esx' then
        -- 1.9+ exposes an export. Older builds only answer the event, so try
        -- the export first and fall back rather than assuming either.
        local ok, obj = pcall(function() return exports['es_extended']:getSharedObject() end)
        if ok and obj then return obj end

        local fallback
        TriggerEvent('esx:getSharedObject', function(obj2) fallback = obj2 end)
        return fallback
    end

    if framework == 'qb' then
        local ok, obj = pcall(function() return exports['qb-core']:GetCoreObject() end)
        if ok then return obj end
    end

    -- Qbox is export-only; there is no core object to hold.
    return nil
end

-- ---------------------------------------------------------------------------
-- Normalisation
-- ---------------------------------------------------------------------------

local function applyEsx(xPlayer)
    if not xPlayer then return end

    local job = xPlayer.job or {}
    data.job = job.name or 'unemployed'
    data.jobLabel = job.label or data.job
    data.grade = job.grade or 0
    data.gradeLabel = job.grade_label or ''

    data.cash = xPlayer.getMoney and xPlayer.getMoney() or (xPlayer.money or 0)

    for _, account in ipairs(xPlayer.accounts or {}) do
        if account.name == 'bank' then data.bank = account.money or 0 end
        if account.name == 'black_money' then data.black = account.money or 0 end
    end

    local name = ('%s %s'):format(xPlayer.firstName or '', xPlayer.lastName or '')
    data.name = Util.SafeString(name, 48)
    if data.name == '' then data.name = GetPlayerName(PlayerId()) end
end

local function applyQb(pd)
    if not pd then return end

    local job = pd.job or {}
    data.job = job.name or 'unemployed'
    data.jobLabel = job.label or data.job
    data.grade = (job.grade and job.grade.level) or 0
    data.gradeLabel = (job.grade and job.grade.name) or ''

    local gang = pd.gang or {}
    data.gang = gang.name or 'none'
    data.gangLabel = gang.label or ''
    data.gangGrade = (gang.grade and gang.grade.level) or 0
    data.gangGradeLabel = (gang.grade and gang.grade.name) or ''

    local money = pd.money or {}
    data.cash = money.cash or 0
    data.bank = money.bank or 0
    data.black = money.crypto or money.black_money or 0

    local charinfo = pd.charinfo or {}
    local name = ('%s %s'):format(charinfo.firstname or '', charinfo.lastname or '')
    data.name = Util.SafeString(name, 48)
    if data.name == '' then data.name = GetPlayerName(PlayerId()) end
end

--- Pull the whole player record from whichever framework is live.
function Bridge.Refresh()
    if framework == 'esx' then
        if not core then core = resolveCore() end
        if core and core.GetPlayerData then applyEsx(core.GetPlayerData()) end

    elseif framework == 'qb' then
        if not core then core = resolveCore() end
        if core and core.Functions and core.Functions.GetPlayerData then
            applyQb(core.Functions.GetPlayerData())
        end

    elseif framework == 'qbx' then
        local ok, pd = pcall(function() return exports.qbx_core:GetPlayerData() end)
        if ok then applyQb(pd) end

    else
        -- Standalone: the server owns the wallet, and pushes it to us.
        data.name = GetPlayerName(PlayerId())
    end

    data.loaded = loaded
    return data
end

--- @return table a copy, so callers cannot edit the cached record
function Bridge.GetPlayerData()
    return Util.Copy(data)
end

function Bridge.GetFramework()
    return framework
end

function Bridge.IsLoaded()
    return loaded
end

-- ---------------------------------------------------------------------------
-- Framework events
-- ---------------------------------------------------------------------------

--- Broadcast to every Nyx resource so HUDs and menus can re-render.
local function announce()
    TriggerEvent('nyx:playerDataChanged', Util.Copy(data))
end

RegisterNetEvent('esx:playerLoaded', function(xPlayer)
    loaded = true
    applyEsx(xPlayer)
    data.loaded = true
    announce()
end)

RegisterNetEvent('esx:setJob', function(job)
    data.job = job.name or data.job
    data.jobLabel = job.label or data.jobLabel
    data.grade = job.grade or 0
    data.gradeLabel = job.grade_label or ''
    announce()
end)

RegisterNetEvent('esx:setAccountMoney', function(account)
    if not account then return end
    if account.name == 'bank' then data.bank = account.money or 0 end
    if account.name == 'money' then data.cash = account.money or 0 end
    if account.name == 'black_money' then data.black = account.money or 0 end
    announce()
end)

RegisterNetEvent('QBCore:Client:OnPlayerLoaded', function()
    loaded = true
    Bridge.Refresh()
    announce()
end)

-- QBCore and Qbox both fire this on every mutation of the player record, so it
-- covers money, job and gang in one handler.
RegisterNetEvent('QBCore:Player:SetPlayerData', function(pd)
    applyQb(pd)
    announce()
end)

RegisterNetEvent('QBCore:Client:OnJobUpdate', function(job)
    applyQb({ job = job })
    announce()
end)

RegisterNetEvent('QBCore:Client:OnGangUpdate', function(gang)
    applyQb({ gang = gang })
    announce()
end)

-- Standalone wallet pushes from nyx_lib's server side.
RegisterNetEvent('nyx:lib:wallet', function(wallet)
    if framework ~= 'standalone' then return end
    data.cash = wallet.cash or 0
    data.bank = wallet.bank or 0
    data.black = wallet.black or 0
    loaded = true
    data.loaded = true
    announce()
end)

-- ---------------------------------------------------------------------------
-- Boot
-- ---------------------------------------------------------------------------

CreateThread(function()
    framework = detect()

    if framework ~= 'standalone' then
        core = resolveCore()

        -- Cores can take a moment to answer right after a resource restart.
        local tries = 0
        while not core and framework ~= 'qbx' and tries < 50 do
            Wait(100)
            core = resolveCore()
            tries = tries + 1
        end

        if not core and framework ~= 'qbx' then
            Util.Warn(('detected "%s" but could not reach its core object; falling back to standalone'):format(framework))
            framework = 'standalone'
        end
    end

    print(('^5[nyx]^7 framework: ^2%s^7'):format(framework))

    -- A resource restart mid-session means the player is already spawned, so
    -- treat an existing ped as "loaded" instead of waiting for a load event
    -- that already fired.
    if framework ~= 'standalone' and DoesEntityExist(PlayerPedId()) then
        loaded = true
    end

    Bridge.Refresh()
    announce()

    TriggerServerEvent('nyx:lib:clientReady')
end)
