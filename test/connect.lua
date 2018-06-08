local irc = require("ircclient")

local session
session = irc.new{
	connect = function(...)
		print("CONNECT:", ...)
		session:join("#roblox-bots")
		session:send("#roblox-bots", "Hello!")
	end,
	default = function(...)
		print("DEFAULT:", ...)
	end,
	numeric = function(...)
		print("NUMERIC:", ...)
	end
}
print(session)
session:connect("luaircclient", "irc.freenode.net")

irc.listen(session)
