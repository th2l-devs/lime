#include <system/DocumentSystem.h>

namespace lime
{

    DocumentSystem::DocumentSystem(const char *treeUri)
    {
        JNIEnv *env = (JNIEnv *)JNI::GetEnv();
        jclass cls = env->FindClass("org/haxe/lime/DocumentSystem");
        jmethodID constructor = env->GetMethodID(cls, "<init>", "(Ljava/lang/String;)V");
        jstring jTreeUri = env->NewStringUTF(treeUri);
        jobject obj = env->NewObject(cls, constructor, jTreeUri);

        if (obj == nullptr)
        {
            __android_log_print(ANDROID_LOG_VERBOSE, "Lime", "Failed to create a DocumentSystem Java object: %ld", (long)pthread_self());
            return;
        }

        this->javaObject = env->NewGlobalRef(obj);
        env->DeleteLocalRef(jTreeUri);
        env->DeleteLocalRef(obj);
    }

    DocumentSystem::~DocumentSystem()
    {
        JNIEnv *env = (JNIEnv *)JNI::GetEnv();
        if (this->javaObject != nullptr)
        {
            env->DeleteGlobalRef(this->javaObject);
        }
    }

    const QuickVec<unsigned char> DocumentSystem::readBytes(const char *path)
    {
        JNIEnv *env = (JNIEnv *)JNI::GetEnv();
        jmethodID mid = env->GetMethodID(env->GetObjectClass(this->javaObject), "readBytes", "(Ljava/lang/String;)[B");
        jstring jPath = env->NewStringUTF(path);
        jobject jResult = env->CallObjectMethod(this->javaObject, mid, jPath);

        if (env->ExceptionCheck() || jResult == NULL)
        {
            if (env->ExceptionCheck())
            {
                env->ExceptionDescribe();
                env->ExceptionClear();
            }
            env->DeleteLocalRef(jPath);
            return QuickVec<unsigned char>();
        }

        jboolean copy;
        int length = env->GetArrayLength((jarray)jResult);
        jbyte *jData = env->GetByteArrayElements((jbyteArray)jResult, &copy);

        QuickVec<unsigned char> results;
        results.append(reinterpret_cast<unsigned char*>(jData), length);

        env->ReleaseByteArrayElements((jbyteArray)jResult, jData, JNI_ABORT);
        env->DeleteLocalRef(jPath);
        env->DeleteLocalRef(jResult);

        return results;
    }

    void DocumentSystem::writeBytes(const char *path, Bytes *data)
    {
        JNIEnv *env = (JNIEnv *)JNI::GetEnv();
        jmethodID mid = env->GetMethodID(env->GetObjectClass(this->javaObject), "writeBytes", "(Ljava/lang/String;[B)V");
        jstring jPath = env->NewStringUTF(path);
        jbyteArray jData = env->NewByteArray(data->length);

        env->SetByteArrayRegion(jData, 0, data->length, reinterpret_cast<const jbyte *>(data->b));
        env->CallVoidMethod(this->javaObject, mid, jPath, jData);

        env->DeleteLocalRef(jPath);
        env->DeleteLocalRef(jData);
    }

    void DocumentSystem::createDirectory(const char *path)
    {
        JNIEnv *env = (JNIEnv *)JNI::GetEnv();
        jmethodID mid = env->GetMethodID(env->GetObjectClass(this->javaObject), "createDirectory", "(Ljava/lang/String;)V");
        jstring jPath = env->NewStringUTF(path);

        env->CallVoidMethod(this->javaObject, mid, jPath);

        env->DeleteLocalRef(jPath);
    }

    value DocumentSystem::readDirectory(const char *path)
    {
        JNIEnv *env = (JNIEnv *)JNI::GetEnv();
        jmethodID mid = env->GetMethodID(env->GetObjectClass(this->javaObject), "readDirectory", "(Ljava/lang/String;)[Ljava/lang/String;");
        jstring jPath = env->NewStringUTF(path);

        jobjectArray jArray = (jobjectArray)env->CallObjectMethod(this->javaObject, mid, jPath);
        int length = env->GetArrayLength(jArray);

        value result = alloc_array(length);

        for (int i = 0; i < length; i++)
        {
            jboolean is_copy;
            jstring jStr = (jstring)env->GetObjectArrayElement(jArray, i);
            const char *cStr = env->GetStringUTFChars(jStr, &is_copy);

            val_array_set_i(result, i, alloc_string(cStr));

            env->ReleaseStringUTFChars(jStr, cStr);
            env->DeleteLocalRef(jStr);
        }

        env->DeleteLocalRef(jPath);

        return result;
    }

    bool DocumentSystem::exists(const char *path)
    {
        JNIEnv *env = (JNIEnv *)JNI::GetEnv();
        jmethodID mid = env->GetMethodID(env->GetObjectClass(this->javaObject), "exists", "(Ljava/lang/String;)Z");
        jstring jPath = env->NewStringUTF(path);

        bool result = env->CallBooleanMethod(this->javaObject, mid, jPath);

        env->DeleteLocalRef(jPath);

        return result;
    }

    bool DocumentSystem::deleteDirectory(const char *path)
    {
        JNIEnv *env = (JNIEnv *)JNI::GetEnv();
        jmethodID mid = env->GetMethodID(env->GetObjectClass(this->javaObject), "deleteDirectory", "(Ljava/lang/String;)Z");
        jstring jPath = env->NewStringUTF(path);

        bool result = env->CallBooleanMethod(this->javaObject, mid, jPath);

        env->DeleteLocalRef(jPath);

        return result;
    }

    bool DocumentSystem::deleteFile(const char *path)
    {
        JNIEnv *env = (JNIEnv *)JNI::GetEnv();
        jmethodID mid = env->GetMethodID(env->GetObjectClass(this->javaObject), "deleteFile", "(Ljava/lang/String;)Z");
        jstring jPath = env->NewStringUTF(path);

        bool result = env->CallBooleanMethod(this->javaObject, mid, jPath);

        env->DeleteLocalRef(jPath);

        return result;
    }

    bool DocumentSystem::isDirectory(const char *path)
    {
        JNIEnv *env = (JNIEnv *)JNI::GetEnv();
        jmethodID mid = env->GetMethodID(env->GetObjectClass(this->javaObject), "isDirectory", "(Ljava/lang/String;)Z");
        jstring jPath = env->NewStringUTF(path);

        bool result = env->CallBooleanMethod(this->javaObject, mid, jPath);

        env->DeleteLocalRef(jPath);

        return result;
    }

}
