#pragma once

#include <QPushButton>

// Custom button class for drawing perfect media control icons
class MediaControlButton : public QPushButton {
  Q_OBJECT

public:
  enum IconType {
    Play,
    Pause,
    RewindArrow,
    ForwardArrow
  };

  explicit MediaControlButton(IconType type, QWidget *parent = nullptr);
  void setIconType(IconType type);

protected:
  void paintEvent(QPaintEvent *event) override;

private:
  IconType iconType;
};

