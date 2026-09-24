local mp = require 'mp'
local utils = require 'mp.utils'
local options = { executable = 'mpv-clipper', hotkey = 'Ctrl+Shift+x' }
require('mp.options').read_options(options, 'clipper')

local active = false

local function fail(message)
    mp.msg.error(message)
    mp.osd_message('Clipper: ' .. message, 5)
end

local function absolute(path)
    return path:sub(1, 1) == '/' or path:sub(1, 1) == '\\' or path:match('^%a:[/\\]')
end

local function selected_track(kind)
    for _, track in ipairs(mp.get_property_native('track-list', {})) do
        if track.type == kind and track.selected then
            local external = track['external-filename']
            if external and not absolute(external) then
                external = utils.join_path(mp.get_property('working-directory'), external)
            end
            return {
                id = track.id, lang = track.lang, title = track.title,
                codec = track.codec, external = track.external or false,
                externalFilename = external,
                sourceIndex = track['ff-index']
            }
        end
    end
end

local function launch()
    if active then
        mp.command_native_async({ name = 'subprocess', playback_only = false,
            args = { options.executable } }, function() end)
        return
    end
    local source = mp.get_property('path')
    if not source or source == '' then
        fail('Open a local video first.')
        return
    end
    if source:match('^%a[%w+.-]*://') or source == '-' then
        fail('Only local files are supported.')
        return
    end
    if not absolute(source) then
        source = utils.join_path(mp.get_property('working-directory'), source)
    end
    local info = utils.file_info(source)
    if not info or not info.is_file then
        fail('The source file is unavailable.')
        return
    end
    local paused = mp.get_property_native('pause', false)
    local json = utils.format_json({
        schemaVersion = 1, source = source,
        mediaTitle = mp.get_property('media-title', ''),
        timePos = mp.get_property_number('time-pos', 0),
        originalPaused = paused,
        activeAudio = selected_track('audio'),
        activeSubtitle = mp.get_property_native('sub-visibility', true) and selected_track('sub') or nil
    })
    if not json then fail('Could not serialize playback context.'); return end

    -- Pass JSON through stdin: no shell, temporary file, or command-line size limit.
    active = true
    mp.set_property_native('pause', true)
    local function finished(success, result)
        active = false
        mp.set_property_native('pause', paused)
        if not success or not result or result.status ~= 0 then
            fail('Could not run Clipper. Check the configured executable.')
        end
    end
    local ok, request = pcall(mp.command_native_async, {
        name = 'subprocess', playback_only = false,
        stdin_data = json,
        args = { options.executable, '--launch-stdin', '--wait-for-close' }
    }, finished)
    if not ok or not request then finished(false) end
end

mp.add_key_binding(options.hotkey, 'open-clipper', launch)
