namespace math {
template <typename T> T identity(T value) {
  return value;
}
template <typename T> class Box {
public:
  explicit Box(T value) : value_(value) {}
  T get() const {
    return value_;
  }

private:
  T value_;
};
} // namespace math
