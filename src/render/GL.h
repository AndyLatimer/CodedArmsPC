#pragma once
// Minimal OpenGL 3.3 core loader built on SDL_GL_GetProcAddress (no system GL headers).
#include <cstddef>
#include <cstdint>

#if defined(_WIN32)
#define CA_APIENTRY __stdcall
#else
#define CA_APIENTRY
#endif

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef int GLint;
typedef int GLsizei;
typedef unsigned char GLboolean;
typedef unsigned char GLubyte;
typedef float GLfloat;
typedef unsigned int GLbitfield;
typedef char GLchar;
typedef ptrdiff_t GLsizeiptr;
typedef ptrdiff_t GLintptr;

#define GL_FALSE 0
#define GL_TRUE 1
#define GL_DEPTH_BUFFER_BIT 0x00000100
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_LINES 0x0001
#define GL_TRIANGLES 0x0004
#define GL_LESS 0x0201
#define GL_LEQUAL 0x0203
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_BACK 0x0405
#define GL_CW 0x0900
#define GL_CCW 0x0901
#define GL_CULL_FACE 0x0B44
#define GL_DEPTH_TEST 0x0B71
#define GL_BLEND 0x0BE2
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_PACK_ALIGNMENT 0x0D05
#define GL_TEXTURE_2D 0x0DE1
#define GL_UNSIGNED_BYTE 0x1401
#define GL_FLOAT 0x1406
#define GL_RGB 0x1907
#define GL_RGBA 0x1908
#define GL_VENDOR 0x1F00
#define GL_RENDERER 0x1F01
#define GL_VERSION 0x1F02
#define GL_EXTENSIONS 0x1F03
#define GL_NEAREST 0x2600
#define GL_LINEAR 0x2601
#define GL_NEAREST_MIPMAP_NEAREST 0x2700
#define GL_LINEAR_MIPMAP_NEAREST 0x2701
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_REPEAT 0x2901
#define GL_MULTISAMPLE 0x809D
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_NUM_EXTENSIONS 0x821D
#define GL_TEXTURE_MAX_ANISOTROPY 0x84FE
#define GL_MAX_TEXTURE_MAX_ANISOTROPY 0x84FF
#define GL_TEXTURE0 0x84C0
#define GL_ARRAY_BUFFER 0x8892
#define GL_STREAM_DRAW 0x88E0
#define GL_STATIC_DRAW 0x88E4
#define GL_DYNAMIC_DRAW 0x88E8
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_INFO_LOG_LENGTH 0x8B84

#define CA_GL_FUNCS(X)                                                                          \
    X(const GLubyte*, glGetString, (GLenum name))                                               \
    X(const GLubyte*, glGetStringi, (GLenum name, GLuint index))                                \
    X(void, glGetIntegerv, (GLenum pname, GLint* data))                                         \
    X(void, glGetFloatv, (GLenum pname, GLfloat* data))                                         \
    X(GLenum, glGetError, (void))                                                               \
    X(void, glViewport, (GLint x, GLint y, GLsizei w, GLsizei h))                               \
    X(void, glClearColor, (GLfloat r, GLfloat g, GLfloat b, GLfloat a))                         \
    X(void, glClear, (GLbitfield mask))                                                         \
    X(void, glEnable, (GLenum cap))                                                             \
    X(void, glDisable, (GLenum cap))                                                            \
    X(void, glBlendFunc, (GLenum s, GLenum d))                                                  \
    X(void, glDepthFunc, (GLenum f))                                                            \
    X(void, glDepthMask, (GLboolean f))                                                         \
    X(void, glCullFace, (GLenum m))                                                             \
    X(void, glFrontFace, (GLenum m))                                                            \
    X(void, glPixelStorei, (GLenum p, GLint v))                                                 \
    X(void, glReadPixels, (GLint x, GLint y, GLsizei w, GLsizei h, GLenum f, GLenum t, void* p)) \
    X(void, glGenTextures, (GLsizei n, GLuint* t))                                              \
    X(void, glDeleteTextures, (GLsizei n, const GLuint* t))                                     \
    X(void, glBindTexture, (GLenum target, GLuint t))                                           \
    X(void, glActiveTexture, (GLenum unit))                                                     \
    X(void, glTexImage2D, (GLenum t, GLint l, GLint ifmt, GLsizei w, GLsizei h, GLint b, GLenum f, GLenum ty, const void* p)) \
    X(void, glTexParameteri, (GLenum t, GLenum p, GLint v))                                     \
    X(void, glTexParameterf, (GLenum t, GLenum p, GLfloat v))                                   \
    X(void, glGenerateMipmap, (GLenum t))                                                       \
    X(GLuint, glCreateShader, (GLenum type))                                                    \
    X(void, glShaderSource, (GLuint s, GLsizei n, const GLchar* const* src, const GLint* len))  \
    X(void, glCompileShader, (GLuint s))                                                        \
    X(void, glGetShaderiv, (GLuint s, GLenum p, GLint* v))                                      \
    X(void, glGetShaderInfoLog, (GLuint s, GLsizei max, GLsizei* len, GLchar* log))             \
    X(void, glDeleteShader, (GLuint s))                                                         \
    X(GLuint, glCreateProgram, (void))                                                          \
    X(void, glAttachShader, (GLuint p, GLuint s))                                               \
    X(void, glBindAttribLocation, (GLuint p, GLuint i, const GLchar* n))                        \
    X(void, glLinkProgram, (GLuint p))                                                          \
    X(void, glGetProgramiv, (GLuint p, GLenum n, GLint* v))                                     \
    X(void, glGetProgramInfoLog, (GLuint p, GLsizei max, GLsizei* len, GLchar* log))            \
    X(void, glDeleteProgram, (GLuint p))                                                        \
    X(void, glUseProgram, (GLuint p))                                                           \
    X(GLint, glGetUniformLocation, (GLuint p, const GLchar* n))                                 \
    X(void, glUniform1i, (GLint l, GLint v))                                                    \
    X(void, glUniform1f, (GLint l, GLfloat v))                                                  \
    X(void, glUniform3f, (GLint l, GLfloat a, GLfloat b, GLfloat c))                            \
    X(void, glUniform4f, (GLint l, GLfloat a, GLfloat b, GLfloat c, GLfloat d))                 \
    X(void, glUniformMatrix4fv, (GLint l, GLsizei n, GLboolean t, const GLfloat* v))            \
    X(void, glGenVertexArrays, (GLsizei n, GLuint* a))                                          \
    X(void, glDeleteVertexArrays, (GLsizei n, const GLuint* a))                                 \
    X(void, glBindVertexArray, (GLuint a))                                                      \
    X(void, glGenBuffers, (GLsizei n, GLuint* b))                                               \
    X(void, glDeleteBuffers, (GLsizei n, const GLuint* b))                                      \
    X(void, glBindBuffer, (GLenum t, GLuint b))                                                 \
    X(void, glBufferData, (GLenum t, GLsizeiptr s, const void* d, GLenum u))                    \
    X(void, glVertexAttribPointer, (GLuint i, GLint s, GLenum t, GLboolean n, GLsizei st, const void* p)) \
    X(void, glEnableVertexAttribArray, (GLuint i))                                              \
    X(void, glDrawArrays, (GLenum m, GLint first, GLsizei count))

#define CA_GL_DECLARE(ret, name, args) typedef ret(CA_APIENTRY* PFN_##name) args; extern PFN_##name ca_##name;
CA_GL_FUNCS(CA_GL_DECLARE)
#undef CA_GL_DECLARE

#define glGetString ca_glGetString
#define glGetStringi ca_glGetStringi
#define glGetIntegerv ca_glGetIntegerv
#define glGetFloatv ca_glGetFloatv
#define glGetError ca_glGetError
#define glViewport ca_glViewport
#define glClearColor ca_glClearColor
#define glClear ca_glClear
#define glEnable ca_glEnable
#define glDisable ca_glDisable
#define glBlendFunc ca_glBlendFunc
#define glDepthFunc ca_glDepthFunc
#define glDepthMask ca_glDepthMask
#define glCullFace ca_glCullFace
#define glFrontFace ca_glFrontFace
#define glPixelStorei ca_glPixelStorei
#define glReadPixels ca_glReadPixels
#define glGenTextures ca_glGenTextures
#define glDeleteTextures ca_glDeleteTextures
#define glBindTexture ca_glBindTexture
#define glActiveTexture ca_glActiveTexture
#define glTexImage2D ca_glTexImage2D
#define glTexParameteri ca_glTexParameteri
#define glTexParameterf ca_glTexParameterf
#define glGenerateMipmap ca_glGenerateMipmap
#define glCreateShader ca_glCreateShader
#define glShaderSource ca_glShaderSource
#define glCompileShader ca_glCompileShader
#define glGetShaderiv ca_glGetShaderiv
#define glGetShaderInfoLog ca_glGetShaderInfoLog
#define glDeleteShader ca_glDeleteShader
#define glCreateProgram ca_glCreateProgram
#define glAttachShader ca_glAttachShader
#define glBindAttribLocation ca_glBindAttribLocation
#define glLinkProgram ca_glLinkProgram
#define glGetProgramiv ca_glGetProgramiv
#define glGetProgramInfoLog ca_glGetProgramInfoLog
#define glDeleteProgram ca_glDeleteProgram
#define glUseProgram ca_glUseProgram
#define glGetUniformLocation ca_glGetUniformLocation
#define glUniform1i ca_glUniform1i
#define glUniform1f ca_glUniform1f
#define glUniform3f ca_glUniform3f
#define glUniform4f ca_glUniform4f
#define glUniformMatrix4fv ca_glUniformMatrix4fv
#define glGenVertexArrays ca_glGenVertexArrays
#define glDeleteVertexArrays ca_glDeleteVertexArrays
#define glBindVertexArray ca_glBindVertexArray
#define glGenBuffers ca_glGenBuffers
#define glDeleteBuffers ca_glDeleteBuffers
#define glBindBuffer ca_glBindBuffer
#define glBufferData ca_glBufferData
#define glVertexAttribPointer ca_glVertexAttribPointer
#define glEnableVertexAttribArray ca_glEnableVertexAttribArray
#define glDrawArrays ca_glDrawArrays

namespace ca {
// Loads every function above; returns false and names the first missing one on failure.
bool loadGL(const char** missing);
bool hasGLExtension(const char* name);
} // namespace ca
