#pragma once

#include "Log.h"
#include <memory>
#define MYAPP_DEBUG

#ifdef MYAPP_DEBUG
#define MYAPP_DEBUGBREAK() __debugbreak()
#define MYAPP_ENABLE_ASSERTS
#else
#define MYAPP_DEBUGBREAK()
#endif

// Custom assertion macros that log errors and break execution in debug mode
#ifdef MYAPP_ENABLE_ASSERTS
// Regular assertion for application code
// #define MY_ASSERT(x, ...)                                   \
// 	{                                                          \
// 		if (!(x))                                              \
// 		{                                                      \
// 			LOG_ERROR("Assertion Failed: {0}", __VA_ARGS__); \
// 			MYAPP_DEBUGBREAK();                                \
// 		}                                                      \
// 	}

// #define MY_ASSERT(x, ...) { if(!(x)) { LOG_ERROR("Assertion Failed: {0}", __VA_ARGS__); __debugbreak(); } }
#define MY_ASSERT(x, ...) { if(!(x)) { LOG_ERROR("Assertion Failed at {}:{} - {}", __FILE__, __LINE__, __VA_ARGS__); __debugbreak(); } }
#else
// Empty macros for release builds
#define MY_ASSERT(x, ...)
#endif

// Utility macro for bit operations
#define BIT(x) (1 << x)

// Modern event binding using lambda instead of std::bind
// #define MYAPP_BIND_EVENT_FN(fn) [this](auto &&...args) -> decltype(auto) { return this->fn(std::forward<decltype(args)>(args)...); }