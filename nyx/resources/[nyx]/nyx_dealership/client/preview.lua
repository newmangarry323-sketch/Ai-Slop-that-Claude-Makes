--[[ ==========================================================================
     nyx_dealership — showroom preview
     --------------------------------------------------------------------------
     Owns the display vehicle and the camera. Two rules keep this from leaking
     entities across the map, which is the classic dealership bug:

       1. Exactly one preview vehicle exists at a time, and swapping models
          deletes the previous one before creating the next.
       2. Everything is torn down on close, on death and on resource stop.

     The preview vehicle is created with `false, false` for networking, so it
     exists only on this client. Other players never see a ghost car spinning
     in the showroom.
     ========================================================================== ]]

local Util = Nyx.Util

local preview = nil
local cam = nil
local location = nil

--- Load a model with a bounded wait. A bad spawn name (a typo, or an add-on
--- whose resource is not started) would otherwise spin here forever.
--- @return boolean
local function requestModel(model)
    local hash = type(model) == 'number' and model or GetHashKey(model)

    if not IsModelInCdimage(hash) or not IsModelAVehicle(hash) then
        Util.Warn(('model "%s" is not a loadable vehicle — check the spawn name'):format(tostring(model)))
        return false, hash
    end

    RequestModel(hash)

    local deadline = GetGameTimer() + 10000
    while not HasModelLoaded(hash) and GetGameTimer() < deadline do Wait(0) end

    if not HasModelLoaded(hash) then
        Util.Warn(('model "%s" did not stream in within 10s'):format(tostring(model)))
        return false, hash
    end

    return true, hash
end

NyxDealer.RequestModel = requestModel

function NyxDealer.DestroyPreview()
    if preview and DoesEntityExist(preview) then
        DeleteEntity(preview)
    end
    preview = nil
end

--- Swap the showroom vehicle to `model`. Returns the stat block the page draws
--- its meters from, or nil when the model could not be loaded.
function NyxDealer.SetPreview(model)
    if not location then return nil end

    local ok, hash = requestModel(model)
    if not ok then return nil end

    NyxDealer.DestroyPreview()

    local spot = location.preview
    preview = CreateVehicle(hash, spot.x, spot.y, spot.z, spot.w, false, false)

    SetEntityInvincible(preview, true)
    SetVehicleDoorsLocked(preview, 4)          -- nobody climbs into the display car
    FreezeEntityPosition(preview, true)
    SetVehicleNumberPlateText(preview, 'NYX')
    SetVehicleDirtLevel(preview, 0.0)
    SetEntityCollision(preview, false, false)

    SetModelAsNoLongerNeeded(hash)

    return NyxDealer.StatsFor(hash)
end

--- Normalise the game's own handling figures to 0-100 so the meters are
--- comparable across classes. The divisors are chosen so the fastest base-game
--- vehicles land near 100 rather than clipping.
function NyxDealer.StatsFor(hash)
    local speed = GetVehicleModelMaxSpeed(hash) or 0
    local accel = GetVehicleModelAcceleration(hash) or 0
    local brake = GetVehicleModelMaxBraking(hash) or 0
    local grip = GetVehicleModelMaxTraction(hash) or 0

    return {
        power = Util.Clamp(Util.Round(speed / 50.0 * 100), 0, 100),
        acceleration = Util.Clamp(Util.Round(accel / 1.2 * 100), 0, 100),
        braking = Util.Clamp(Util.Round(brake / 1.2 * 100), 0, 100),
        handling = Util.Clamp(Util.Round(grip / 2.6 * 100), 0, 100)
    }
end

function NyxDealer.ApplyRespray(primary, secondary)
    if not preview or not DoesEntityExist(preview) then return false end
    SetVehicleColours(preview, primary, secondary)
    return true
end

-- ---------------------------------------------------------------------------
-- Camera
-- ---------------------------------------------------------------------------

function NyxDealer.StartCamera(loc)
    location = loc

    cam = CreateCamWithParams(
        'DEFAULT_SCRIPTED_CAMERA',
        loc.camera.x, loc.camera.y, loc.camera.z,
        0.0, 0.0, 0.0, 50.0, false, 0
    )

    PointCamAtCoord(cam, loc.preview.x, loc.preview.y, loc.preview.z + 0.4)
    SetCamActive(cam, true)
    RenderScriptCams(true, true, 600, true, true)
end

function NyxDealer.StopCamera()
    if cam then
        RenderScriptCams(false, true, 400, true, true)
        DestroyCam(cam, false)
        cam = nil
    end
    location = nil
end

--- Slow orbit around the display vehicle while the showroom is open.
function NyxDealer.SpinPreview()
    CreateThread(function()
        local heading = location and location.preview.w or 0.0
        while preview and DoesEntityExist(preview) do
            heading = (heading + 0.22) % 360.0
            SetEntityHeading(preview, heading)
            Wait(16)
        end
    end)
end

function NyxDealer.HasPreview()
    return preview ~= nil and DoesEntityExist(preview)
end

AddEventHandler('onResourceStop', function(resource)
    if resource ~= GetCurrentResourceName() then return end
    NyxDealer.DestroyPreview()
    NyxDealer.StopCamera()
end)
