namespace variadic {
template <typename... Ts> void log_all(Ts&&... values) {
  (void)sizeof...(values);
}
template <typename T, typename... Rest> class Pack {
public:
  Pack(T first, Rest... rest) : first_(first) {
    (void)sizeof...(rest);
  }
  std::size_t size() const {
    return 1 + sizeof...(Rest);
  }

private:
  T first_;
};
} // namespace variadic
