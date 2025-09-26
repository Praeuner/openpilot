#include "bp_video_widget.h"
#include <QDebug>
#include <QMouseEvent>

// Vertex shader for NV12 YUV to RGB conversion (OpenGL ES 3.2)
static const char* vertex_shader_source = R"(
#version 320 es
precision mediump float;
layout (location = 0) in vec2 aPosition;
layout (location = 1) in vec2 aTexCoord;

uniform mat4 uProjection;
uniform mat4 uModel;

out vec2 vTexCoord;

void main() {
    gl_Position = uProjection * uModel * vec4(aPosition, 0.0, 1.0);
    vTexCoord = aTexCoord;
}
)";

// Fixed fragment shader with proper YUV to RGB conversion
static const char* fragment_shader_source = R"(
#version 320 es
precision highp float;
in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D uYTexture;
uniform sampler2D uUVTexture;

void main() {
    float y = texture(uYTexture, vTexCoord).r;
    vec2 uv = texture(uUVTexture, vTexCoord).rg - vec2(0.5, 0.5);

    // BT.601 YUV to RGB conversion (more compatible)
    float r = y + 1.370705 * uv.g;
    float g = y - 0.337633 * uv.r - 0.698001 * uv.g;
    float b = y + 1.732446 * uv.r;

    FragColor = vec4(clamp(r, 0.0, 1.0), clamp(g, 0.0, 1.0), clamp(b, 0.0, 1.0), 1.0);
}
)";

BPVideoWidget::BPVideoWidget(QWidget* parent)
    : QOpenGLWidget(parent), vertex_buffer(QOpenGLBuffer::VertexBuffer) {
    setFocusPolicy(Qt::StrongFocus);
}

BPVideoWidget::~BPVideoWidget() {
    makeCurrent();
    if (y_texture) {
        glDeleteTextures(1, &y_texture);
    }
    if (uv_texture) {
        glDeleteTextures(1, &uv_texture);
    }
    doneCurrent();
}

void BPVideoWidget::initializeGL() {
    initializeOpenGLFunctions();

    qDebug() << "[VIDEO WIDGET] Initializing OpenGL";
    qDebug() << "[VIDEO WIDGET] OpenGL Version:" << (char*)glGetString(GL_VERSION);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    setupShaders();
    setupBuffers();

    // Create textures
    glGenTextures(1, &y_texture);
    glGenTextures(1, &uv_texture);

    qDebug() << "[VIDEO WIDGET] OpenGL initialized successfully";
}

void BPVideoWidget::setupShaders() {
    shader_program = std::make_unique<QOpenGLShaderProgram>();

    if (!shader_program->addShaderFromSourceCode(QOpenGLShader::Vertex, vertex_shader_source)) {
        qWarning() << "[VIDEO WIDGET] Vertex shader compilation failed:" << shader_program->log();
        return;
    }

    if (!shader_program->addShaderFromSourceCode(QOpenGLShader::Fragment, fragment_shader_source)) {
        qWarning() << "[VIDEO WIDGET] Fragment shader compilation failed:" << shader_program->log();
        return;
    }

    if (!shader_program->link()) {
        qWarning() << "[VIDEO WIDGET] Shader program linking failed:" << shader_program->log();
        return;
    }

    qDebug() << "[VIDEO WIDGET] Shaders compiled and linked successfully";
}

void BPVideoWidget::setupBuffers() {
    // Quad vertices (position + texture coordinates)
    float vertices[] = {
        // Position   // TexCoord
        -1.0f, -1.0f,  0.0f, 1.0f,  // Bottom-left
         1.0f, -1.0f,  1.0f, 1.0f,  // Bottom-right
         1.0f,  1.0f,  1.0f, 0.0f,  // Top-right
        -1.0f,  1.0f,  0.0f, 0.0f   // Top-left
    };

    GLuint indices[] = {
        0, 1, 2,  // First triangle
        2, 3, 0   // Second triangle
    };

    vertex_array.create();
    vertex_array.bind();

    vertex_buffer.create();
    vertex_buffer.bind();
    vertex_buffer.allocate(vertices, sizeof(vertices));

    // Position attribute
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // Texture coordinate attribute
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    // Element buffer
    GLuint ebo;
    glGenBuffers(1, &ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    vertex_array.release();
    vertex_buffer.release();

    qDebug() << "[VIDEO WIDGET] Vertex buffers setup complete";
}

void BPVideoWidget::resizeGL(int width, int height) {
    glViewport(0, 0, width, height);

    // Update projection matrix for orthographic projection
    projection_matrix.setToIdentity();
    projection_matrix.ortho(-1.0f, 1.0f, -1.0f, 1.0f, -1.0f, 1.0f);

    model_matrix.setToIdentity();
}

void BPVideoWidget::displayFrame(VisionBuf* buf, int width, int height) {
    if (!buf) {
        qWarning() << "[VIDEO WIDGET] Null buffer provided";
        return;
    }

    static int frame_count = 0;
    frame_count++;
    if (frame_count % 60 == 0) {  // Log every 60th frame
        qDebug() << "[VIDEO WIDGET] Frame" << frame_count << ":" << width << "x" << height
                 << "stride:" << buf->stride << "Y addr:" << (void*)buf->y
                 << "UV addr:" << (void*)buf->uv;
    }

    makeCurrent();
    updateTextures(buf, width, height);
    frame_width = width;
    frame_height = height;
    has_frame = true;
    update(); // Schedule a repaint
    doneCurrent();
}

void BPVideoWidget::updateTextures(VisionBuf* buf, int width, int height) {
    if (!buf || !buf->y || !buf->uv) {
        qWarning() << "[VIDEO WIDGET] ERROR: Invalid VisionBuf data";
        return;
    }

    static int update_count = 0;
    update_count++;
    if (update_count % 60 == 0) {
        qDebug() << "[VIDEO WIDGET] Updating textures:" << width << "x" << height
                 << "stride:" << buf->stride << "Y addr:" << (void*)buf->y
                 << "UV addr:" << (void*)buf->uv;
    }

    // Handle stride properly for aligned memory access
    // CRITICAL: Use GL_UNPACK_ROW_LENGTH to handle stride correctly

    // Update Y texture (luminance)
    glBindTexture(GL_TEXTURE_2D, y_texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, buf->stride); // Tell OpenGL about the actual stride
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, buf->y);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0); // Reset to default
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    // Update UV texture (chroma) - NV12 has interleaved UV
    glBindTexture(GL_TEXTURE_2D, uv_texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, buf->stride / 2); // UV plane has same stride but half the pixel width
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG8, width/2, height/2, 0, GL_RG, GL_UNSIGNED_BYTE, buf->uv);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0); // Reset to default
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_2D, 0);

    // Check for OpenGL errors
    GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        qWarning() << "[VIDEO WIDGET] OpenGL error during texture update:" << error;
    }
}

void BPVideoWidget::paintGL() {
    glClear(GL_COLOR_BUFFER_BIT);

    if (!has_frame || !shader_program) {
        return;
    }

    shader_program->bind();

    // Set uniforms
    shader_program->setUniformValue("uProjection", projection_matrix);
    shader_program->setUniformValue("uModel", model_matrix);
    shader_program->setUniformValue("uYTexture", 0);
    shader_program->setUniformValue("uUVTexture", 1);

    // Bind textures
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, y_texture);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, uv_texture);

    // Draw quad
    vertex_array.bind();
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
    vertex_array.release();

    // Cleanup
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);

    shader_program->release();
}

void BPVideoWidget::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        emit clicked();
    }
    QOpenGLWidget::mouseReleaseEvent(event);
}

// MOC file will be generated automatically by the build system
