#pragma once
#include <framework/disable_all_warnings.h>
#include <framework/opengl_includes.h>
DISABLE_WARNINGS_PUSH()
#include <glm/mat3x3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/gtc/type_ptr.hpp>
DISABLE_WARNINGS_POP()

// Uniform setters for the currently bound program.
// Shader::getUniformLocation prints a warning for every uniform the driver optimised away, which
// floods the console as soon as a feature is toggled off. These stay silent instead: setting
// location -1 is a no-op in OpenGL.
namespace uniform {

inline GLint location(const char* name)
{
    GLint program = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &program);
    return glGetUniformLocation(static_cast<GLuint>(program), name);
}

inline void set(const char* name, int value) { glUniform1i(location(name), value); }
inline void set(const char* name, bool value) { glUniform1i(location(name), value ? 1 : 0); }
inline void set(const char* name, float value) { glUniform1f(location(name), value); }
inline void set(const char* name, const glm::vec2& value) { glUniform2fv(location(name), 1, glm::value_ptr(value)); }
inline void set(const char* name, const glm::vec3& value) { glUniform3fv(location(name), 1, glm::value_ptr(value)); }
inline void set(const char* name, const glm::vec4& value) { glUniform4fv(location(name), 1, glm::value_ptr(value)); }
inline void set(const char* name, const glm::mat3& value) { glUniformMatrix3fv(location(name), 1, GL_FALSE, glm::value_ptr(value)); }
inline void set(const char* name, const glm::mat4& value) { glUniformMatrix4fv(location(name), 1, GL_FALSE, glm::value_ptr(value)); }

inline void setArray(const char* name, const glm::vec4* values, int count) { glUniform4fv(location(name), count, glm::value_ptr(values[0])); }
inline void setArray(const char* name, const glm::vec3* values, int count) { glUniform3fv(location(name), count, glm::value_ptr(values[0])); }

}
