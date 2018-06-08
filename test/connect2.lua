local irc = require("ircclient")

local one
one = irc.new{
	connect = function(...)
		print(...)
		one:join("#roblox-bots")
		one:send("#roblox-bots", "Hi from one!")
	end,
	default = print,
	numeric = print
}

local two
two = irc.new {
	connect = function(...)
		print(...)
		two:join("#roblox-bots")
		two:send("#roblox-bots", "Hi from two!")
	end,
	default = print,
	numeric = print
}

print(one, two)

one:connect("luaircclient1", "irc.freenode.net")
two:connect("luaircclient2", "irc.freenode.net")

print(one, two)

while true do
	irc.think({one, two})
end
