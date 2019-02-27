#include <iostream>
#include <wrapper.h>
#include<string>
using namespace std;

int main(int argc, char** argv)
{
  // using namespace std;
  Wrapper wrapper;

  //================= Prepare and load java VM and JNI interface =========================
  string path = "-Djava.class.path=rapidwright-2018.2.5-standalone-lin64.jar";   // where to find java .class
  bool loadSuccess = wrapper.loadJVM(path);
  if (!loadSuccess) {
    // TO DO: error processing...
    cin.get();
    exit(EXIT_FAILURE);
  }

  //================= Display JVM version =======================================
  cout << "JVM load succeeded: Version ";
  wrapper.displayVersion();

  //================ Find class ===================================
  //string cls_name = "com/xilinx/rapidwright/device/browser/DeviceBrowser";  // try to find the class
  jclass cls2 = wrapper.findClass("com/xilinx/rapidwright/device/browser/DeviceBrowser");
  jclass cls = cls2;
    if(cls2 == nullptr)
    {
        cerr << "ERROR: class not found !";
    }
    else
    {                                  // if class found, continue
    cout << "Class DeviceBrowser is found" << endl;
    jmethodID methodID = wrapper.findMethod(cls, "main", "([Ljava/lang/String;)V");  // find method

    if(methodID == nullptr)
      cerr << "ERROR: method void mymain() not found !" << endl;

    else
    {
     // env->CallStaticVoidMethod(cls2, mid);                      // call method
        jobjectArray ret = (jobjectArray)wrapper.env->NewObjectArray(0,wrapper.env->FindClass("java/lang/String"), wrapper.env->NewStringUTF(""));
        wrapper.env->CallStaticObjectMethod(cls,methodID, ret);
        cout << endl;
    }
    cin.get();
    wrapper.jvm->DestroyJavaVM();
  }
}
