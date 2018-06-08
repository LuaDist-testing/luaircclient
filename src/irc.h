//includes
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#include "libircclient.h"
#include "libirc_rfcnumeric.h"

#define LUA_LIB

#include "lua.h"
#include "lauxlib.h"

// Defines
#ifndef LUAIRC_API
#define LUAIRC_API extern
#endif

#define LUAIRC_VERSION "1.0-4"

// Util

typedef struct {
	lua_State* L; // The Lua state which created me
	int callback_table_ref; // My reference to the callback table from L
	irc_session_t* session; // The IRC session we refer to

	const char* nickname; // Current nickname
	const char* realname; // Current realname
	const char* username; // Current username
	const char* host; // Host address
	int port; // Host port
	int autoreconnect; // automatically reconnect on disconnect
} lua_irc_session;

// ==HANDLER FUNCTIONS==
void irc_client_event(irc_session_t* session, const char* event,
		const char* origin, const char** params, unsigned int numParams) {

	lua_irc_session* sess = (lua_irc_session*)irc_get_ctx(session);

	if (strcmp(event, "NICK") == 0) {
		if (sess->nickname == NULL || strcmp(sess->nickname, origin) == 0) {
			printf("My nick is now %s\n", params[0]);
			sess->nickname = params[0];
		}
	}

	lua_rawgeti(sess->L, LUA_REGISTRYINDEX, sess->callback_table_ref);

	char event_chr[50]; // event is unlikely to be smaller than this.
	memcpy(event_chr, event, strlen(event)+1);

	int i;
	for(i = 0; i<50; i++){
		event_chr[i] = tolower(event_chr[i]); // Lower-case conversion
	}

	lua_pushstring(sess->L, event_chr);
	lua_rawget(sess->L, -2); // get our handler

	if (!lua_isnil(sess->L, -1)) { // if our handler exists, call it
		lua_pushvalue(sess->L, 1); // our userdata is at the top of the stack
		int extra = 0;
		if (origin != NULL) { // if our origin isn't null, push it too
			lua_pushstring(sess->L, origin);
			extra = 1;
		}

		unsigned int i; // push params
		for (i = 0; i < numParams; i++) {
			lua_pushstring(sess->L, params[i]);
		}

		lua_call(sess->L, numParams+extra+1, 0);
	} else {
		lua_pop(sess->L, 1); // pop nil

		// Call the `default` handler if there
		lua_pushliteral(sess->L, "default");
		lua_gettable(sess->L, -2);

		if (!lua_isnil(sess->L, -1)) { // if our handler exists, call it
			lua_pushvalue(sess->L, 1);
			lua_pushstring(sess->L, event_chr);
			int extra = 0;
			if (origin != NULL) {
				lua_pushstring(sess->L, origin);
				extra = 1;
			}

			unsigned int i;
			for (i = 0; i < numParams; i++) {
				lua_pushstring(sess->L, params[i]);
			}

			lua_call(sess->L, numParams+extra+2, 0);
		} else {
			lua_pop(sess->L, 1); // pop nil
		}
	}

	lua_pop(sess->L, 1); // pop ref
}
// The following function is the same, except for event_numeric
void irc_client_event_numeric(irc_session_t* session, unsigned int event,
		const char* origin, const char** params, unsigned int numParams) {

	lua_irc_session* sess = (lua_irc_session*)irc_get_ctx(session);

	lua_rawgeti(sess->L, LUA_REGISTRYINDEX, sess->callback_table_ref);

	lua_pushliteral(sess->L, "numeric");
	lua_gettable(sess->L, -2);

	if (!lua_isnil(sess->L, -1)) {
		lua_pushvalue(sess->L, 1);
		lua_pushinteger(sess->L, event);

		int extra = 0;
		if (origin != NULL) {
			lua_pushstring(sess->L, origin);
			extra = 1;
		}

		unsigned int i;
		for (i = 0; i < numParams; i++) {
			lua_pushstring(sess->L, params[i]);
		}

		lua_call(sess->L, numParams+extra+2, 0);
	}
 
	lua_pop(sess->L, 1); // pop ref
}

irc_callbacks_t callbacks;
static void setup_callbacks() {
	memset(&callbacks, 0, sizeof(callbacks));

	// TODO: make these nicer
	callbacks.event_connect = irc_client_event;
	callbacks.event_nick = irc_client_event;
	callbacks.event_quit = irc_client_event;
	callbacks.event_join = irc_client_event;
	callbacks.event_part = irc_client_event;
	callbacks.event_mode = irc_client_event;
	callbacks.event_umode = irc_client_event;
	callbacks.event_topic = irc_client_event;
	callbacks.event_kick = irc_client_event;
	callbacks.event_channel = irc_client_event;
	callbacks.event_privmsg = irc_client_event;
	callbacks.event_notice = irc_client_event;
	callbacks.event_channel_notice = irc_client_event;
	callbacks.event_invite = irc_client_event;
	callbacks.event_ctcp_req = irc_client_event;
	callbacks.event_ctcp_rep = irc_client_event;
	callbacks.event_ctcp_action = irc_client_event;
	callbacks.event_unknown = irc_client_event;
	callbacks.event_numeric = irc_client_event_numeric;
}


// Creates a new irc_session_t based on callbacks at the top of the lua stack
static irc_session_t* new_lua_irc_session(lua_State* L) {
	irc_session_t* session = irc_create_session(&callbacks);

	if (!session) {
		// Oh no, we errored :(
		return NULL;
	}

	irc_option_set(session, LIBIRC_OPTION_STRIPNICKS);


	int ref = luaL_ref(L, LUA_REGISTRYINDEX);
	size_t nbytes = sizeof(lua_irc_session); // TODO: check if this is correct


	lua_irc_session* l_session = (lua_irc_session*)lua_newuserdata(L, nbytes);
	l_session->session = session;
	l_session->callback_table_ref = ref;
	l_session->L = L;
	l_session->nickname = "unknown";
	l_session->host = "unknown";
	l_session->port = 0;

	irc_set_ctx(session, l_session);
	return session;
}

// Destroys an irc_session_t once it has been used
static void lua_irc_session_destroy(lua_irc_session* l_session) {
	// Get our lua information from the irc session
	irc_session_t* session = l_session->session;

	if (irc_is_connected(session))
		irc_disconnect(session);

	// remove our callback table reference
	luaL_unref(l_session->L, LUA_REGISTRYINDEX,l_session->callback_table_ref);

	// And finally, destroy the session.
	irc_destroy_session(session);
}
