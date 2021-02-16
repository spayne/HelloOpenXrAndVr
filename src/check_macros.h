#pragma once

#include <stdarg.h>
#include <string>
#include <memory>
#include <stdexcept>

#include "gfxwrapper_opengl.h"
#include <openxr/openxr.h>
#include <openxr/openxr_reflection.h>

// Macro to generate stringify functions for OpenXR enumerations based data provided in openxr_reflection.h
// clang-format off
#define ENUM_CASE_STR(name, val) case name: return #name;
#define MAKE_TO_STRING_FUNC(enumType)                  \
    inline const char* to_string(enumType e) {         \
        switch (e) {                                   \
            XR_LIST_ENUM_##enumType(ENUM_CASE_STR)     \
            default: return "Unknown " #enumType;      \
        }                                              \
    }
// clang-format on

MAKE_TO_STRING_FUNC(XrResult);




inline std::string Fmt(const char* fmt, ...) {
	va_list vl;
	va_start(vl, fmt);
	int size = std::vsnprintf(nullptr, 0, fmt, vl);
	va_end(vl);

	if (size != -1) {
		std::unique_ptr<char[]> buffer(new char[size + 1]);

		va_start(vl, fmt);
		size = std::vsnprintf(buffer.get(), size + 1, fmt, vl);
		va_end(vl);
		if (size != -1) {
			return std::string(buffer.get(), size);
		}
	}

	throw std::runtime_error("Unexpected vsnprintf failure");
}

[[noreturn]] inline void Throw(std::string failureMessage, const char* originator = nullptr, const char* sourceLocation = nullptr) {
	if (originator != nullptr) {
		failureMessage += Fmt("\n    Origin: %s", originator);
	}
	if (sourceLocation != nullptr) {
		failureMessage += Fmt("\n    Source: %s", sourceLocation);
	}

	throw std::logic_error(failureMessage);
}

[[noreturn]] inline void ThrowXrResult(XrResult res, const char* originator = nullptr, const char* sourceLocation = nullptr) {
	Throw(Fmt("XrResult failure [%s]", to_string(res)), originator, sourceLocation);
}

inline XrResult CheckXrResult(XrResult res, const char* originator = nullptr, const char* sourceLocation = nullptr) {
	if (XR_FAILED(res)) {
		ThrowXrResult(res, originator, sourceLocation);
	}
	return res;
}

#define THROW(msg) Throw(msg, nullptr, FILE_AND_LINE);
#define CHK_STRINGIFY(x) #x
#define TOSTRING(x) CHK_STRINGIFY(x)
#define FILE_AND_LINE __FILE__ ":" TOSTRING(__LINE__)
#define CHECK_XRCMD(cmd) CheckXrResult(cmd, #cmd, FILE_AND_LINE);



inline int CheckOpenVrResult(int res, const char* originator = nullptr, const char* sourceLocation = nullptr)
{
	if (res != 0) {
		printf("openvr failure: %d, line: %s\n", res, sourceLocation);
	}
	return res;
}
#define CHECK_OPENVRCMD(cmd) CheckOpenVrResult(cmd, #cmd, FILE_AND_LINE)

inline void DebugMessageCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* userParam) {
	printf(std::string(std::string("GL Debug: ") + std::string(message, 0, length)).c_str());
}

inline void CheckShader(GLuint shader) {
	GLint r = 0;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &r);
	if (r == GL_FALSE) {
		GLchar msg[4096] = {};
		GLsizei length;
		glGetShaderInfoLog(shader, sizeof(msg), &length, msg);
		THROW(Fmt("Compile shader failed: %s", msg));
	}
}

inline void CheckProgram(GLuint prog) {
	GLint r = 0;
	glGetProgramiv(prog, GL_LINK_STATUS, &r);
	if (r == GL_FALSE) {
		GLchar msg[4096] = {};
		GLsizei length;
		glGetProgramInfoLog(prog, sizeof(msg), &length, msg);
		THROW(Fmt("Link program failed: %s", msg));
	}
}

