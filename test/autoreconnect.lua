local irc = require("ircclient")

local one
one = irc.new{
	connect = function(...)
		print(...)
		one:join("#roblox-bots")
		one:send("#roblox-bots", "hi")
	end,
	default = function(...)
		print(one)
		print(...)
	end,
	numeric = function(...)
		print(...)
	end
}
one:set_option("auto_reconnect", true) -- when we lose connection, automatically reconnect
one:connect("luaircclient", "irc.freenode.net")

while true do
	one:think() -- think for the one session
end
