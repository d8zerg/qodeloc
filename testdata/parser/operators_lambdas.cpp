namespace util {
class Counter {
public:
  Counter() {}
  Counter& operator++();
  explicit operator bool() const;
};
Counter& Counter::operator++() {
  return *this;
}
Counter::operator bool() const {
  return true;
}
void use_lambda() {
  auto value = [](int x) { return x + 1; };
  (void)value;
}
} // namespace util
