#ifndef ARDOP_COMMON_MUSTUSE_H_
#define ARDOP_COMMON_MUSTUSE_H_

/**
 * @def ARDOP_MUSTUSE
 * @brief Indicates return value must be used
 *
 * On supported compilers, this macro indicates that a function's
 * return value must be used. Discarding the return value without
 * checking it (i.e., via if/else) is considered a compiler warning
 * or error. Use this to mark functions that return an error code
 * which must be checked.
 *
 * \code
 * #include "common/mustuse.h"
 *
 * ARDOP_MUSTUSE bool this_function_can_fail(int in, int* out);
 * \endcode
 */

#if defined __has_attribute
#if __has_attribute(warn_unused_result)
#define ARDOP_MUSTUSE __attribute__((warn_unused_result))
#endif
#elif __has_attribute(nodiscard)
#define ARDOP_MUSTUSE __attribute__((nodiscard))
#endif

// If compiler support is not present for nodiscard, make it a no-op
#ifndef ARDOP_MUSTUSE
#define ARDOP_MUSTUSE
#endif

#endif /* ARDOP_COMMON_MUSTUSE_H_ */
