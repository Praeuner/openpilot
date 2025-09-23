#pragma once

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLTexture>
#include <QTimer>
#include <memory>

#include "msgq/visionipc/visionbuf.h"

class BPVideoWidget : public QOpenGLWidget, protected QOpenGLFunctions {
  Q_OBJECT

public:
  explicit BPVideoWidget(QWidget* parent = nullptr);
  ~BPVideoWidget();

  void displayFrame(VisionBuf* buf, int width, int height);
  void setBackgroundColor(const QColor &color) { bg_color = color; }

signals:
  void clicked();

protected:
  void initializeGL() override;
  void paintGL() override;
  void resizeGL(int width, int height) override;
  void mouseReleaseEvent(QMouseEvent *event) override;

private:
  void setupShaders();
  void setupBuffers();
  void updateTextures(VisionBuf* buf, int width, int height);

  QColor bg_color = QColor("#000000");

  // OpenGL resources
  std::unique_ptr<QOpenGLShaderProgram> shader_program;
  QOpenGLBuffer vertex_buffer;
  QOpenGLVertexArrayObject vertex_array;
  GLuint y_texture = 0;
  GLuint uv_texture = 0;

  // Frame data
  int frame_width = 0;
  int frame_height = 0;
  bool has_frame = false;

  // Transformation matrix
  QMatrix4x4 projection_matrix;
  QMatrix4x4 model_matrix;
};
