/**
 * HaxeCBridge Function Binding Implementation
 * Automatically generated by HaxeCBridge
 */
#include <hxcpp.h>
#include <hx/Native.h>
#include <hx/Thread.h>
#include <hx/StdLibs.h>
#include <hx/GC.h>
#include <HaxeCBridge.h>
#include <Retainer.h>
#include <assert.h>
#include <queue>
#include <utility>
#include <atomic>

#include "../HaxeLib.h"

#include <test/HxPublicApi.h>
#include <Main.h>
#include <pack/_ExampleClass/ExampleClassPrivate.h>
#include <pack/ExampleClass.h>

namespace HaxeCBridgeInternal {
	std::atomic<bool> threadStarted = { false };
	std::atomic<bool> threadRunning = { false };
	// once haxe statics are initialized we cannot clear them for a clean restart
	std::atomic<bool> staticsInitialized = { false };

	struct HaxeThreadData {
		HaxeExceptionCallback haxeExceptionCallback;
		const char* initExceptionInfo;
	};

	HxSemaphore threadInitSemaphore;
	HxSemaphore threadEndSemaphore;
	HxMutex threadManageMutex;
	Dynamic mainThreadRef;

	void defaultExceptionHandler(const char* info) {
		printf("Unhandled haxe exception: %s\n", info);
	}

	typedef void (* MainThreadCallback)(void* data);
	HxMutex queueMutex;
	std::queue<std::pair<MainThreadCallback, void*>> queue;

	void runInMainThread(MainThreadCallback callback, void* data) {
		queueMutex.Lock();
		queue.push(std::make_pair(callback, data));
		queueMutex.Unlock();
		HaxeCBridge::wakeMainThread();
	}

	// called on the haxe main thread
	void processNativeCalls() {
		AutoLock lock(queueMutex);
		while(!queue.empty()) {
			std::pair<MainThreadCallback, void*> pair = queue.front();
			queue.pop();
			pair.first(pair.second);
		}
	}

	bool isHaxeMainThread() {
		hx::NativeAttach autoAttach;
		Dynamic currentInfo = __hxcpp_thread_current();
		return HaxeCBridgeInternal::mainThreadRef.mPtr == currentInfo.mPtr;
	}
}

THREAD_FUNC_TYPE haxeMainThreadFunc(void *data) {
	HX_TOP_OF_STACK
	HaxeCBridgeInternal::HaxeThreadData* threadData = (HaxeCBridgeInternal::HaxeThreadData*) data;
	HaxeCBridgeInternal::mainThreadRef = __hxcpp_thread_current();

	HaxeCBridgeInternal::threadRunning = true; // must come after mainThreadRef assignment

	threadData->initExceptionInfo = nullptr;

	// copy out callback
	HaxeExceptionCallback haxeExceptionCallback = threadData->haxeExceptionCallback;

	bool firstRun = !HaxeCBridgeInternal::staticsInitialized;

	// See hx::Init in StdLibs.cpp for reference
	if (!HaxeCBridgeInternal::staticsInitialized) try {
		::hx::Boot();
		__boot_all();
		HaxeCBridgeInternal::staticsInitialized = true;
	} catch(Dynamic initException) {
		// hxcpp init failure or uncaught haxe runtime exception
		threadData->initExceptionInfo = initException->toString().utf8_str();
	}

	if (HaxeCBridgeInternal::staticsInitialized) { // initialized without error
		// blocks running the event loop
		// keeps alive until manual stop is called
		HaxeCBridge::mainThreadInit();
		HaxeCBridgeInternal::threadInitSemaphore.Set();
		HaxeCBridge::mainThreadRun(HaxeCBridgeInternal::processNativeCalls, haxeExceptionCallback);
	} else {
		// failed to initialize statics; unlock init semaphore so _initializeHaxeThread can continue and report the exception 
		HaxeCBridgeInternal::threadInitSemaphore.Set();
	}

	HaxeCBridgeInternal::threadRunning = false;
	HaxeCBridgeInternal::threadEndSemaphore.Set();

	THREAD_FUNC_RET
}

HXCPP_EXTERN_CLASS_ATTRIBUTES
const char* HaxeLib_initializeHaxeThread(HaxeExceptionCallback unhandledExceptionCallback) {
	HaxeCBridgeInternal::HaxeThreadData threadData = {
		.haxeExceptionCallback = unhandledExceptionCallback == nullptr ? HaxeCBridgeInternal::defaultExceptionHandler : unhandledExceptionCallback,
		.initExceptionInfo = nullptr,
	};

	{
		// mutex prevents two threads calling this function from being able to start two haxe threads
		AutoLock lock(HaxeCBridgeInternal::threadManageMutex);
		if (!HaxeCBridgeInternal::threadStarted) {
			// startup the haxe main thread
			HxCreateDetachedThread(haxeMainThreadFunc, &threadData);

			HaxeCBridgeInternal::threadStarted = true;

			// wait until the thread is initialized and ready
			HaxeCBridgeInternal::threadInitSemaphore.Wait();
		} else {
			threadData.initExceptionInfo = "haxe thread cannot be started twice";
		}
	}
				
	if (threadData.initExceptionInfo != nullptr) {
		HaxeLib_stopHaxeThreadIfRunning(false);

		const int returnInfoMax = 1024;
		static char returnInfo[returnInfoMax] = ""; // statically allocated for return safety
		strncpy(returnInfo, threadData.initExceptionInfo, returnInfoMax);
		return returnInfo;
	} else {
		return nullptr;
	}
}

HXCPP_EXTERN_CLASS_ATTRIBUTES
void HaxeLib_stopHaxeThreadIfRunning(bool waitOnScheduledEvents) {
	if (HaxeCBridgeInternal::isHaxeMainThread()) {
		// it is possible for stopHaxeThread to be called from within the haxe thread, while another thread is waiting on HaxeCBridgeInternal::threadEndSemaphore
		// so it is important the haxe thread does not wait on certain locks
		HaxeCBridge::endMainThread(waitOnScheduledEvents);
	} else {
		AutoLock lock(HaxeCBridgeInternal::threadManageMutex);
		if (HaxeCBridgeInternal::threadRunning) {
			struct Callback {
				static void run(void* data) {
					bool* b = (bool*) data;
					HaxeCBridge::endMainThread(*b);
				}
			};

			HaxeCBridgeInternal::runInMainThread(Callback::run, &waitOnScheduledEvents);

			HaxeCBridgeInternal::threadEndSemaphore.Wait();
		}
	}
}

HXCPP_EXTERN_CLASS_ATTRIBUTES
void HaxeLib_releaseHaxeObject(HaxeObject obj) {
	struct Callback {
		static void run(void* data) {
			HaxeCBridge::releaseHaxeObject(Retainer((hx::Object *)data, false));
		}
	};
	HaxeCBridgeInternal::runInMainThread(Callback::run, obj);
}

HXCPP_EXTERN_CLASS_ATTRIBUTES
void HaxeLib_voidRtn(int a0, const char* a1, HaxeLib_NonTrivialAlias a2, HaxeLib_EnumAlias a3) {
	if (HaxeCBridgeInternal::isHaxeMainThread()) {
		return test::HxPublicApi_obj::voidRtn(a0, a1, a2, a3);
	}
	struct Data {
		struct {int a0; const char* a1; HaxeLib_NonTrivialAlias a2; HaxeLib_EnumAlias a3;} args;
		HxSemaphore lock;
	};
	struct Callback {
		static void run(void* p) {
			// executed within the haxe main thread
			Data* data = (Data*) p;
			try {
				test::HxPublicApi_obj::voidRtn(data->args.a0, data->args.a1, data->args.a2, data->args.a3);
				data->lock.Set();
			} catch(Dynamic runtimeException) {
				data->lock.Set();
				throw runtimeException;
			}
		}
	};

	#ifdef HXCPP_DEBUG
	assert(HaxeCBridgeInternal::threadRunning && "haxe thread not running, use HaxeLib_initializeHaxeThread() to activate the haxe thread");
	#endif

	Data data = { {a0, a1, a2, a3} };

	// queue a callback to execute voidRtn() on the main thread and wait until execution completes
	HaxeCBridgeInternal::runInMainThread(Callback::run, &data);
	data.lock.Wait();
}

HXCPP_EXTERN_CLASS_ATTRIBUTES
void HaxeNoArgsNoReturn() {
	if (HaxeCBridgeInternal::isHaxeMainThread()) {
		return test::HxPublicApi_obj::noArgsNoReturn();
	}
	struct Data {
		struct {} args;
		HxSemaphore lock;
	};
	struct Callback {
		static void run(void* p) {
			// executed within the haxe main thread
			Data* data = (Data*) p;
			try {
				test::HxPublicApi_obj::noArgsNoReturn();
				data->lock.Set();
			} catch(Dynamic runtimeException) {
				data->lock.Set();
				throw runtimeException;
			}
		}
	};

	#ifdef HXCPP_DEBUG
	assert(HaxeCBridgeInternal::threadRunning && "haxe thread not running, use HaxeLib_initializeHaxeThread() to activate the haxe thread");
	#endif

	Data data = { {} };

	// queue a callback to execute noArgsNoReturn() on the main thread and wait until execution completes
	HaxeCBridgeInternal::runInMainThread(Callback::run, &data);
	data.lock.Wait();
}

HXCPP_EXTERN_CLASS_ATTRIBUTES
bool HaxeLib_callInMainThread(double a0) {
	if (HaxeCBridgeInternal::isHaxeMainThread()) {
		return test::HxPublicApi_obj::callInMainThread(a0);
	}
	struct Data {
		struct {double a0;} args;
		HxSemaphore lock;
		bool ret;
	};
	struct Callback {
		static void run(void* p) {
			// executed within the haxe main thread
			Data* data = (Data*) p;
			try {
				data->ret = test::HxPublicApi_obj::callInMainThread(data->args.a0);
				data->lock.Set();
			} catch(Dynamic runtimeException) {
				data->lock.Set();
				throw runtimeException;
			}
		}
	};

	#ifdef HXCPP_DEBUG
	assert(HaxeCBridgeInternal::threadRunning && "haxe thread not running, use HaxeLib_initializeHaxeThread() to activate the haxe thread");
	#endif

	Data data = { {a0} };

	// queue a callback to execute callInMainThread() on the main thread and wait until execution completes
	HaxeCBridgeInternal::runInMainThread(Callback::run, &data);
	data.lock.Wait();
	return data.ret;
}

HXCPP_EXTERN_CLASS_ATTRIBUTES
bool HaxeLib_callInExternalThread(double a0) {
	hx::NativeAttach autoAttach;
	return test::HxPublicApi_obj::callInExternalThread(a0);
}

HXCPP_EXTERN_CLASS_ATTRIBUTES
int HaxeLib_add(int a0, int a1) {
	if (HaxeCBridgeInternal::isHaxeMainThread()) {
		return test::HxPublicApi_obj::add(a0, a1);
	}
	struct Data {
		struct {int a0; int a1;} args;
		HxSemaphore lock;
		int ret;
	};
	struct Callback {
		static void run(void* p) {
			// executed within the haxe main thread
			Data* data = (Data*) p;
			try {
				data->ret = test::HxPublicApi_obj::add(data->args.a0, data->args.a1);
				data->lock.Set();
			} catch(Dynamic runtimeException) {
				data->lock.Set();
				throw runtimeException;
			}
		}
	};

	#ifdef HXCPP_DEBUG
	assert(HaxeCBridgeInternal::threadRunning && "haxe thread not running, use HaxeLib_initializeHaxeThread() to activate the haxe thread");
	#endif

	Data data = { {a0, a1} };

	// queue a callback to execute add() on the main thread and wait until execution completes
	HaxeCBridgeInternal::runInMainThread(Callback::run, &data);
	data.lock.Wait();
	return data.ret;
}

HXCPP_EXTERN_CLASS_ATTRIBUTES
int* HaxeLib_starPointers(void* a0, HaxeLib_CppVoidX* a1, HaxeLib_CppVoidX* a2, int** a3, const void* a4, int* a5, const char* a6) {
	if (HaxeCBridgeInternal::isHaxeMainThread()) {
		return test::HxPublicApi_obj::starPointers(a0, a1, a2, a3, a4, a5, a6);
	}
	struct Data {
		struct {void* a0; HaxeLib_CppVoidX* a1; HaxeLib_CppVoidX* a2; int** a3; const void* a4; int* a5; const char* a6;} args;
		HxSemaphore lock;
		int* ret;
	};
	struct Callback {
		static void run(void* p) {
			// executed within the haxe main thread
			Data* data = (Data*) p;
			try {
				data->ret = test::HxPublicApi_obj::starPointers(data->args.a0, data->args.a1, data->args.a2, data->args.a3, data->args.a4, data->args.a5, data->args.a6);
				data->lock.Set();
			} catch(Dynamic runtimeException) {
				data->lock.Set();
				throw runtimeException;
			}
		}
	};

	#ifdef HXCPP_DEBUG
	assert(HaxeCBridgeInternal::threadRunning && "haxe thread not running, use HaxeLib_initializeHaxeThread() to activate the haxe thread");
	#endif

	Data data = { {a0, a1, a2, a3, a4, a5, a6} };

	// queue a callback to execute starPointers() on the main thread and wait until execution completes
	HaxeCBridgeInternal::runInMainThread(Callback::run, &data);
	data.lock.Wait();
	return data.ret;
}

HXCPP_EXTERN_CLASS_ATTRIBUTES
void* HaxeLib_rawPointers(void* a0, int64_t* a1, const void* a2) {
	if (HaxeCBridgeInternal::isHaxeMainThread()) {
		return test::HxPublicApi_obj::rawPointers(a0, a1, a2);
	}
	struct Data {
		struct {void* a0; int64_t* a1; const void* a2;} args;
		HxSemaphore lock;
		void* ret;
	};
	struct Callback {
		static void run(void* p) {
			// executed within the haxe main thread
			Data* data = (Data*) p;
			try {
				data->ret = test::HxPublicApi_obj::rawPointers(data->args.a0, data->args.a1, data->args.a2);
				data->lock.Set();
			} catch(Dynamic runtimeException) {
				data->lock.Set();
				throw runtimeException;
			}
		}
	};

	#ifdef HXCPP_DEBUG
	assert(HaxeCBridgeInternal::threadRunning && "haxe thread not running, use HaxeLib_initializeHaxeThread() to activate the haxe thread");
	#endif

	Data data = { {a0, a1, a2} };

	// queue a callback to execute rawPointers() on the main thread and wait until execution completes
	HaxeCBridgeInternal::runInMainThread(Callback::run, &data);
	data.lock.Wait();
	return data.ret;
}

HXCPP_EXTERN_CLASS_ATTRIBUTES
int64_t* HaxeLib_hxcppPointers(function_Bool_Void a0, void* a1, int64_t* a2, int a3, const void* a4) {
	if (HaxeCBridgeInternal::isHaxeMainThread()) {
		return test::HxPublicApi_obj::hxcppPointers(a0, a1, a2, a3, a4);
	}
	struct Data {
		struct {function_Bool_Void a0; void* a1; int64_t* a2; int a3; const void* a4;} args;
		HxSemaphore lock;
		int64_t* ret;
	};
	struct Callback {
		static void run(void* p) {
			// executed within the haxe main thread
			Data* data = (Data*) p;
			try {
				data->ret = test::HxPublicApi_obj::hxcppPointers(data->args.a0, data->args.a1, data->args.a2, data->args.a3, data->args.a4);
				data->lock.Set();
			} catch(Dynamic runtimeException) {
				data->lock.Set();
				throw runtimeException;
			}
		}
	};

	#ifdef HXCPP_DEBUG
	assert(HaxeCBridgeInternal::threadRunning && "haxe thread not running, use HaxeLib_initializeHaxeThread() to activate the haxe thread");
	#endif

	Data data = { {a0, a1, a2, a3, a4} };

	// queue a callback to execute hxcppPointers() on the main thread and wait until execution completes
	HaxeCBridgeInternal::runInMainThread(Callback::run, &data);
	data.lock.Wait();
	return data.ret;
}

HXCPP_EXTERN_CLASS_ATTRIBUTES
function_Int_cpp_ConstCharStar HaxeLib_hxcppCallbacks(function_Bool_Void a0, function_Void a1, function_Int a2, function_Int_cpp_ConstCharStar a3, function_cpp_Star_Int__cpp_Star_Int_ a4, HaxeLib_FunctionAlias a5, function_MessagePayload_Void a6) {
	if (HaxeCBridgeInternal::isHaxeMainThread()) {
		return test::HxPublicApi_obj::hxcppCallbacks(a0, a1, a2, a3, a4, a5, a6);
	}
	struct Data {
		struct {function_Bool_Void a0; function_Void a1; function_Int a2; function_Int_cpp_ConstCharStar a3; function_cpp_Star_Int__cpp_Star_Int_ a4; HaxeLib_FunctionAlias a5; function_MessagePayload_Void a6;} args;
		HxSemaphore lock;
		function_Int_cpp_ConstCharStar ret;
	};
	struct Callback {
		static void run(void* p) {
			// executed within the haxe main thread
			Data* data = (Data*) p;
			try {
				data->ret = test::HxPublicApi_obj::hxcppCallbacks(data->args.a0, data->args.a1, data->args.a2, data->args.a3, data->args.a4, data->args.a5, data->args.a6);
				data->lock.Set();
			} catch(Dynamic runtimeException) {
				data->lock.Set();
				throw runtimeException;
			}
		}
	};

	#ifdef HXCPP_DEBUG
	assert(HaxeCBridgeInternal::threadRunning && "haxe thread not running, use HaxeLib_initializeHaxeThread() to activate the haxe thread");
	#endif

	Data data = { {a0, a1, a2, a3, a4, a5, a6} };

	// queue a callback to execute hxcppCallbacks() on the main thread and wait until execution completes
	HaxeCBridgeInternal::runInMainThread(Callback::run, &data);
	data.lock.Wait();
	return data.ret;
}

HXCPP_EXTERN_CLASS_ATTRIBUTES
MessagePayload HaxeLib_externStruct(MessagePayload a0, MessagePayload* a1) {
	if (HaxeCBridgeInternal::isHaxeMainThread()) {
		return test::HxPublicApi_obj::externStruct(a0, a1);
	}
	struct Data {
		struct {MessagePayload a0; MessagePayload* a1;} args;
		HxSemaphore lock;
		MessagePayload ret;
	};
	struct Callback {
		static void run(void* p) {
			// executed within the haxe main thread
			Data* data = (Data*) p;
			try {
				data->ret = test::HxPublicApi_obj::externStruct(data->args.a0, data->args.a1);
				data->lock.Set();
			} catch(Dynamic runtimeException) {
				data->lock.Set();
				throw runtimeException;
			}
		}
	};

	#ifdef HXCPP_DEBUG
	assert(HaxeCBridgeInternal::threadRunning && "haxe thread not running, use HaxeLib_initializeHaxeThread() to activate the haxe thread");
	#endif

	Data data = { {a0, a1} };

	// queue a callback to execute externStruct() on the main thread and wait until execution completes
	HaxeCBridgeInternal::runInMainThread(Callback::run, &data);
	data.lock.Wait();
	return data.ret;
}

HXCPP_EXTERN_CLASS_ATTRIBUTES
void HaxeLib_allocateABunchOfData() {
	if (HaxeCBridgeInternal::isHaxeMainThread()) {
		return test::HxPublicApi_obj::allocateABunchOfData();
	}
	struct Data {
		struct {} args;
		HxSemaphore lock;
	};
	struct Callback {
		static void run(void* p) {
			// executed within the haxe main thread
			Data* data = (Data*) p;
			try {
				test::HxPublicApi_obj::allocateABunchOfData();
				data->lock.Set();
			} catch(Dynamic runtimeException) {
				data->lock.Set();
				throw runtimeException;
			}
		}
	};

	#ifdef HXCPP_DEBUG
	assert(HaxeCBridgeInternal::threadRunning && "haxe thread not running, use HaxeLib_initializeHaxeThread() to activate the haxe thread");
	#endif

	Data data = { {} };

	// queue a callback to execute allocateABunchOfData() on the main thread and wait until execution completes
	HaxeCBridgeInternal::runInMainThread(Callback::run, &data);
	data.lock.Wait();
}

HXCPP_EXTERN_CLASS_ATTRIBUTES
void HaxeLib_allocateABunchOfDataExternalThread() {
	hx::NativeAttach autoAttach;
	return test::HxPublicApi_obj::allocateABunchOfDataExternalThread();
}

HXCPP_EXTERN_CLASS_ATTRIBUTES
enum HaxeLib_IntEnum2 HaxeLib_enumTypes(enum HaxeLib_IntEnumAbstract a0, const char* a1, HaxeLib_EnumAlias a2) {
	if (HaxeCBridgeInternal::isHaxeMainThread()) {
		return static_cast<enum HaxeLib_IntEnum2>(test::HxPublicApi_obj::enumTypes(a0, a1, a2));
	}
	struct Data {
		struct {enum HaxeLib_IntEnumAbstract a0; const char* a1; HaxeLib_EnumAlias a2;} args;
		HxSemaphore lock;
		enum HaxeLib_IntEnum2 ret;
	};
	struct Callback {
		static void run(void* p) {
			// executed within the haxe main thread
			Data* data = (Data*) p;
			try {
				data->ret = static_cast<enum HaxeLib_IntEnum2>(test::HxPublicApi_obj::enumTypes(data->args.a0, data->args.a1, data->args.a2));
				data->lock.Set();
			} catch(Dynamic runtimeException) {
				data->lock.Set();
				throw runtimeException;
			}
		}
	};

	#ifdef HXCPP_DEBUG
	assert(HaxeCBridgeInternal::threadRunning && "haxe thread not running, use HaxeLib_initializeHaxeThread() to activate the haxe thread");
	#endif

	Data data = { {a0, a1, a2} };

	// queue a callback to execute enumTypes() on the main thread and wait until execution completes
	HaxeCBridgeInternal::runInMainThread(Callback::run, &data);
	data.lock.Wait();
	return data.ret;
}

HXCPP_EXTERN_CLASS_ATTRIBUTES
void HaxeLib_cppCoreTypes(size_t a0, char a1, const char* a2) {
	if (HaxeCBridgeInternal::isHaxeMainThread()) {
		return test::HxPublicApi_obj::cppCoreTypes(a0, a1, a2);
	}
	struct Data {
		struct {size_t a0; char a1; const char* a2;} args;
		HxSemaphore lock;
	};
	struct Callback {
		static void run(void* p) {
			// executed within the haxe main thread
			Data* data = (Data*) p;
			try {
				test::HxPublicApi_obj::cppCoreTypes(data->args.a0, data->args.a1, data->args.a2);
				data->lock.Set();
			} catch(Dynamic runtimeException) {
				data->lock.Set();
				throw runtimeException;
			}
		}
	};

	#ifdef HXCPP_DEBUG
	assert(HaxeCBridgeInternal::threadRunning && "haxe thread not running, use HaxeLib_initializeHaxeThread() to activate the haxe thread");
	#endif

	Data data = { {a0, a1, a2} };

	// queue a callback to execute cppCoreTypes() on the main thread and wait until execution completes
	HaxeCBridgeInternal::runInMainThread(Callback::run, &data);
	data.lock.Wait();
}

HXCPP_EXTERN_CLASS_ATTRIBUTES
uint64_t HaxeLib_cppCoreTypes2(int a0, double a1, float a2, signed char a3, short a4, int a5, int64_t a6, uint64_t a7, const char* a8) {
	if (HaxeCBridgeInternal::isHaxeMainThread()) {
		return test::HxPublicApi_obj::cppCoreTypes2(a0, a1, a2, a3, a4, a5, a6, a7, a8);
	}
	struct Data {
		struct {int a0; double a1; float a2; signed char a3; short a4; int a5; int64_t a6; uint64_t a7; const char* a8;} args;
		HxSemaphore lock;
		uint64_t ret;
	};
	struct Callback {
		static void run(void* p) {
			// executed within the haxe main thread
			Data* data = (Data*) p;
			try {
				data->ret = test::HxPublicApi_obj::cppCoreTypes2(data->args.a0, data->args.a1, data->args.a2, data->args.a3, data->args.a4, data->args.a5, data->args.a6, data->args.a7, data->args.a8);
				data->lock.Set();
			} catch(Dynamic runtimeException) {
				data->lock.Set();
				throw runtimeException;
			}
		}
	};

	#ifdef HXCPP_DEBUG
	assert(HaxeCBridgeInternal::threadRunning && "haxe thread not running, use HaxeLib_initializeHaxeThread() to activate the haxe thread");
	#endif

	Data data = { {a0, a1, a2, a3, a4, a5, a6, a7, a8} };

	// queue a callback to execute cppCoreTypes2() on the main thread and wait until execution completes
	HaxeCBridgeInternal::runInMainThread(Callback::run, &data);
	data.lock.Wait();
	return data.ret;
}

HXCPP_EXTERN_CLASS_ATTRIBUTES
HaxeObject HaxeLib_createHaxeObject() {
	if (HaxeCBridgeInternal::isHaxeMainThread()) {
		return (HaxeObject)(test::HxPublicApi_obj::createHaxeObject().mPtr);
	}
	struct Data {
		struct {} args;
		HxSemaphore lock;
		HaxeObject ret;
	};
	struct Callback {
		static void run(void* p) {
			// executed within the haxe main thread
			Data* data = (Data*) p;
			try {
				data->ret = (HaxeObject)(test::HxPublicApi_obj::createHaxeObject().mPtr);
				data->lock.Set();
			} catch(Dynamic runtimeException) {
				data->lock.Set();
				throw runtimeException;
			}
		}
	};

	#ifdef HXCPP_DEBUG
	assert(HaxeCBridgeInternal::threadRunning && "haxe thread not running, use HaxeLib_initializeHaxeThread() to activate the haxe thread");
	#endif

	Data data = { {} };

	// queue a callback to execute createHaxeObject() on the main thread and wait until execution completes
	HaxeCBridgeInternal::runInMainThread(Callback::run, &data);
	data.lock.Wait();
	return data.ret;
}

HXCPP_EXTERN_CLASS_ATTRIBUTES
void HaxeLib_testHaxeObject(HaxeObject a0) {
	if (HaxeCBridgeInternal::isHaxeMainThread()) {
		return test::HxPublicApi_obj::testHaxeObject(Retainer((hx::Object *)a0, false));
	}
	struct Data {
		struct {HaxeObject a0;} args;
		HxSemaphore lock;
	};
	struct Callback {
		static void run(void* p) {
			// executed within the haxe main thread
			Data* data = (Data*) p;
			try {
				test::HxPublicApi_obj::testHaxeObject(Retainer((hx::Object *)data->args.a0, false));
				data->lock.Set();
			} catch(Dynamic runtimeException) {
				data->lock.Set();
				throw runtimeException;
			}
		}
	};

	#ifdef HXCPP_DEBUG
	assert(HaxeCBridgeInternal::threadRunning && "haxe thread not running, use HaxeLib_initializeHaxeThread() to activate the haxe thread");
	#endif

	Data data = { {a0} };

	// queue a callback to execute testHaxeObject() on the main thread and wait until execution completes
	HaxeCBridgeInternal::runInMainThread(Callback::run, &data);
	data.lock.Wait();
}

HXCPP_EXTERN_CLASS_ATTRIBUTES
void HaxeLib_throwException() {
	if (HaxeCBridgeInternal::isHaxeMainThread()) {
		return test::HxPublicApi_obj::throwException();
	}
	struct Data {
		struct {} args;
		HxSemaphore lock;
	};
	struct Callback {
		static void run(void* p) {
			// executed within the haxe main thread
			Data* data = (Data*) p;
			try {
				test::HxPublicApi_obj::throwException();
				data->lock.Set();
			} catch(Dynamic runtimeException) {
				data->lock.Set();
				throw runtimeException;
			}
		}
	};

	#ifdef HXCPP_DEBUG
	assert(HaxeCBridgeInternal::threadRunning && "haxe thread not running, use HaxeLib_initializeHaxeThread() to activate the haxe thread");
	#endif

	Data data = { {} };

	// queue a callback to execute throwException() on the main thread and wait until execution completes
	HaxeCBridgeInternal::runInMainThread(Callback::run, &data);
	data.lock.Wait();
}

HXCPP_EXTERN_CLASS_ATTRIBUTES
void HaxeLib_Main_stopLoopingAfterTime_ms(int a0) {
	if (HaxeCBridgeInternal::isHaxeMainThread()) {
		return Main_obj::stopLoopingAfterTime_ms(a0);
	}
	struct Data {
		struct {int a0;} args;
		HxSemaphore lock;
	};
	struct Callback {
		static void run(void* p) {
			// executed within the haxe main thread
			Data* data = (Data*) p;
			try {
				Main_obj::stopLoopingAfterTime_ms(data->args.a0);
				data->lock.Set();
			} catch(Dynamic runtimeException) {
				data->lock.Set();
				throw runtimeException;
			}
		}
	};

	#ifdef HXCPP_DEBUG
	assert(HaxeCBridgeInternal::threadRunning && "haxe thread not running, use HaxeLib_initializeHaxeThread() to activate the haxe thread");
	#endif

	Data data = { {a0} };

	// queue a callback to execute stopLoopingAfterTime_ms() on the main thread and wait until execution completes
	HaxeCBridgeInternal::runInMainThread(Callback::run, &data);
	data.lock.Wait();
}

HXCPP_EXTERN_CLASS_ATTRIBUTES
int HaxeLib_Main_getLoopCount() {
	if (HaxeCBridgeInternal::isHaxeMainThread()) {
		return Main_obj::getLoopCount();
	}
	struct Data {
		struct {} args;
		HxSemaphore lock;
		int ret;
	};
	struct Callback {
		static void run(void* p) {
			// executed within the haxe main thread
			Data* data = (Data*) p;
			try {
				data->ret = Main_obj::getLoopCount();
				data->lock.Set();
			} catch(Dynamic runtimeException) {
				data->lock.Set();
				throw runtimeException;
			}
		}
	};

	#ifdef HXCPP_DEBUG
	assert(HaxeCBridgeInternal::threadRunning && "haxe thread not running, use HaxeLib_initializeHaxeThread() to activate the haxe thread");
	#endif

	Data data = { {} };

	// queue a callback to execute getLoopCount() on the main thread and wait until execution completes
	HaxeCBridgeInternal::runInMainThread(Callback::run, &data);
	data.lock.Wait();
	return data.ret;
}

HXCPP_EXTERN_CLASS_ATTRIBUTES
int HaxeLib_Main_hxcppGcMemUsage() {
	if (HaxeCBridgeInternal::isHaxeMainThread()) {
		return Main_obj::hxcppGcMemUsage();
	}
	struct Data {
		struct {} args;
		HxSemaphore lock;
		int ret;
	};
	struct Callback {
		static void run(void* p) {
			// executed within the haxe main thread
			Data* data = (Data*) p;
			try {
				data->ret = Main_obj::hxcppGcMemUsage();
				data->lock.Set();
			} catch(Dynamic runtimeException) {
				data->lock.Set();
				throw runtimeException;
			}
		}
	};

	#ifdef HXCPP_DEBUG
	assert(HaxeCBridgeInternal::threadRunning && "haxe thread not running, use HaxeLib_initializeHaxeThread() to activate the haxe thread");
	#endif

	Data data = { {} };

	// queue a callback to execute hxcppGcMemUsage() on the main thread and wait until execution completes
	HaxeCBridgeInternal::runInMainThread(Callback::run, &data);
	data.lock.Wait();
	return data.ret;
}

HXCPP_EXTERN_CLASS_ATTRIBUTES
int HaxeLib_Main_hxcppGcMemUsageExternal() {
	hx::NativeAttach autoAttach;
	return Main_obj::hxcppGcMemUsageExternal();
}

HXCPP_EXTERN_CLASS_ATTRIBUTES
void HaxeLib_Main_hxcppGcRun(bool a0) {
	if (HaxeCBridgeInternal::isHaxeMainThread()) {
		return Main_obj::hxcppGcRun(a0);
	}
	struct Data {
		struct {bool a0;} args;
		HxSemaphore lock;
	};
	struct Callback {
		static void run(void* p) {
			// executed within the haxe main thread
			Data* data = (Data*) p;
			try {
				Main_obj::hxcppGcRun(data->args.a0);
				data->lock.Set();
			} catch(Dynamic runtimeException) {
				data->lock.Set();
				throw runtimeException;
			}
		}
	};

	#ifdef HXCPP_DEBUG
	assert(HaxeCBridgeInternal::threadRunning && "haxe thread not running, use HaxeLib_initializeHaxeThread() to activate the haxe thread");
	#endif

	Data data = { {a0} };

	// queue a callback to execute hxcppGcRun() on the main thread and wait until execution completes
	HaxeCBridgeInternal::runInMainThread(Callback::run, &data);
	data.lock.Wait();
}

HXCPP_EXTERN_CLASS_ATTRIBUTES
void HaxeLib_Main_printTime() {
	if (HaxeCBridgeInternal::isHaxeMainThread()) {
		return Main_obj::printTime();
	}
	struct Data {
		struct {} args;
		HxSemaphore lock;
	};
	struct Callback {
		static void run(void* p) {
			// executed within the haxe main thread
			Data* data = (Data*) p;
			try {
				Main_obj::printTime();
				data->lock.Set();
			} catch(Dynamic runtimeException) {
				data->lock.Set();
				throw runtimeException;
			}
		}
	};

	#ifdef HXCPP_DEBUG
	assert(HaxeCBridgeInternal::threadRunning && "haxe thread not running, use HaxeLib_initializeHaxeThread() to activate the haxe thread");
	#endif

	Data data = { {} };

	// queue a callback to execute printTime() on the main thread and wait until execution completes
	HaxeCBridgeInternal::runInMainThread(Callback::run, &data);
	data.lock.Wait();
}

HXCPP_EXTERN_CLASS_ATTRIBUTES
int HaxeLib_pack__ExampleClass_ExampleClassPrivate_examplePrivate() {
	if (HaxeCBridgeInternal::isHaxeMainThread()) {
		return pack::_ExampleClass::ExampleClassPrivate::examplePrivate();
	}
	struct Data {
		struct {} args;
		HxSemaphore lock;
		int ret;
	};
	struct Callback {
		static void run(void* p) {
			// executed within the haxe main thread
			Data* data = (Data*) p;
			try {
				data->ret = pack::_ExampleClass::ExampleClassPrivate::examplePrivate();
				data->lock.Set();
			} catch(Dynamic runtimeException) {
				data->lock.Set();
				throw runtimeException;
			}
		}
	};

	#ifdef HXCPP_DEBUG
	assert(HaxeCBridgeInternal::threadRunning && "haxe thread not running, use HaxeLib_initializeHaxeThread() to activate the haxe thread");
	#endif

	Data data = { {} };

	// queue a callback to execute examplePrivate() on the main thread and wait until execution completes
	HaxeCBridgeInternal::runInMainThread(Callback::run, &data);
	data.lock.Wait();
	return data.ret;
}

HXCPP_EXTERN_CLASS_ATTRIBUTES
int ExamplePrefix_example() {
	if (HaxeCBridgeInternal::isHaxeMainThread()) {
		return pack::ExampleClass_obj::example();
	}
	struct Data {
		struct {} args;
		HxSemaphore lock;
		int ret;
	};
	struct Callback {
		static void run(void* p) {
			// executed within the haxe main thread
			Data* data = (Data*) p;
			try {
				data->ret = pack::ExampleClass_obj::example();
				data->lock.Set();
			} catch(Dynamic runtimeException) {
				data->lock.Set();
				throw runtimeException;
			}
		}
	};

	#ifdef HXCPP_DEBUG
	assert(HaxeCBridgeInternal::threadRunning && "haxe thread not running, use HaxeLib_initializeHaxeThread() to activate the haxe thread");
	#endif

	Data data = { {} };

	// queue a callback to execute example() on the main thread and wait until execution completes
	HaxeCBridgeInternal::runInMainThread(Callback::run, &data);
	data.lock.Wait();
	return data.ret;
}

