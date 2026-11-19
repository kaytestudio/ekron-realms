plugins { kotlin("jvm"); `java-library` }
kotlin { jvmToolchain(17) }
dependencies {
    api("com.badlogicgames.gdx:gdx:1.12.1")
    testImplementation("junit:junit:4.13.2")
}
