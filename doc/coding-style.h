/******************************************************************************
 * Copyright (C) 2011  Intel Corporation. All rights reserved.
 *****************************************************************************/
/*!
 * @file coding-style.h
 * @brief The provman coding guidelines
 *
 * @section coding Coding Style
 * 
 * Provman uses the linux kernel coding standard (1), with some exceptions and
 * additions.  The linux kernel coding standard needs to be read in addition
 * to this document before code can be submitted to provman.
 * 
 * There is one exception to the linux coding standard permitted by provman
 * 
 * typedefs are used for structures, for example
 * \code
 *    typedef struct provman_buf_t_ provman_buf_t;
 * 
 *    struct provman_buf_t_ {
 * 	   unsigned int size;
 * 	   unsigned int max_size;
 * 	   unsigned int block_size;
 * 	   uint8_t *buffer;
 *    };
 * \endcode
 * 
 * We have a number of additions, each of which will be described in its own
 * section.
 * 
 * @section naming Naming
 * 
 * All identifiers, be they labels, function names, types or variables should be
 * in lower case. Underscores '_', should be used to separate the different 
 * words that compose multi-word identifiers, e.g., do_something.
 * 
 * Identifiers that are accessible outside the file in which they are defined
 * should be prefixed by <code>provman_</code> or <code>PROVMAN_ </code>
 * depending on whether they are types, functions and variables or macros.
 * 
 * The names of any new types defined should end in <code>_t</code>.
 * 
 * Functions that are private to a given file should be static and their names
 * should be prefixed with <code>prv_</code>, e.g., <code>prv_open_file</code>.
 * 
 * The names of functions that are accessible outside a file and are
 * essentially methods that operate on a structure should be prefixed by the
 * name of the structure, in addition to <code>provman_</code>.  Therefore a
 * function that creates a pointer array might be called 
 * <code>provman_pointer_array_create</code>. 
 * 
 * @section parameters Parameters
 * 
 * Function parameters should not be prefixed with any identifiers that indicate
 * whether they are input or output parameters.  
 * 
 * Input parameters should appear at the beginning of the parameter list.
 * Output parameters should appear at the end. The first parameter of a method
 * that operates on an existing structure instance should be a pointer to that
 * structure instance. An exception here are constructor functions, where the
 * created structure instance will be passed out of the constructor using the 
 * final output parameter of the parameter list. Here are some examples,
 * 
 * \code
 * void plugin_map_file_new(const char *fname, map_file_t **map_file);
 * void plugin_map_file_save(map_file_t *map_file);
 * \endcode
 *
 * @section errors Errors
 * 
 * Functions that can return error codes should have an int return type.
 * Standard error codes are defined in <code>error.h</code>, but you are
 * free to use your own codes, with the restriction that 0 means success.
 * In all but the most simple functions errors should be handled using
 * <code>gotos</code>.  For example;
 * 
 * \code
 * int fn()
 * {
 *         int err;
 * 
 *         err = fn1();
 *         if (err != PROVMAN_ERR_NONE)
 *                  goto on_error;
 *
 *         err = fn2();
 *         if (err != PROVMAN_ERR_NONE)
 *                 goto on_error;
 * 
 * on_error:
 * 
 *         return err;
 * }
 * \endcode
 * 
 * The variable <code>err</code> should be used to store the error code and
 * the label <code>on_error</code> should be used as the label to jump to in
 * case of error.  You are free to define additional labels using your own
 * naming scheme if necessary, e.g., <code>no_free</code>.
 * 
 * @section functions Functions
 * 
 * Functions should only have one exit point.  Multiple exit points typically
 * lead to memory leaks and other errors.  Multiple return statements are 
 * permitted in one case only and this is when both return statements appear at
 * the bottom of the function on either sides of an error label.  Sometimes
 * this is useful to avoid having to set local variables whose ownership is
 * being transferred to the caller to NULL, e.g.,
 * 
 * \code
 * int fn(char **name)
 * {
 *         char *str;
 *         int err = PROVMAN_ERR_NONE;
 * 	    
 *         str = strdup("Hello");
 *         if (!str) {
 *                 err = PROVMAN_ERR_OOM;
 *                 goto on_error;
 *         }
 *
 *         err = fn1(str);
 *         if (err != DMC_ERR_NONE)
 *                 goto on_error;
 *
 *         *name = str;
 * 
 *         return PROVMAN_ERR_NONE;
 * 
 * on_error:
 * 
 *         free(str);
 * 
 *         return err;
 * }
 * \endcode
 *
 * @section constructors Constructors/Destructors
 * 
 * It is useful to define common idioms for functions that create and delete
 * object instances.  Two types of constructor/destructor pairs are defined.
 * 
 * Type 1 uses two phase construction and expects memory for the object to 
 * be allocated by the caller.  The first constructor name should end with
 * <code>_init</code> and should have a return type <code>void</code>.  It 
 * initialises all
 * object members to a default value. Calls to <code>*_init</code> methods 
 * should occur at the start of a function before any functions that can return
 * errors have been called.  A second constructor function, whose name ends with
 * <code>_create</code> completes the object construction and returns an
 * <code>int</code>
 * error code.  A function whose name ends in <code>_free</code> is used to
 * deallocate any resources owned by the object, but not the memory of the
 * object itself.  The <code>_free</code> function can be called any time 
 * after the init method has been called.
 * 
 * This two phased object construction exists as away to cut down on heap
 * usage.  It allows small objects to be created easily on the stack.  An
 * example of its usage is shown below.
 * 
 * \code
 * int fn()
 * {
 *         my_obj obj;
 *         int err;
 * 
 *         my_obj_init(&obj);
 *      
 *         err = my_obj_create(&obj);
 *         if (err != PROVMAN_ERR_NONE)
 *                 goto on_error;
 *         err = my_obj_do_something(&obj);
 *         if (err != PROVMAN_ERR_NONE)
 *                 goto on_error;
 * 
 * on_error:
 * 
 *         my_obj_free(&obj);
 * 
 *         return err;
 * }
 * \endcode
 * 
 * The second form of object construction has only one constructor.  The 
 * name of the constructor should end with <code>_new</code>.  It allocates 
 * memory for the object and performs all initialisation.  After the function
 * returns successfully the object has been created and is ready to use.  The
 * constructor should return an int error code.  The new object should be passed
 * to the caller via a parameter.  The names of destructors for objects
 * allocated in this way should end in <code>_delete</code>.  Calling such
 * destructors on NULL pointers should have no effect.  An example of the single
 * phase constructor is shown below.
 *
 * \code
 * int fn()
 * {
 *         my_obj *obj = NULL;
 *         int err;
 * 
 *         err = my_obj_new(&obj);
 *         if (err != PROVMAN_ERR_NONE)
 *                 goto on_error;
 *         err = my_obj_do_something(obj);
 *         if (err != PROVMAN_ERR_NONE)
 *                 goto on_error;
 * 
 * on_error:
 * 
 *         my_obj_delete(obj);
 * 
 *         return err;
 * }
 * \endcode
 *
 * In cases where the <code>new</code> method allocates memory using a glib
 * function and it calls no other method that returns an error code, it can
 * be declared void as it cannot return an error method to the caller.
 * 
 * @section headers Header Files
 * 
 * Should include header guards whose names should be in uppercase and derived 
 * from the project name, e.g., <code>PROVMAN</code>, and the filename.  The 
 * guard names should be followed by an underscore, e.g.,
 * 
 * \code
 * #ifndef PROVMAN_UTILS_H__
 * #define PROVMAN_UTILS_H__
 * \endcode
 *
 * Provman itself is written in C but we do not want to prevent plugin writers
 * from writing their plugins in C++.  For this reason, any header file accessible
 * to plugins, i.e., those located in the include folder, should enclose 
 * any functions that it exports by 
 * 
 * \code
 * #ifdef __cplusplus
 * extern "C" {
 * #endif
 * 
 * 
 * #ifdef __cplusplus
 * }
 * #endif
 * \endcode
 * 
 * Public functions, structures, variables and macros located in header files
 * in the includes directory must be documented using doxygen comments.
 * 
 * @section includes Include Directives
 * 
 * Header files should be included in the following order
 * 
 * <ol>
 * <li> <code>config.h</code> </li>
 * <li> posix include files </li>
 * <li> external library include files </li>
 * <li> external provman include files </li>
 * <li> local include files </li>
 * </ol>
 *
 * <code>config.h</code> should never be included in a header file as it
 * does not contain any header guards.
 * 
 * @section local-variables Local Variables
 * 
 * Should all be declared at the top of the function.  This is enforced by 
 * compiler options.
 *
 * @section global-variables Global Variables
 * 
 * Modifiable global variables should be avoided where possible. Constant global 
 * variables are permitted. The names of global variables should be beging with
 * g_, i.e., g_token_dictionary.
 * 
 * @section git Git Usage
 * 
 * We will try to mantain a single branch in our git repository.  Please rebase
 * your changes before submitting patches.
 *
 * Do not submit multiple features or bugs fixes in the same patch.  Split your
 * changes into separate smaller patches.
 *
 * The first line of each patch comment should contain an identifier in square
 * brackets that indicates what is being modified.  Some predefined identifiers
 * are:
 * <ul>
 * <li>[Documentation] for changes to the general documentation stored under the
 *     docs folder.</li>
 * <li>[Provman] for changes to any code stored under src or include.</li>
 * <li>[PLUGIN-NAME] for changes to a specified plugin, PLUGIN-NAME to be replaced
 *     by the name of the plugin, e.g., ofono.
 * <li>[Tests] for changes to the test cases.
 * </ul>
 * 
 * @section references References
 * 
 * (1) http://www.kernel.org/doc/Documentation/CodingStyle

 ******************************************************************************/

