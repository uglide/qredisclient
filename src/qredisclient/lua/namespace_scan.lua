local root_namespaces = {}
local root_keys = {}

local cursor = "0"
local ns_s = ARGV[1] or ":"
local current_ns = ARGV[2] or ""

repeat
	local result = redis.call("SCAN", cursor, "MATCH", current_ns .. "*")

	cursor = result[1]
	local keys = result[2]

	for _, key in ipairs(keys) do
		local ns = string.match(key, "^".. current_ns .."([^".. ns_s .."]+)".. ns_s .."[^".. ns_s .."]+")

		if ns ~= nil then
      if root_namespaces[ns] == nil then
         root_namespaces[ns] = 1
      else
        root_namespaces[ns] = root_namespaces[ns] + 1
      end            
    else 
      root_keys[key] = 1
		end
	end

	if cursor == "0" then
		break
	end
until true

return {cjson.encode(root_namespaces), cjson.encode(root_keys)}