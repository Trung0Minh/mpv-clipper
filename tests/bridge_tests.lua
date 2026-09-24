local real_mp = require 'mp'
local utils = require 'mp.utils'

local function run()
    local launch, callback, command, last_message
    local properties = { pause = false, ['working-directory'] = '/tmp',
        ['time-pos'] = 12.5, ['track-list'] = {
            { type = 'audio', id = 2, selected = true, lang = 'jpn' }
        } }
    local fake = {
        msg = { error = function() end },
        osd_message = function(message) last_message = message end,
        get_property = function(key, default) return properties[key] or default end,
        get_property_number = function(key, default) return properties[key] or default end,
        get_property_native = function(key, default)
            if properties[key] ~= nil then return properties[key] end
            return default
        end,
        set_property_native = function(key, value) properties[key] = value end,
        command_native = real_mp.command_native,
        command_native_async = function(args, done)
            if #args.args == 1 then return 2 end
            command, callback = args, done
            return 1
        end,
        add_key_binding = function(_, name, fn)
            assert(name == 'open-clipper'); launch = fn
        end,
        register_event = function() end
    }
    package.loaded['mp'] = fake
    package.loaded['mp.options'] = { read_options = function() end }
    dofile('mpv/clipper.lua')
    launch()
    assert(not command and last_message:find('local video'))
    properties.path = 'https://example.com/video'
    launch()
    assert(not command and last_message:find('Only local'))
    local temp = os.tmpname()
    os.remove(temp)
    local source = temp .. " clip's [caf\195\169].mkv"
    local source_file = assert(io.open(source, 'wb'))
    source_file:close()
    properties.path = source
    for _, originally_paused in ipairs({ false, true }) do
        properties.pause = originally_paused
        launch()
        assert(properties.pause == true)
        assert(command.args[2] == '--launch-stdin')
        assert(command.args[3] == '--wait-for-close')
        local payload = assert(utils.parse_json(command.stdin_data))
        assert(payload.source == source and payload.schemaVersion == 1)
        assert(payload.timePos == 12.5 and payload.originalPaused == originally_paused)
        assert(payload.activeAudio.id == 2)
        launch()
        assert(command.args[3] == '--wait-for-close' and properties.pause)
        callback(true, { status = 0 })
        assert(properties.pause == originally_paused)
    end
    local real_file_info = utils.file_info
    for _, windows_path in ipairs({ 'C:\\Videos\\clip.mkv', 'C:/Videos/clip.mkv', '\\\\server\\share\\clip.mkv' }) do
        utils.file_info = function(path)
            if path == windows_path then return { is_file = true } end
            return real_file_info(path)
        end
        properties.path = windows_path
        properties['track-list'] = {{ type = 'sub', selected = true, ['external-filename'] = 'D:\\Subs\\clip.ass' }}
        launch()
        local payload = assert(utils.parse_json(command.stdin_data))
        assert(payload.source == windows_path)
        assert(payload.activeSubtitle.externalFilename == 'D:\\Subs\\clip.ass')
        callback(true, { status = 0 })
    end
    utils.file_info = real_file_info
    properties.path = source
    properties.pause = false
    launch()
    callback(false, { status = -1 })
    assert(not properties.pause and last_message:find('configured executable'))
    fake.command_native_async = function() error('launch failed') end
    launch()
    assert(not properties.pause and last_message:find('configured executable'))
    os.remove(source)
end

local ok, err = pcall(run)
if not ok then real_mp.msg.error(tostring(err)) end
real_mp.commandv('quit', ok and '0' or '1')
