#include <type_traits>

namespace dispatch_queue {

namespace detail {

#ifdef __cpp_lib_is_invocable
template<class F, class... ArgTypes>
using function_result = typename std::invoke_result<F, ArgTypes...>::type;
#else
template<class F, class... ArgTypes>
using function_result = typename std::result_of<F(ArgTypes...)>::type;
#endif

}

}
