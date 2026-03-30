namespace graphics {
class Drawable {
public:
  virtual ~Drawable() {}
  virtual void draw() const {}
};
class Movable {
public:
  virtual ~Movable() {}
  virtual void move(int dx, int dy) {}
};
class Sprite : public Drawable, public Movable {
public:
  Sprite() {}
  void draw() const override {}
  void move(int dx, int dy) override {}
};
} // namespace graphics
