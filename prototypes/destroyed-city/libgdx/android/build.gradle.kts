plugins { id("com.android.application"); kotlin("android") }
val natives by configurations.creating
android {
    namespace = "com.ekron.citypreview"
    compileSdk = 34
    defaultConfig {
        applicationId = "com.ekron.citypreview"
        minSdk = 26
        targetSdk = 34
        versionCode = 1
        versionName = "0.1-libgdx-city"
        ndk { abiFilters += "arm64-v8a" }
    }
    compileOptions { sourceCompatibility = JavaVersion.VERSION_17; targetCompatibility = JavaVersion.VERSION_17 }
    kotlinOptions { jvmTarget = "17" }
    sourceSets["main"].jniLibs.srcDir(layout.buildDirectory.dir("generated/jniLibs"))
}
dependencies {
    implementation(project(":core"))
    implementation("com.badlogicgames.gdx:gdx-backend-android:1.12.1")
    natives("com.badlogicgames.gdx:gdx-platform:1.12.1:natives-arm64-v8a")
}
val extractNatives by tasks.registering(Copy::class) {
    from({ natives.map { zipTree(it) } })
    include("**/*.so")
    eachFile { path = name }
    includeEmptyDirs = false
    into(layout.buildDirectory.dir("generated/jniLibs/arm64-v8a"))
}
tasks.named("preBuild") { dependsOn(extractNatives) }
