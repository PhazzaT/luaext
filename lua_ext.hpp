#pragma once

// Copyright (c) 2019 Piotr Dulikowski

// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:

// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.

// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include <functional>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <cassert>

#include <lua5.3/lua.hpp>

namespace LuaExt
{

template <typename F>
struct fun_ret_
{
};

template <typename Ret, typename... Args>
struct fun_ret_<Ret (*)(Args...)>
{
	using type = Ret;
};

template <typename F>
struct fun_num_args_
{
};

template <typename Ret, typename... Args>
struct fun_num_args_<Ret (*)(Args...)>
{
	static const unsigned int value = sizeof...(Args);
};

struct pusher_
{
#define PUSHARG(type, pushfunc) \
	static void pushArg(lua_State *L, type v) { pushfunc(L, v); }

	PUSHARG(bool, lua_pushboolean);
	PUSHARG(long long, lua_pushinteger);
	PUSHARG(long, lua_pushinteger);
	PUSHARG(int, lua_pushinteger);
	PUSHARG(short, lua_pushinteger);
	PUSHARG(double, lua_pushnumber);
	PUSHARG(float, lua_pushnumber);
	PUSHARG(const char *, lua_pushstring);
	PUSHARG(void *, lua_pushlightuserdata);

#undef PUSHARG

	template <typename T1, typename... T>
	static void pushArgs(lua_State *L, T1 firstArg, T... args)
	{
		pushArg(L, firstArg);
		pushArgs(L, args...);
	}

	static void pushArgs(lua_State *L)
	{
	}

	template <typename Ret>
	static int callAndPush(lua_State *L, const std::function<Ret(lua_State *)> &fun)
	{
		pushArg(L, fun(L));
		return 1;
	}

	static int callAndPush(lua_State *L, const std::function<void(lua_State *)> &fun)
	{
		fun(L);
		return 0;
	}
};

template <typename Ret>
struct popper_
{
};

#define POPARG(type, popfunc)                                                           \
	template <>                                                                         \
	struct popper_<type>                                                                \
	{                                                                                   \
		static type popArg(lua_State *L, int argid) { return (type)popfunc(L, argid); } \
	};

POPARG(bool, luaL_checkinteger);
POPARG(long long, luaL_checkinteger);
POPARG(long, luaL_checkinteger);
POPARG(int, luaL_checkinteger);
POPARG(short, luaL_checkinteger);
POPARG(double, luaL_checknumber);
POPARG(float, luaL_checknumber);
POPARG(const char *, luaL_checkstring);
POPARG(void *, lua_touserdata);

template <>
struct popper_<void>
{
	static void popArg(lua_State *L, int argid) {}
};

#undef POPARG

template <int C, int... I>
struct caller_proxy_
{
	template <typename F, F f, typename... Ty>
	static auto call(lua_State *L) -> typename fun_ret_<F>::type
	{
		return caller_proxy_<C - 1, 1, (I + 1)...>::template call<F, f, Ty...>(L);
	}

	template <typename CL, typename Ret, typename... Ty>
	static auto call_op(lua_State *L, const CL &cl) -> Ret
	{
		return caller_proxy_<C - 1, 1, (I + 1)...>::template call_op<CL, Ret, Ty...>(L, cl);
	}
};

template <int... I>
struct caller_proxy_<0, I...>
{
	template <typename F, F f, typename... Ty>
	static auto call(lua_State *L) -> typename fun_ret_<F>::type
	{
		return f(popper_<Ty>::popArg(L, I)...);
	}

	template <typename CL, typename Ret, typename... Ty>
	static auto call_op(lua_State *L, const CL &cl) -> Ret
	{
		return cl(popper_<Ty>::popArg(L, I)...);
	}
};

template <typename F>
struct wrapper
{
};

template <typename Ret, typename... Ty>
struct wrapper<Ret (*)(Ty...)>
{
	using F = Ret (*)(Ty...);

	template <F f>
	static auto wrapLua() -> lua_CFunction
	{
		return [](lua_State *L) -> int {
			auto ret = caller_proxy_<fun_num_args_<F>::value>::template call<F, f, Ty...>(L);
			pusher_::pushArg(L, ret);
			return 1;
		};
	}
};

template <typename... Ty>
struct wrapper<void (*)(Ty...)>
{
	using F = void (*)(Ty...);

	template <F f>
	static auto wrapLua() -> lua_CFunction
	{
		return [](lua_State *L) -> int {
			caller_proxy_<fun_num_args_<F>::value>::template call<F, f, Ty...>(L);
			return 0;
		};
	}
};

template <typename F>
struct methptr_proxy_
{
};

template <typename C, typename Ret, typename... Ty>
struct methptr_proxy_<Ret (C::*)(Ty...) const>
{
	using F = Ret (C::*)(Ty...) const;

	template <F f>
	static int call_op(lua_State *L, const C &c)
	{
		auto ret = caller_proxy_<sizeof...(Ty)>::template call_op<C, Ret, Ty...>(L, c);
		pusher_::pushArg(L, ret);
		return 1;
	}
};

template <typename C, typename... Ty>
struct methptr_proxy_<void (C::*)(Ty...) const>
{
	using F = void (C::*)(Ty...) const;

	template <F f>
	static int call_op(lua_State *L, const C &c)
	{
		caller_proxy_<sizeof...(Ty)>::template call_op<C, void, Ty...>(L, c);
		return 0;
	}
};

template <typename C>
void pushClosure(lua_State *L, const C &c)
{
	using OP = decltype(&C::operator());

	lua_CFunction caller = [](lua_State *L) -> int {
		// Zdob�d� funktor
		C *c = (C *)lua_touserdata(L, lua_upvalueindex(1));
		return methptr_proxy_<OP>::template call_op<&C::operator()>(L, *c);
	};

	lua_CFunction destructor = [](lua_State *L) -> int {
		C *c = (C *)lua_touserdata(L, 1);
		c->~C();
	};

	// Tworzymy miejsce na funktor i kopiujemy go
	void *mem = lua_newuserdata(L, sizeof(C));
	C *pc = new (mem) C(c);

	// Tworzymy metatabel� i umieszczamy w niej destruktor
	lua_newtable(L);
	lua_pushliteral(L, "__gc");
	lua_pushcfunction(L, destructor);
	lua_rawset(L, -3);

	// Ustawiamy metatabel� dla funktora
	lua_setmetatable(L, -2);

	// Wrzucamy wo�acza na stos
	lua_pushcclosure(L, caller, 1);
}

class LuaFunctionBase
{
protected:
	lua_State *L;
	int rid;

public:
	LuaFunctionBase(lua_State *L, int rid)
		: L(L), rid(rid)
	{
	}

	LuaFunctionBase(const LuaFunctionBase &lf)
		: L(lf.L)
	{
		lua_rawgeti(L, LUA_REGISTRYINDEX, lf.rid);
		rid = luaL_ref(L, LUA_REGISTRYINDEX);
	}

	LuaFunctionBase(LuaFunctionBase &&lf)
		: L(lf.L), rid(lf.rid)
	{
		lf.rid = LUA_REFNIL;
	}

	~LuaFunctionBase()
	{
		luaL_unref(L, LUA_REGISTRYINDEX, rid);
	}
};

template <typename F>
struct help_
{
};

template <typename Ret, typename... Args>
struct help_<Ret(Args...)>
{
	class LuaSimpleFunction : public LuaFunctionBase
	{
	public:
		LuaSimpleFunction(lua_State *L, int rid)
			: LuaFunctionBase(L, rid)
		{
		}

		Ret operator()(Args... args)
		{
			// Wrzu� funkcj� na stos
			lua_rawgeti(L, LUA_REGISTRYINDEX, rid);
			pusher_::pushArgs(L, args...);

			// Wykonaj funkcj�
			// TODO: Rozwa�y� pcall zamiast call
			// i raportowanie b��d�w w inny spos�b?
			lua_call(L, sizeof...(Args), 1);
			return popper_<Ret>::popArg(L, -1);
		}
	};

	static std::function<Ret(Args...)> call(lua_State *L)
	{
		// Umieszczamy nasz� funkcj� w tabeli
		int rid = luaL_ref(L, LUA_REGISTRYINDEX);
		return std::function<Ret(Args...)>(LuaSimpleFunction(L, rid));
	}
};

// Zak�ada, �e warto�� funkcji jest od�o�ona na szczycie stosu.
// Nale�y uwa�a�, aby wyzerowa�/usun�� wska�nik przed usuni�ciem stanu Lua.
// TODO: Rozszerzy� o mo�liwo�� zwracania tupli (kilka warto�ci zwracanych).
template <typename F>
std::function<F> storeLuaFunction(lua_State *L)
{
	return help_<F>::call(L);
}

class LuaYieldableFunction : public LuaFunctionBase
{
public:
	class Thread
	{
	private:
		lua_State *L;
		int rid; // For thread value
		lua_State *thread;
		int coroutineState;

	public:
		// Creates new yieldable function from the function on the top of the stack
		Thread(lua_State *L)
			: L(L), rid(0), thread(nullptr), coroutineState(LUA_YIELD)
		{
			// Create new thread and move it to the registry
			thread = lua_newthread(L);
			rid = luaL_ref(L, LUA_REGISTRYINDEX);

			// Move function to new thread's stack and leave it there
			lua_xmove(L, thread, 1);
		}

		Thread(const Thread &);
		Thread(Thread &&lyf)
			: L(lyf.L), rid(lyf.rid), thread(lyf.thread), coroutineState(lyf.coroutineState)
		{
			lyf.thread = nullptr;
		}

		~Thread()
		{
			if (thread != nullptr)
				luaL_unref(L, LUA_REGISTRYINDEX, rid);
		}

		Thread &operator=(const Thread &) = delete;

		inline bool hasEnded() const
		{
			return coroutineState != LUA_YIELD;
		}

		template <typename Ret = void, typename... Args>
		Ret resume(Args... args)
		{
			// Assert we can continue execution
			assert(!hasEnded());

			pusher_::pushArgs(thread, args...);

			coroutineState = lua_resume(thread, nullptr, sizeof...(args));

			if (coroutineState >= LUA_ERRRUN)
			{
				const char *s = luaL_checkstring(thread, -1);
				printf("ERROR: %s\n", s);
				assert(false);
			}

			// Assert that only one value was returned
			auto top = lua_gettop(thread);
			bool error = false;
			if (std::is_same<void, Ret>::value)
				//assert(top == 0);
				error = top != 0;
			else
				//assert(top == 1);
				error = top != 1;

			if (error)
			{
				printf("Top is %d\n", top);
				printf("ERROR: %s\n", lua_tostring(L, -1));
				assert(false);
			}

			return popper_<Ret>::popArg(thread, -1);
		}
	};

	LuaYieldableFunction(lua_State *L, int rid)
		: LuaFunctionBase(L, rid)
	{
	}

	Thread spawn() const
	{
		lua_rawgeti(L, LUA_REGISTRYINDEX, rid);
		return Thread(L);
	}
};

inline LuaYieldableFunction storeLuaYieldableFunction(lua_State *L)
{
	int rid = luaL_ref(L, LUA_REGISTRYINDEX);
	return LuaYieldableFunction(L, rid);
}
}; // namespace LuaExt

#define toLuaFunction(X) (LuaExt::wrapper<decltype(X) *>::wrapLua<X>())
