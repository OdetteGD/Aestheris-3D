plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}
android {
    namespace = "com.aetheris.engine"
    compileSdk = 35
    ndkVersion = "27.2.12479018"
    defaultConfig {
        applicationId = "com.aetheris.engine"
        minSdk = 24
        targetSdk = 35
        versionCode = 1
        versionName = "0.1.0"
        ndk { abiFilters += setOf("armeabi-v7a", "arm64-v8a") }
        externalNativeBuild {
            cmake {
                cppFlags += listOf("-std=c++20")
                arguments += listOf(\n                    "-DANDROID_STL=c++_shared",\n                    "-DCMAKE_BUILD_TYPE=Release",\n                    "-DCMAKE_C_COMPILER_LAUNCHER=ccache",\n                    "-DCMAKE_CXX_COMPILER_LAUNCHER=ccache"\n                )
            }
        }
    }
    buildTypes {
        release {
            isMinifyEnabled = false
            isShrinkResources = false
            proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
        }
    }
    externalNativeBuild {
        cmake { path = file("../CMakeLists.txt"); version = "3.22.1" }
    }
    sourceSets {
        getByName("main") { java.srcDirs("../../android") }
    }
    packaging { jniLibs { useLegacyPackaging = false } }
}
kotlin { jvmToolchain(17) }
