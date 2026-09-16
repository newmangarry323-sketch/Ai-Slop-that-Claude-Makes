--[[ ==========================================================================
     Nyx — server framework bridge
     --------------------------------------------------------------------------
     Same job as the client bridge: one shape for identifiers, names and money,
     whichever framework is underneath. When there is no framework, Nyx keeps
     its own wallet in the `wallets` bucket so the dealership and the inventory
     still have something to spend.
     ========================================================================== ]]

Nyx = Nyx or {}
Nyx.Bridge = {}

local Bridge = Nyx.Bridge
local Util = Nyx.Util
local Store = Nyx.Store

local framework = 'standalone'
local core = nil

local ACCOUNTS = { cash = true, bank = true, black = true }

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
    if started('qbx_core') then return 'qbx' end
    if started('es_extended') then return 'esx' end
    if started('qb-core') then return 'qb' end
    return 'standalone'
end

CreateThread(function()
    framework = detect()

    if framework == 'esx' then
        local ok, obj = pcall(function() return exports['es_extended']:getSharedObject() end)
        core = ok and obj or nil
        if not core then
            TriggerEvent('esx:getSharedObject', function(obj2) core = obj2 end)
        end
    elseif framework == 'qb' then
        local ok, obj = pcall(function() return exports['qb-core']:GetCoreObject() end)
        core = ok and obj or nil
    end

    print(('^5[nyx]^7 server framework: ^2%s^7'):format(framework))
end)

function Bridge.GetFramework()
    return framework
end

-- ---------------------------------------------------------------------------
-- Identity
-- ---------------------------------------------------------------------------

--- A stable per-player key for the store. Prefers the framework's own
--- identifier so data survives a licence change the framework already handles;
--- otherwise falls back to the Rockstar licence, which is the most stable
--- identifier FiveM hands out.
--- @param src number
--- @return string|nil nil when the player has no usable identifier yet
function Bridge.GetIdentifier(src)
    if framework == 'esx' and core then
        local xPlayer = core.GetPlayerFromId(src)
        if xPlayer then return xPlayer.identifier end

    elseif framework == 'qb' and core then
        local player = core.Functions.GetPlayer(src)
        if player then return player.PlayerData.citizenid end

    elseif framework == 'qbx' then
        local ok, player = pcall(function() return exports.qbx_core:GetPlayer(src) end)
        if ok and player then return player.PlayerData.citizenid end
    end

    local licence = GetPlayerIdentifierByType(src, 'license')
    if licence then return licence end

    -- A player can be mid-connection with no licence resolved yet. Callers
    -- must handle nil rather than writing to a bucket key of "nil".
    return nil
end

function Bridge.GetName(src)
    if framework == 'esx' and core then
        local xPlayer = core.GetPlayerFromId(src)
        if xPlayer then
            return Util.SafeString(('%s %s'):format(xPlayer.get('firstName') or '', xPlayer.get('lastName') or ''), 48)
        end

    elseif framework == 'qb' or framework == 'qbx' then
        local player
        if framework == 'qb' and core then
            player = core.Functions.GetPlayer(src)
        else
            local ok, p = pcall(function() return exports.qbx_core:GetPlayer(src) end)
            player = ok and p or nil
        end
        if player then
            local info = player.PlayerData.charinfo or {}
            return Util.SafeString(('%s %s'):format(info.firstname or '', info.lastname or ''), 48)
        end
    end

    return Util.SafeString(GetPlayerName(src), 48)
end

-- ---------------------------------------------------------------------------
-- Standalone wallet
-- ---------------------------------------------------------------------------

local function wallet(src)
    local id = Bridge.GetIdentifier(src)
    if not id then return nil end

    local w = Store.Get('wallets', id)
    if not w then
        w = {
            cash = Nyx.Config.standaloneStartingCash or 0,
            bank = Nyx.Config.standaloneStartingBank or 0,
            black = 0
        }
        Store.Set('wallets', id, w)
    end
    return w
end

local function pushWallet(src)
    local w = wallet(src)
    if w then TriggerClientEvent('nyx:lib:wallet', src, w) end
end

Bridge.PushWallet = pushWallet

-- ---------------------------------------------------------------------------
-- Money
-- ---------------------------------------------------------------------------

--- @param account 'cash'|'bank'|'black'
--- @return number
function Bridge.GetMoney(src, account)
    account = ACCOUNTS[account] and account or 'cash'

    if framework == 'esx' and core then
        local xPlayer = core.GetPlayerFromId(src)
        if not xPlayer then return 0 end
        if account == 'cash' then return xPlayer.getMoney() end
        local acc = xPlayer.getAccount(account == 'bank' and 'bank' or 'black_money')
        return acc and acc.money or 0
    end

    if framework == 'qb' or framework == 'qbx' then
        local player
        if framework == 'qb' and core then
            player = core.Functions.GetPlayer(src)
        else
            local ok, p = pcall(function() return exports.qbx_core:GetPlayer(src) end)
            player = ok and p or nil
        end
        if not player then return 0 end

        local money = player.PlayerData.money or {}
        if account == 'cash' then return money.cash or 0 end
        if account == 'bank' then return money.bank or 0 end
        return money.crypto or money.black_money or 0
    end

    local w = wallet(src)
    return w and (w[account] or 0) or 0
end

--- @return boolean true when the money was actually added
function Bridge.AddMoney(src, account, amount)
    account = ACCOUNTS[account] and account or 'cash'
    amount = Util.PositiveInt(amount)
    if not amount then return false end

    if framework == 'esx' and core then
        local xPlayer = core.GetPlayerFromId(src)
        if not xPlayer then return false end
        if account == 'cash' then xPlayer.addMoney(amount)
        else xPlayer.addAccountMoney(account == 'bank' and 'bank' or 'black_money', amount) end
        return true
    end

    if framework == 'qb' or framework == 'qbx' then
        local player
        if framework == 'qb' and core then
            player = core.Functions.GetPlayer(src)
        else
            local ok, p = pcall(function() return exports.qbx_core:GetPlayer(src) end)
            player = ok and p or nil
        end
        if not player then return false end
        return player.Functions.AddMoney(account == 'black' and 'crypto' or account, amount, 'nyx') and true or false
    end

    local w = wallet(src)
    if not w then return false end
    w[account] = (w[account] or 0) + amount
    Store.Set('wallets', Bridge.GetIdentifier(src), w)
    pushWallet(src)
    return true
end

--- Removes money only when the player actually has it. The caller is expected
--- to treat `false` as "the purchase did not happen" — never as "probably fine".
--- @return boolean
function Bridge.RemoveMoney(src, account, amount)
    account = ACCOUNTS[account] and account or 'cash'
    amount = Util.PositiveInt(amount)
    if not amount then return false end

    if Bridge.GetMoney(src, account) < amount then return false end

    if framework == 'esx' and core then
        local xPlayer = core.GetPlayerFromId(src)
        if not xPlayer then return false end
        if account == 'cash' then xPlayer.removeMoney(amount)
        else xPlayer.removeAccountMoney(account == 'bank' and 'bank' or 'black_money', amount) end
        return true
    end

    if framework == 'qb' or framework == 'qbx' then
        local player
        if framework == 'qb' and core then
            player = core.Functions.GetPlayer(src)
        else
            local ok, p = pcall(function() return exports.qbx_core:GetPlayer(src) end)
            player = ok and p or nil
        end
        if not player then return false end
        return player.Functions.RemoveMoney(account == 'black' and 'crypto' or account, amount, 'nyx') and true or false
    end

    local w = wallet(src)
    if not w then return false end
    w[account] = (w[account] or 0) - amount
    Store.Set('wallets', Bridge.GetIdentifier(src), w)
    pushWallet(src)
    return true
end

--- Job name and grade, normalised. Used for gating menu sections by job.
function Bridge.GetJob(src)
    if framework == 'esx' and core then
        local xPlayer = core.GetPlayerFromId(src)
        if xPlayer and xPlayer.job then return xPlayer.job.name, xPlayer.job.grade or 0 end

    elseif framework == 'qb' or framework == 'qbx' then
        local player
        if framework == 'qb' and core then
            player = core.Functions.GetPlayer(src)
        else
            local ok, p = pcall(function() return exports.qbx_core:GetPlayer(src) end)
            player = ok and p or nil
        end
        if player then
            local job = player.PlayerData.job or {}
            return job.name or 'unemployed', (job.grade and job.grade.level) or 0
        end
    end

    return 'unemployed', 0
end
