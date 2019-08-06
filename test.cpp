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

#include <cstdio>

#include "lua_ext.hpp"

int greeter(int x, int y)
{
    printf("Hello, world! %d %d\n", x, y);
    return x + y;
}

void testCallingCppFunctionFromLua()
{
    lua_State *L = luaL_newstate();

    // toLuaFunction macro allows to skip writing function's type
    auto lfun = toLuaFunction(greeter);
    lua_pushcfunction(L, lfun);
    lua_setglobal(L, "lfun");
    luaL_dostring(L, "return lfun(1, 2)");

    // The result value is nicely returned to the lua interpreter
    int i = luaL_checkinteger(L, 0);
    printf("Result: %d\n", i);

    lua_close(L);
}

void testCallingCppClosureFromLua()
{
    lua_State *L = luaL_newstate();

    int accumulator = 0;
    auto cppfun = [&accumulator](int x) {
        accumulator += x;
    };

    // Unfortunately, the toLuaFunction does not work with closures...
    LuaExt::pushClosure(L, cppfun);
    lua_setglobal(L, "lfun");
    luaL_dostring(L, "lfun(100)\nlfun(99)");

    printf("Result: %d\n", accumulator);

    lua_close(L);
}

void testCallingLuaFunctionFromCpp()
{
    lua_State *L = luaL_newstate();

    luaL_dostring(L, "function luafun(x, y) return x .. y end\nreturn luafun");

    auto cppfun = LuaExt::storeLuaFunction<const char *(const char *, const char *)>(L);
    printf("Result 1: %s\n", cppfun("Hello, ", "World!"));
    printf("Result 2: %s\n", cppfun("Goodbye, ", "World!"));
}

void testCallingLuaCoroutineFromCpp()
{
    lua_State *L = luaL_newstate();
    luaL_openlibs(L);

    luaL_dostring(L, "return function ()\n\
        for i=1,10 do\n\
            print(i)\
            coroutine.yield(i * 7)\n\
        end\n\
        return -1\
    end");

    // The interface for lua coroutines allows resuming with arbitrary arguments,
    // and expecting arbitrary return values
    auto cppcoro = LuaExt::storeLuaYieldableFunction(L);
    auto thread = cppcoro.spawn();
    while (!thread.hasEnded())
    {
        int x = thread.resume<int>();
        printf("Returned from coroutine: %d\n", x);
    }
}

int main(int argc, char **argv)
{
    // testCallingCppFunctionFromLua();
    // testCallingCppClosureFromLua();
    // testCallingLuaFunctionFromCpp();
    // testCallingLuaCoroutineFromCpp();
    return 0;
}
