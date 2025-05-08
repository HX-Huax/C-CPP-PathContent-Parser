#define add(T) _ADD_IMPL##T
#define ADD_IMPL(T)                                                            \
  T add(T)(T a, T b) { return a + b; }
ADD_IMPL(int);
int main() { add(int)(1, 3); }
