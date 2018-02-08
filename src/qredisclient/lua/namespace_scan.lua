local root_namespaces = {}
local root_keys = {}

local cursor = "0"
local ns_s = ARGV[1] or ":"
local filter = ARGV[2] or "*"
local local_key_part_index = string.find(filter, ns_s .. "[^" .. ns_s .. "]*$")

if local_key_part_index == nil then
    local_key_part_index = 1
else
    local_key_part_index = local_key_part_index + 1
end

repeat
    local result = redis.call("SCAN", cursor, "MATCH", filter .. "*")

    cursor = result[1]
    local keys = result[2]

    for _, key in ipairs(keys) do
        local local_key_part = string.sub(key, local_key_part_index)
        local ns = string.match(local_key_part, "^([^" .. ns_s .. "]*)" .. ns_s .. "[^" .. ns_s .. "]*")

        if ns == nil then
            root_keys[key] = 1
        else
            local full_ns = string.sub(key, 0, local_key_part_index - 1) .. ns
            if root_namespaces[full_ns] == nil then
                root_namespaces[full_ns] = 1
            else
                root_namespaces[full_ns] = root_namespaces[full_ns] + 1
            end
        end
    end    
until cursor == "0"

return { cjson.encode(root_namespaces), cjson.encode(root_keys) }
