namespace outer {
namespace inner {
enum class Mode {
  Idle,
  Busy,
};
class Widget {
public:
  void run() {}
};
void helper() {}
} // namespace inner
} // namespace outer
