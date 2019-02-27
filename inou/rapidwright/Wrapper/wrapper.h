#ifndef WRAPPER_H
#define WRAPPER_H

#include <jni.h>
#include <string>

class Wrapper{
public:
    JavaVM *jvm;               // Pointer to the JVM (Java Virtual Machine)
    JNIEnv *env;             // Pointer to native interface
    
    /* bool loadJVM(std::string path)
     * Take the string of the path as the parametr
     * Loads and initializes a java VM and returns a pointer to the JMI imterface pointer
     * Return true if the JNI pointer is right, otherwise return false
     * */
    bool loadJVM(std::string path)
    {
        //================== prepare loading of Java VM ============================
        JavaVMInitArgs vm_args;                        // Initialization arguments
        JavaVMOption* options = new JavaVMOption[1];   // JVM invocation options
        options[0].optionString = (char*)path.c_str();   // where to find java .class
        vm_args.version = JNI_VERSION_1_6;             // minimum Java version
        vm_args.nOptions = 1;                          // number of options
        vm_args.options = options;
        vm_args.ignoreUnrecognized = false;     // invalid options make the JVM init fail

        //=============== load and initialize Java VM and JNI interface =============
        jint rc = JNI_CreateJavaVM(&jvm, (void**)&env, &vm_args);  // YES !!
        delete options;    // we then no longer need the initialisation options.i
	return (rc == JNI_OK);
    }
    

    /* void displayVersion()
     *Check the version and print out the version of the mative method interface
     * */
    void displayVersion()
    {
        jint version = env->GetVersion();
	std::cout << ((version >> 16) &0x0f) << "." << (version & 0x0f) << std::endl;
	return;
    }

    /*jclass findClass(std::string cls_name)
     *Read in the string of the class as parameter 
     *return java class object or NULL if an error occurs
     * */
    jclass findClass(std::string cls_name)
    {
        jclass cls = env->FindClass((char*)cls_name.c_str());
	return cls;
    }

    jmethodID findMethod(jclass cls, std::string method, std::string sig)
    {
        jmethodID id = env->GetStaticMethodID(cls,(char*)method.c_str(), (char*)sig.c_str());
	return id;
    }

    
};


#endif
