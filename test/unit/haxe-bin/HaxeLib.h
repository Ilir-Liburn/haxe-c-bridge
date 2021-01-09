/**
 * HaxeLib.h
 * Automatically generated by HaxeEmbed
 */

#ifndef HaxeLib_h
#define HaxeLib_h

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "MessagePayload.h"

typedef void HaxeLib_AliasA;
typedef HaxeLib_AliasA HaxeLib_CppVoidX;
typedef void (* function_Void) (void);
typedef int (* function_Int) (void);
typedef const char* (* function_Int_String) (int);
typedef int (* function_String_Int) (const char*);
typedef void (* function_Int_Void) (int);
typedef int* (* function_cpp_Pointer_Int__cpp_Pointer_Int_) (int*);
typedef const char* (* function_CustomStar_Int__String) (int*);
typedef function_CustomStar_Int__String HaxeLib_FunctionAlias;
enum HaxeLib_IntEnumAbstract {
	A = 0,
	B = 1
};
typedef enum HaxeLib_IntEnumAbstract HaxeLib_EnumAlias;
enum HaxeLib_IndirectlyReferencedEnum {
	AAA = 9,
	BBB = 10,
	CCC = 8
};

typedef void (* HaxeExceptionCallback) (const char* exceptionInfo);

#ifdef __cplusplus
extern "C" {
#endif

	/**
	 * Initializes a haxe thread that remains alive indefinitely and executes the user's haxe main()
	 * 
	 * This must be first before calling haxe functions
	 * 
	 * @param unhandledExceptionCallback a callback to execute if an unhandled exception occurs on the haxe thread. The haxe thread will continue processing events after an unhandled exception. Use `NULL` for no callback
	 * @returns `NULL` if the thread initializes successfully or a null terminated C string if an error occurs during initialization
	 */
	const char* HaxeLib_initializeHaxeThread(HaxeExceptionCallback unhandledExceptionCallback);

	/**
	 * Ends the haxe thread after it finishes processing pending events (events scheduled in the future will not be executed). Once ended, it cannot be restarted
	 * 
	 * Blocks until the haxe thread has finished
	 * 
	 * Thread-safety: May be called on a different thread to `HaxeLib_startHaxeThread`
	 */
	void HaxeLib_stopHaxeThread();

	/**
	 * Some doc
	 * @param a some integer
	 * @param b some string
	 * @returns void
	 */
	void HaxeLib_voidRtn(int a, const char* b);

	void HaxeLib_noArgsNoReturn();

	/**
	 * when called externally from C this function will be executed synchronously on the main thread
	 */
	bool HaxeLib_callInMainThread(double f64);

	/**
	 * When called externally from C this function will be executed on the calling thread.
	 * Beware: you cannot interact with the rest of your code without first synchronizing with the main thread (or risk crashes)
	 */
	bool HaxeLib_callInExternalThread(double f64);

	int HaxeLib_add(int a, int b);

	int* HaxeLib_starPointers(void* starVoid, HaxeLib_CppVoidX* starVoid2, HaxeLib_CppVoidX* customStar, int** customStar2, const void* constStarVoid, int* starInt, const char* constCharStar);

	void HaxeLib_rawPointers(void* rawPointer, int64_t* rawInt64Pointer, const void* rawConstPointer);

	void HaxeLib_hxcppPointers(void* pointer, int64_t* int64Pointer, const void* constPointer);

	function_Void HaxeLib_hxcppCallbacks(function_Void voidVoid, function_Int voidInt, function_Int_String intString, function_String_Int stringInt, function_Int_Void intVoid, function_cpp_Pointer_Int__cpp_Pointer_Int_ pointers, HaxeLib_FunctionAlias fnAlias);

	MessagePayload HaxeLib_externStruct(MessagePayload v);

	void HaxeLib_optional(float single);

	void HaxeLib_badOptional(float opt, float notOpt);

	void HaxeLib_enumTypes(enum HaxeLib_IntEnumAbstract e, const char* s, HaxeLib_EnumAlias a, enum HaxeLib_IndirectlyReferencedEnum* i, enum HaxeLib_IndirectlyReferencedEnum** ii);

	void HaxeLib_cppCoreTypes(size_t sizet, char char_, const char* constCharStar);

	/**
	 * single-line doc
	 */
	int HaxeLib_somePublicMethod(int i, double f, float s, signed char i8, short i16, int i32, int64_t i64, uint64_t ui64, const char* str);

	void HaxeLib_throwException();

	const char* HaxeLib_pack__ExampleClass_ExampleClassPrivate_examplePrivate();

	const char* HaxeLib_pack_ExampleClass_example();

#ifdef __cplusplus
}
#endif

#endif /* HaxeLib_h */
