plugins {
    alias(libs.plugins.android.application)
    alias(libs.plugins.jetbrains.kotlin.android)
}

// The app version is the project version, project(EkronRealms VERSION x.y.z)
// in the repository's CMakeLists.txt, like the desktop packages and the
// release tags (vx.y.z). versionCode grows with it: x * 10000 + y * 100 + z.
val ekronVersion: String = rootProject.file("../../CMakeLists.txt").let { cmake ->
    val match = Regex("""(?m)^project\(EkronRealms VERSION ([0-9]+)\.([0-9]+)\.([0-9]+)""")
        .find(if (cmake.isFile) cmake.readText() else "")
        ?: throw GradleException("project(EkronRealms VERSION x.y.z) not found in ${cmake.path}")
    match.groupValues.drop(1).joinToString(".")
}
val ekronVersionCode: Int = ekronVersion.split('.').map(String::toInt).let { (x, y, z) -> x * 10000 + y * 100 + z }

android {
    namespace = "com.example.realms"
    compileSdk = 34

    androidResources { noCompress += "mp4" }
    defaultConfig {
        applicationId = "com.example.realms"
        minSdk = 30
        targetSdk = 34
        versionCode = ekronVersionCode
        versionName = ekronVersion
        // Shipped UI languages: English (default), Russian, Portuguese (Portugal), Spanish (Spain).
        resourceConfigurations += listOf("en", "ru", "pt-rPT", "es-rES")
        externalNativeBuild {
            cmake { cppFlags += "-std=c++17" }
        }

        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_1_8
        targetCompatibility = JavaVersion.VERSION_1_8
    }
    kotlinOptions {
        jvmTarget = "1.8"
    }
    buildFeatures {
        prefab = true
    }
    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }
}

dependencies {

    implementation(libs.androidx.core.ktx)
    implementation(libs.androidx.appcompat)
    implementation(libs.material)
    implementation(libs.androidx.games.activity)
    testImplementation(libs.junit)
    androidTestImplementation(libs.androidx.junit)
    androidTestImplementation(libs.androidx.espresso.core)
}