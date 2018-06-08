# luaircclient

A Lua wrapper for libircclient, written for Lua 5.1 but should be easily portable for Lua 5.2 and above.

## Requirements

Before compiling you're probably going to want to grab a copy of libircclient.
Once you've installed libircclient, you should be OK to go.

## Installing

Installing should be really easy through luarocks:
```
luarocks install luaircclient
```

But if you feel inclined to compile it yourself, it should be compiled and installed to your lua C module directory and required like any other module.

## Usage

Once you have the module installed/compiled and running, using it is quite easy.
For the most bare-bones IRC bot, have a look at [the test directory](/test).

Looking through the code is probably also a great way to learn how to use the API.
[The IRC callbacks](https://github.com/DigiTechs/luaircclient/blob/master/src/irc.c#L22) are listed in the source.

For an API reference, check [ircclient.luadoc](doc/ircclient.luadoc). 
