/// \file
/// \brief Declaration of classes in the SystemException namespace
/// \ingroup utility

#ifndef SystemException_h
#define SystemException_h

#include <boost/system/system_error.hpp>

/// Namespace for names dealing with common exceptions
namespace SystemException {

    #define STRINGIFY_(x) #x
    #define TOSTRING(x) STRINGIFY_(x)

    //******************************************************************
    #define here(operation)\
	there(__FILE__, __func__, TOSTRING(__LINE__), operation)
    /// \def here
    /// \return The constructed string.
    ///
    /// \brief The here macro constructs a string that reflects the location
    /// of its expansion using standard cpp macros __FILE__ and __LINE__
    /// along with the C99 compiler's expansion of __func__
    /// and the operation argument.
    /// The here macro relies on the #there function for formatting the
    /// the location there.

    static inline std::string there(
	char const *	file,		///< __FILE__
	char const *	function,	///< __func__
	char const *	line,		///< __LINE__
	char const *	operation)	///< operation
    throw()
    /// \brief Return a formatted string that reflects the location specified
    /// by its arguments (there).
    /// \return A formattted string for the location there
    //******************************************************************
    {
	return std::string("")
	    + file
	    + ';'
	    + function
	    + ';'
	    + line
	    + ';'
	    + operation;
    }

    //******************************************************************
    #define throwErrorIfNegative1(result) throwErrorIfNegative1_(\
	result, __FILE__, __func__, TOSTRING(__LINE__), STRINGIFY_(result))
    /**
    \def throwErrorIfNegative1(result)
    \param result	result to test against -1 and, if not equal, return
    \return The result if not -1; otherwise throw SystemException::Error
    \throw Error If result is -1

    \brief Throws an SystemException::Error if result is -1;
    otherwise, result is returned as the function value.

    Consider main.cc:
    \code
	#include <iostream>

	#include <unistd.h>

	#include "SystemException.h"

	int main(int argc, char ** argv, char ** envv) {
	    try {
		char buffer[64];
		ssize_t count = SystemException::throwErrorIfNegative1(
		    read(-1, buffer, sizeof buffer));
	    }
	    catch (std::exception & e) {
		std::cerr << e.what() << std::endl;
	    }
	    return 0;
	}
    \endcode
    When run, this example will print:
    \code
	main.cc:main:11:read(-1, buffer, sizeof(buffer): Bad file descriptor
    \endcode
    */

    static inline int throwErrorIfNegative1_(
	int result,		///< Function result if no error
	char const * file,	///< __FILE__
	char const * function,	///< __func__
	char const * line,	///< __LINE__
	char const * operation)	///< operation
    throw(boost::system::system_error)
    /// \brief Throw an SystemException::Error (constructed with errno and
    /// a formatted concatenation of file, function, line and operation)
    /// if result is -1; otherwise, result is returned as the function value.
    /// \throw Error If result is -1
    /// \return result if not -1; otherwise throw SystemException::Error
    //******************************************************************
    {
	if (-1 == result)
	    throw boost::system::system_error(
		errno,
		boost::system::system_category(),
		there(file, function, line, operation));
	return result;
    }
}

#endif
