#include "irc.h"
#include <sys/select.h>

// Assumes table is at top of stack
static void check_callbacks(lua_State* L, int required, const char* key) {
	lua_pushstring(L, key);
	lua_gettable(L, -2); // callbacks[key]

	if (!lua_isfunction(L, -1)) {
		if (required) {
			luaL_error(L, "callbacks table missing required key for `%s`", key);
		} else {
			if (!lua_isnil(L, -1)) {
				luaL_error(L, "callbacks table invalid type for key `%s` (expected function, got %s)", key, lua_typename(L, lua_type(L, -1)));
			}
		}
	}
	lua_pop(L, 1);  // clear up
}

// == IRC CALLBACKS ==

#define NUM_EVTS 17
static const char* irc_callbacks[NUM_EVTS] = {
	"quit",
	"nick",
	"join",
	"part",
	"mode",
	"umode",
	"topic",
	"kick",
	"channel",
	"privmsg",
	"notice",
	"channel_notice",
	"invite",
	"ctcp_req", // CTCP request (i.e. /ctcp time <user>) 
	"ctcp_rep", // CTCP response (i.e. the response from /ctcp time <user>)
	"ctcp_action", // CTCP action (i.e. /me does an action)
	"default" // event_unknown (non-RFC event, also fired on any event)
	// TODO: DCC callbacks
};

static const char* irc_options[3] = {
	"debug", // LIBIRC_OPTION_DEBUG
	"ssl_no_verify", // LIBIRC_OPTION_SSL_NO_VERIFY
	"auto_reconnect" // auto reconnect
};


// ==MODULE FUNCTIONS==

int ircclient_new(lua_State* L) {
	// ircclient.new(table callbacks)
	luaL_checktype(L, 1, LUA_TTABLE);
	
	// Check callbacks table
	check_callbacks(L, 1, "connect");
	check_callbacks(L, 1, "numeric");

	int tmp;
	for (tmp = 0; tmp < NUM_EVTS; tmp++) {
		check_callbacks(L, 0, irc_callbacks[tmp]);
	}

	irc_session_t* sess = new_lua_irc_session(L);

	if (sess == NULL)
		return luaL_error(L, "couldn't create session");

	luaL_getmetatable(L, "luaircclient_session"); // ensure it has our metatable
	lua_setmetatable(L, -2);
	
	return 1; // irc session is at the top of the stack
}


// global irc.listen(session) for using irc_run - Only supports one session :(
int ircclient_listen(lua_State* L) {
	lua_irc_session* sess = (lua_irc_session*)luaL_checkudata(L, 1, "luaircclient_session");
	if (irc_run(sess->session)) {
		const char* error = irc_strerror(irc_errno(sess->session));
		return luaL_error(L, error);
	}
	return 0;
}

// global irc.think({sessions}, timeout = 0.25) for using a select()-based loop on multiple sessions
int ircclient_think(lua_State* L) {
	luaL_checktype(L, 1, LUA_TTABLE);
	int timeout = (int)(luaL_optnumber(L, 2, 0.25) * 1000000);

	struct timeval tv;
	fd_set in_set, out_set;
	int maxfd = 0;

	tv.tv_usec = timeout;

	FD_ZERO(&in_set);
	FD_ZERO(&out_set);

	int length = lua_objlen(L, 1);
	int i;

	for (i = 0; i < length; i++) {
		lua_rawgeti(L, 1, i+1);

		lua_irc_session* sess = (lua_irc_session*) luaL_checkudata(L, -1, "luaircclient_session");

		if (!irc_is_connected(sess->session) && sess->autoreconnect) {
			if (irc_connect(sess->session, sess->host, sess->port, 0, sess->nickname, sess->username, sess->realname)) {
				const char* error = irc_strerror(irc_errno(sess->session));
				return luaL_error(L, error);
			}
		}

		irc_add_select_descriptors(sess->session, &in_set, &out_set, &maxfd);
	}

	if (select(maxfd+1, &in_set, &out_set, 0, &tv) < 0) {
		// Failed to select. TODO: handle this
	}

	for (i = 0; i < length; i++) {
		lua_rawgeti(L, 1, i+1);

		lua_irc_session* sess = (lua_irc_session*) luaL_checkudata(L, -1, "luaircclient_session");

		if (irc_process_select_descriptors(sess->session, &in_set, &out_set)) {
			if (!irc_is_connected(sess->session) && sess->autoreconnect) {
				if (irc_connect(sess->session, sess->host, sess->port, 0, sess->nickname, sess->username, sess->realname)) {
					const char* error = irc_strerror(irc_errno(sess->session));
					return luaL_error(L, error);
				}
			}
		}
	}

	return 0;
}

// ==METAMETHODS==

int ircmeta_gc(lua_State* L) {
	// Check we are actually a lua_irc_session
	lua_irc_session* sess = (lua_irc_session*)luaL_checkudata(L, 1, "luaircclient_session");
	// Destroy us
	if (irc_is_connected(sess->session)) {
		irc_disconnect(sess->session);
	}

	lua_irc_session_destroy(sess);
	return 0;
}

int ircmeta_tostring(lua_State* L) {
	// Check we are actually a lua_irc_session
	lua_irc_session* sess = (lua_irc_session*)luaL_checkudata(L, 1, "luaircclient_session");
	if (irc_is_connected(sess->session))
		lua_pushfstring(L, "%s <%s:%d>", sess->nickname, sess->host, sess->port);
	else
		lua_pushliteral(L, "unconnected <*:*>");

	return 1;
}

// ==METHODS==
// session:connect(string nickname, string hostname, int port = 6667, string username = "luaircclient", string realname = "lua rocks")
int ircsess_connect(lua_State* L) {
	lua_irc_session* sess = (lua_irc_session*)luaL_checkudata(L, 1, "luaircclient_session");

	const char* nickname = luaL_checkstring(L, 2);
	const char* host = luaL_checkstring(L, 3);
	int port = luaL_optint(L, 4, 6667);
	const char* username = luaL_optstring(L, 5, "luaircclient");
	const char* realname = luaL_optstring(L, 6, "lua rocks");

	if (irc_connect(sess->session, host, port, 0, nickname, username, realname)) {
		const char* error = irc_strerror(irc_errno(sess->session));
		return luaL_error(L, error);
	} else { // set properties
		sess->nickname = nickname;
		sess->host = host;
		sess->port = port;
		sess->username = username;
		sess->realname = realname;
	}
	return 0;
} // TODO: connect6
// session:quit(string reason = "bye")
int ircsess_quit(lua_State* L) {
	lua_irc_session* sess = (lua_irc_session*)luaL_checkudata(L, 1, "luaircclient_session");
	sess->autoreconnect = 0; // ensure we don't try to reconnect
	const char* message = luaL_optstring(L, 2, "bye");

	irc_cmd_quit(sess->session, message);

	return 0;
}
// session:join(string channel, string secretkey = "")
int ircsess_join(lua_State* L) {
	lua_irc_session* sess = (lua_irc_session*)luaL_checkudata(L, 1, "luaircclient_session");

	const char* channel = luaL_checkstring(L, 2);
	const char* key = luaL_optstring(L, 3, NULL);

	if (irc_cmd_join(sess->session, channel, key)) {
		const char* error = irc_strerror(irc_errno(sess->session));
		return luaL_error(L, error);
	}
	return 0;
}
// session:part(string channel)
int ircsess_part(lua_State* L) {
	lua_irc_session* sess = (lua_irc_session*)luaL_checkudata(L, 1, "luaircclient_session");

	const char* channel = luaL_checkstring(L, 2);

	if (irc_cmd_part(sess->session, channel)) {
		const char* error = irc_strerror(irc_errno(sess->session));
		return luaL_error(L, error);
	}
	return 0;
}
// session:invite(string user, string channel)
int ircsess_invite(lua_State* L) {
	lua_irc_session* sess = (lua_irc_session*)luaL_checkudata(L, 1, "luaircclient_session");

	const char* user = luaL_checkstring(L, 2);
	const char* channel = luaL_checkstring(L, 3);

	if (irc_cmd_invite(sess->session, user, channel)) {
		const char* error = irc_strerror(irc_errno(sess->session));
		return luaL_error(L, error);
	}
	return 0;
}
// session:names(string channel)
int ircsess_names(lua_State* L) {
	lua_irc_session* sess = (lua_irc_session*)luaL_checkudata(L, 1, "luaircclient_session");

	const char* channel = luaL_checkstring(L, 2);

	if (irc_cmd_names(sess->session, channel)) {
		const char* error = irc_strerror(irc_errno(sess->session));
		return luaL_error(L, error);
	}
	return 0;
}// TODO: list channels? seems unneccesary (easy to make spam bots)
// session:send(string destination, string message)
int ircsess_send(lua_State* L) {
	lua_irc_session* sess = (lua_irc_session*)luaL_checkudata(L, 1, "luaircclient_session");

	const char* destination = luaL_checkstring(L, 2);
	const char* message = luaL_checkstring(L, 3);

	if (irc_cmd_msg(sess->session, destination, message)) {
		const char* error = irc_strerror(irc_errno(sess->session));
		return luaL_error(L, error);
	}

	return 0;
}
// on the topic of listing channels: this would be able to do it too... ugh
// session:send_raw(string rawline)
int ircsess_send_raw(lua_State* L) {
	lua_irc_session* sess = (lua_irc_session*)luaL_checkudata(L, 1, "luaircclient_session");

	const char* message = luaL_checkstring(L, 3);

	if (irc_send_raw(sess->session, message)) { // TODO: printf-type formatting string support
		const char* error = irc_strerror(irc_errno(sess->session));
		return luaL_error(L, error);
	}

	return 0;
}
// session:think(number timeout = 0.25)
int ircsess_think(lua_State* L) {
	lua_irc_session* sess = (lua_irc_session*)luaL_checkudata(L, 1, "luaircclient_session");

	if (!irc_is_connected(sess->session) && sess->autoreconnect) {
		if (irc_connect(sess->session, sess->host, sess->port, 0, sess->nickname, sess->username, sess->realname)) {
			const char* error = irc_strerror(irc_errno(sess->session));
			return luaL_error(L, error);
		}
	}

	struct timeval tv;
	fd_set in_set, out_set;
	int maxfd = 0;
	
	tv.tv_usec = (int)(luaL_optnumber(L, 2, 0.25) * 1000000);

	FD_ZERO(&in_set);
	FD_ZERO(&out_set);

	irc_add_select_descriptors(sess->session, &in_set, &out_set, &maxfd);

	if (select(maxfd+1, &in_set, &out_set, 0, &tv) < 0) {
		// Failed to select. TODO: handle this
	}

	if (irc_process_select_descriptors(sess->session, &in_set, &out_set)) {
		printf("failed to process\n");
		printf("error: %d", irc_errno(sess->session));
		if (!irc_is_connected(sess->session) && sess->autoreconnect) {
			if (irc_connect(sess->session, sess->host, sess->port, 0, sess->nickname, sess->username, sess->realname)) {
				const char* error = irc_strerror(irc_errno(sess->session));
				return luaL_error(L, error);
			}
		}
	}
	
	return 0;
}
// session:set_option(string option, bool enabled)
int ircsess_setoption(lua_State* L) {
	lua_irc_session* sess = (lua_irc_session*)luaL_checkudata(L, 1, "luaircclient_session");

	int option = luaL_checkoption(L, 2, NULL, irc_options);
	int enable = lua_toboolean(L, 3);

	if (option == 3) {
		sess->autoreconnect = enable;
	}

	if (enable) {
		if (option == 0) {
			irc_option_set(sess->session, LIBIRC_OPTION_DEBUG);
		} else if (option == 1) {
			irc_option_set(sess->session, LIBIRC_OPTION_SSL_NO_VERIFY);
		}
	} else {
		if (option == 0) {
			irc_option_reset(sess->session, LIBIRC_OPTION_DEBUG);
		} else if (option == 1) {
			irc_option_reset(sess->session, LIBIRC_OPTION_SSL_NO_VERIFY);
		}
	}
}


static const struct luaL_reg api[] = {
	{"new", ircclient_new},
	{"listen", ircclient_listen},
	{"think", ircclient_think},
	{NULL, NULL}
};

static const struct luaL_reg meta_funcs[] = {
	{"connect", ircsess_connect},
	{"quit", ircsess_quit},
	{"join", ircsess_join},
	{"part", ircsess_part},
	{"invite", ircsess_invite},
	{"names", ircsess_names},
	{"send", ircsess_send},
	{"think", ircsess_think},
	{"set_option", ircsess_setoption},
	{NULL, NULL}
};

static const struct luaL_reg meta_methods[] = {
	{"__gc", ircmeta_gc},
	{"__tostring", ircmeta_tostring},
	{NULL, NULL}
};

LUAIRC_API int luaopen_ircclient(lua_State* L) {
	// Metatable registration
	luaL_newmetatable(L, "luaircclient_session");
	luaL_register(L, NULL, meta_methods);

	lua_pushliteral(L, "__index");
	lua_newtable(L);
	luaL_register(L, NULL, meta_funcs);

	lua_settable(L, -3);
	
	// Callback registration
	setup_callbacks(); // ensure that our callbacks exist

	luaL_register(L, "irc", api);

	unsigned int high, low;
	irc_get_version(&high, &low);

	char libircclient_version[50];
	sprintf(libircclient_version, "%d.%0.2d", high, low);

	lua_pushliteral(L, "_VERSION");
	lua_pushfstring(L, "luaircclient %s using libircclient %s", LUAIRC_VERSION, libircclient_version);

	lua_rawset(L, -3); // add _VERSION to table

	return 1; // api at the top of the stack
}
